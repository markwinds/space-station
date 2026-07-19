#include "ssh/ssh_websocket.hpp"

#include <algorithm>

namespace spacestation::ssh
{
namespace
{
void SendError(const drogon::WebSocketConnectionPtr& connection, const std::string& message)
{
    connection->send(nlohmann::json({{"type", "error"}, {"message", message}}).dump());
}
} // namespace

SshWebSocketController::SshWebSocketController(ConfigStore& config_store) : config_store_(config_store)
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
    connection->send(nlohmann::json({{"type", "ready"}}).dump());
}

void SshWebSocketController::handleNewMessage(const drogon::WebSocketConnectionPtr& connection,
                                              std::string&& message,
                                              const drogon::WebSocketMessageType& type)
{
    if (type == drogon::WebSocketMessageType::Binary)
    {
        if (message.size() > 1024 * 1024)
        {
            SendError(connection, "单次终端输入过大。");
            return;
        }
        if (const auto session = connection->getContext<SshSession>())
        {
            session->Write(std::move(message));
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
    const auto session = connection->getContext<SshSession>();
    if (!session)
    {
        SendError(connection, "SSH 会话尚未创建。");
        return;
    }
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
    if (const auto session = connection->getContext<SshSession>())
    {
        session->Stop();
    }
    connection->clearContext();
}

void SshWebSocketController::HandleConnect(const drogon::WebSocketConnectionPtr& connection,
                                           const nlohmann::json& message)
{
    if (connection->hasContext())
    {
        SendError(connection, "当前 WebSocket 已经绑定 SSH 会话。");
        return;
    }
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
    auto session = std::make_shared<SshSession>(config_store_, connection);
    connection->setContext(session);
    session->Start(std::move(options));
}
} // namespace spacestation::ssh
