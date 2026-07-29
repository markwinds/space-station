#include "ssh/ssh_websocket.hpp"
#include "logging/logger.hpp"

#include <openssl/rand.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace spacestation::ssh
{
namespace
{
using namespace std::chrono_literals;
constexpr auto kResumeWindow = 90s;

struct WebSocketState
{
    std::string connection_id;
    std::string peer;
    std::string tab_id;
    std::string resume_token;
    std::shared_ptr<SshSession> session;
    std::chrono::steady_clock::time_point opened_at = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_native_pong = opened_at;
    std::chrono::steady_clock::time_point last_client_ping = opened_at;
    bool terminate_requested = false;
};

std::string RandomHex(std::size_t byte_count)
{
    std::vector<unsigned char> bytes(byte_count);
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1)
    {
        throw std::runtime_error("无法生成 SSH 会话恢复令牌。");
    }
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const auto byte : bytes)
    {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0f]);
    }
    return result;
}

void SendError(const drogon::WebSocketConnectionPtr& connection, const std::string& message)
{
    connection->send(nlohmann::json({{"type", "error"}, {"message", message}}).dump());
}

std::string CloseFrameDescription(const std::string& message)
{
    if (message.size() < 2) return "code=none";
    const auto code = (static_cast<std::uint16_t>(static_cast<unsigned char>(message[0])) << 8) |
                      static_cast<unsigned char>(message[1]);
    auto result = "code=" + std::to_string(code);
    if (message.size() > 2) result += " reason=" + message.substr(2);
    return result;
}
} // namespace

SshWebSocketController::SshWebSocketController(ConfigStore& config_store,
                                               plugins::TerminalPluginService& plugin_service)
    : config_store_(config_store), plugin_service_(plugin_service)
{
}

void SshWebSocketController::handleNewConnection(const drogon::HttpRequestPtr& request,
                                                 const drogon::WebSocketConnectionPtr& connection)
{
    if (!request->isOnSecureConnection() && !request->peerAddr().isLoopbackIp())
    {
        connection->shutdown(drogon::CloseCode::kViolation, "SSH terminal requires HTTPS");
        return;
    }
    connection->setPingMessage("space-station", std::chrono::seconds(20));
    auto state = std::make_shared<WebSocketState>();
    state->connection_id = RandomHex(8);
    state->peer = request->peerAddr().toIpPort();
    connection->setContext(state);
    const auto message = "SSH WebSocket opened: connection=" + state->connection_id + " peer=" + state->peer;
    logI(message.c_str());
    connection->send(nlohmann::json({{"type", "ready"}, {"connectionId", state->connection_id}}).dump());
}

void SshWebSocketController::handleNewMessage(const drogon::WebSocketConnectionPtr& connection,
                                              std::string&& message,
                                              const drogon::WebSocketMessageType& type)
{
    const auto state = connection->getContext<WebSocketState>();
    if (!state) return;
    if (type == drogon::WebSocketMessageType::Pong)
    {
        state->last_native_pong = std::chrono::steady_clock::now();
        return;
    }
    if (type == drogon::WebSocketMessageType::Close)
    {
        const auto log_message = "SSH WebSocket close frame received: connection=" + state->connection_id + " " +
                                 CloseFrameDescription(message);
        logI(log_message.c_str());
        return;
    }
    if (type == drogon::WebSocketMessageType::Binary)
    {
        if (message.size() > 1024 * 1024)
        {
            SendError(connection, "单次终端输入过大。");
            return;
        }
        if (state->session && state->session->IsAttachedTo(state->connection_id))
        {
            state->session->Write(std::move(message));
        }
        return;
    }
    if (type != drogon::WebSocketMessageType::Text || message.size() > 2 * 1024 * 1024)
    {
        return;
    }
    const auto json = nlohmann::json::parse(message, nullptr, false);
    if (json.is_discarded() || !json.is_object())
    {
        SendError(connection, "SSH WebSocket 消息格式无效。");
        return;
    }
    const auto action = json.value("type", "");
    if (action == "connect")
    {
        HandleConnect(connection, json);
        return;
    }
    if (action == "resume")
    {
        HandleResume(connection, json);
        return;
    }
    if (action == "terminate")
    {
        HandleTerminate(connection);
        return;
    }
    if (action == "client-ping")
    {
        state->last_client_ping = std::chrono::steady_clock::now();
        connection->send(nlohmann::json({{"type", "client-pong"},
                                         {"at", json.value("at", std::uint64_t{0})},
                                         {"connectionId", state->connection_id}}).dump());
        return;
    }
    const auto session = state->session;
    if (!session)
    {
        SendError(connection, "SSH 会话尚未创建。");
        return;
    }
    if (!session->IsAttachedTo(state->connection_id)) return;
    if (action == "input")
    {
        session->Write(json.value("data", ""));
    }
    else if (action == "resize")
    {
        session->Resize(json.value("columns", 100), json.value("rows", 30));
    }
    else if (action == "trust-host")
    {
        session->ConfirmHostKey(json.value("trusted", false));
    }
    else
    {
        SendError(connection, "未知的 SSH WebSocket 操作。");
    }
}

void SshWebSocketController::handleConnectionClosed(const drogon::WebSocketConnectionPtr& connection)
{
    const auto state = connection->getContext<WebSocketState>();
    if (!state) return;
    const auto now = std::chrono::steady_clock::now();
    const auto pong_age = std::chrono::duration_cast<std::chrono::milliseconds>(now - state->last_native_pong).count();
    const auto ping_age = std::chrono::duration_cast<std::chrono::milliseconds>(now - state->last_client_ping).count();
    const auto log_message = "SSH WebSocket closed: connection=" + state->connection_id + " peer=" + state->peer +
                             " tab=" + state->tab_id +
                             " nativePongAgeMs=" + std::to_string(pong_age) +
                             " clientPingAgeMs=" + std::to_string(ping_age) +
                             " action=" + (state->terminate_requested ? "terminate" : "detach");
    logI(log_message.c_str());
    if (!state->terminate_requested && state->session && state->session->Detach(state->connection_id))
    {
        const auto session = state->session;
        const auto resume_token = state->resume_token;
        drogon::app().getLoop()->runAfter(kResumeWindow.count(), [this, resume_token, session] {
            ExpireDetachedSession(resume_token, session);
        });
    }
    connection->clearContext();
}

void SshWebSocketController::HandleConnect(const drogon::WebSocketConnectionPtr& connection,
                                           const nlohmann::json& message)
{
    const auto state = connection->getContext<WebSocketState>();
    if (!state)
    {
        SendError(connection, "WebSocket 连接状态不存在。");
        return;
    }
    if (state->session)
    {
        SendError(connection, "当前 WebSocket 已经绑定 SSH 会话。");
        return;
    }
    state->tab_id = message.value("tabId", "");
    const auto host_id = message.value("hostId", "");
    nlohmann::json selected;
    for (const auto& host : config_store_.LoadSshHosts())
    {
        if (host.is_object() && host.value("id", "") == host_id)
        {
            selected = host;
            break;
        }
    }
    if (!selected.is_object())
    {
        SendError(connection, "找不到要连接的 SSH 主机。");
        return;
    }
    SshConnectOptions options;
    options.host_id = host_id;
    options.host = selected.value("host", "");
    options.port = std::clamp(selected.value("port", 22), 1, 65535);
    options.username = selected.value("username", "");
    options.expected_fingerprint = selected.value("hostKeySha256", "");
    options.jump_host_id = selected.value("jumpHostId", "");
    options.password = message.value("password", "");
    options.private_key = message.value("privateKey", "");
    options.passphrase = message.value("passphrase", "");
    options.save_credential = message.value("saveCredential", false);
    options.use_agent = message.value("useAgent", false);
    if (!options.use_agent && options.password.empty() && options.private_key.empty())
    {
        try
        {
            if (const auto saved = config_store_.LoadSshCredential(host_id))
            {
                options.password = saved->value("password", "");
                options.private_key = saved->value("privateKey", "");
                options.passphrase = saved->value("passphrase", "");
                options.save_credential = false;
            }
        }
        catch (const std::exception& error)
        {
            SendError(connection, error.what());
            return;
        }
    }
    options.columns = std::clamp(message.value("columns", 100), 2, 1000);
    options.rows = std::clamp(message.value("rows", 30), 2, 1000);
    if (options.host.empty() || options.username.empty() ||
        (!options.use_agent && options.password.empty() && options.private_key.empty()))
    {
        SendError(connection, "主机、用户名和认证凭据不能为空。");
        return;
    }
    const auto resume_token = RandomHex(32);
    auto session = std::make_shared<SshSession>(config_store_, connection, state->connection_id, &plugin_service_);
    {
        std::lock_guard lock(sessions_mutex_);
        resumable_sessions_[resume_token] = session;
    }
    state->session = session;
    state->resume_token = resume_token;
    connection->send(nlohmann::json({{"type", "session"},
                                     {"resumeToken", resume_token},
                                     {"resumeWindowSeconds", kResumeWindow.count()}}).dump());
    session->Start(std::move(options));
}

void SshWebSocketController::HandleResume(const drogon::WebSocketConnectionPtr& connection,
                                          const nlohmann::json& message)
{
    const auto state = connection->getContext<WebSocketState>();
    if (!state || state->session)
    {
        SendError(connection, "当前 WebSocket 已经绑定 SSH 会话。");
        return;
    }
    const auto resume_token = message.value("resumeToken", "");
    const auto last_sequence = message.value("lastSequence", std::uint64_t{0});
    state->tab_id = message.value("tabId", "");
    std::shared_ptr<SshSession> session;
    const auto next_resume_token = RandomHex(32);
    {
        std::lock_guard lock(sessions_mutex_);
        const auto found = resumable_sessions_.find(resume_token);
        if (found == resumable_sessions_.end())
        {
            connection->send(nlohmann::json({{"type", "resume-failed"},
                                             {"message", "SSH 会话恢复窗口已过期。"}}).dump());
            return;
        }
        session = found->second;
        resumable_sessions_.erase(found);
        resumable_sessions_[next_resume_token] = session;
    }

    state->session = session;
    state->resume_token = next_resume_token;
    connection->send(nlohmann::json({{"type", "resumed"},
                                     {"resumeToken", next_resume_token},
                                     {"resumeWindowSeconds", kResumeWindow.count()}}).dump());
    const auto result = session->Attach(connection, state->connection_id, last_sequence);
    if (result.gap)
    {
        connection->send(nlohmann::json({{"type", "resume-gap"},
                                         {"earliestSequence", result.earliest_sequence},
                                         {"latestSequence", result.latest_sequence}}).dump());
    }
    const auto log_message = "SSH session resumed: connection=" + state->connection_id +
                             " tab=" + state->tab_id +
                             " lastSequence=" + std::to_string(last_sequence) +
                             " replayed=" + std::to_string(result.replayed_messages) +
                             " gap=" + (result.gap ? "true" : "false");
    logI(log_message.c_str());
}

void SshWebSocketController::HandleTerminate(const drogon::WebSocketConnectionPtr& connection)
{
    const auto state = connection->getContext<WebSocketState>();
    if (!state) return;
    state->terminate_requested = true;
    const auto session = std::exchange(state->session, {});
    if (session)
    {
        std::lock_guard lock(sessions_mutex_);
        const auto found = resumable_sessions_.find(state->resume_token);
        if (found != resumable_sessions_.end() && found->second == session) resumable_sessions_.erase(found);
    }
    if (session)
    {
        session->Detach(state->connection_id);
        session->Stop();
    }
    state->resume_token.clear();
    connection->send(nlohmann::json({{"type", "terminated"}}).dump());
}

bool SshWebSocketController::TerminateSession(const std::string& resume_token)
{
    if (resume_token.empty()) return false;
    std::shared_ptr<SshSession> session;
    {
        std::lock_guard lock(sessions_mutex_);
        const auto found = resumable_sessions_.find(resume_token);
        if (found == resumable_sessions_.end()) return false;
        session = found->second;
        resumable_sessions_.erase(found);
    }
    session->Stop();
    return true;
}

void SshWebSocketController::ExpireDetachedSession(const std::string& resume_token,
                                                   const std::shared_ptr<SshSession>& session)
{
    if (!session->DetachedFor(kResumeWindow)) return;
    {
        std::lock_guard lock(sessions_mutex_);
        const auto found = resumable_sessions_.find(resume_token);
        if (found == resumable_sessions_.end() || found->second != session) return;
        resumable_sessions_.erase(found);
    }
    logI("Detached SSH session expired after 90 seconds; stopping session");
    session->Stop();
}
} // namespace spacestation::ssh
