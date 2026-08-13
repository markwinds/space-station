#include "telnet/telnet_websocket.hpp"
#include "logging/logger.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>

namespace spacestation::telnet
{
namespace
{
struct ConnectionState
{
    std::shared_ptr<TelnetSession> session;
};

void SendError(const drogon::WebSocketConnectionPtr& connection, const std::string& message)
{
    connection->send(nlohmann::json({{"type", "error"}, {"message", message}}).dump());
}
} // namespace

TelnetWebSocketController::TelnetWebSocketController(ConfigStore& config_store) : config_store_(config_store) {}

void TelnetWebSocketController::handleNewConnection(const drogon::HttpRequestPtr& request,
                                                     const drogon::WebSocketConnectionPtr& connection)
{
    if (!request->isOnSecureConnection() && !request->peerAddr().isLoopbackIp())
    {
        connection->shutdown(drogon::CloseCode::kViolation, "Telnet terminal requires HTTPS");
        return;
    }
    connection->setContext(std::make_shared<ConnectionState>());
    connection->send(nlohmann::json({{"type", "ready"}}).dump());
}

void TelnetWebSocketController::handleNewMessage(const drogon::WebSocketConnectionPtr& connection,
                                                  std::string&& message,
                                                  const drogon::WebSocketMessageType& type)
{
    const auto state = connection->getContext<ConnectionState>();
    if (!state) return;
    if (type == drogon::WebSocketMessageType::Binary)
    {
        if (message.size() <= 1024 * 1024 && state->session) state->session->Write(std::move(message));
        return;
    }
    if (type != drogon::WebSocketMessageType::Text || message.size() > 2 * 1024 * 1024) return;
    const auto payload = nlohmann::json::parse(message, nullptr, false);
    if (!payload.is_object()) { SendError(connection, "Telnet 消息格式无效。"); return; }
    const auto action = payload.value("type", "");
    if (action == "client-ping")
    {
        connection->send(nlohmann::json({{"type", "client-pong"}, {"at", payload.value("at", 0LL)}}).dump());
        return;
    }
    if (action == "terminate")
    {
        if (state->session) state->session->Stop();
        state->session.reset();
        connection->send(nlohmann::json({{"type", "terminated"}}).dump());
        return;
    }
    if (action == "resize")
    {
        if (state->session) state->session->Resize(payload.value("columns", 100), payload.value("rows", 30));
        return;
    }
    if (action != "connect") { SendError(connection, "未知的 Telnet 操作。"); return; }
    if (state->session) { SendError(connection, "当前连接已经打开 Telnet 会话。"); return; }
    const auto host_id = payload.value("hostId", "");
    nlohmann::json selected;
    for (const auto& host : config_store_.LoadSshHosts())
        if (host.is_object() && host.value("id", "") == host_id) { selected = host; break; }
    if (!selected.is_object() || selected.value("protocol", "ssh") != "telnet")
    { SendError(connection, "找不到要连接的 Telnet 主机。"); return; }
    TelnetConnectOptions options;
    options.host_id = host_id;
    options.host = selected.value("host", "");
    options.port = std::clamp(selected.value("port", 23), 1, 65535);
    options.username = selected.value("username", "");
    options.password = payload.value("password", "");
    options.save_credential = payload.value("saveCredential", false);
    options.columns = std::clamp(payload.value("columns", 100), 2, 1000);
    options.rows = std::clamp(payload.value("rows", 30), 2, 1000);
    if (options.password.empty())
    {
        try
        {
            if (const auto saved = config_store_.LoadSshCredential(host_id))
                options.password = saved->value("password", "");
        }
        catch (const std::exception& error) { SendError(connection, error.what()); return; }
    }
    if (options.host.empty()) { SendError(connection, "Telnet 主机不能为空。"); return; }
    state->session = std::make_shared<TelnetSession>(config_store_, connection);
    state->session->Start(std::move(options));
}

void TelnetWebSocketController::handleConnectionClosed(const drogon::WebSocketConnectionPtr& connection)
{
    if (const auto state = connection->getContext<ConnectionState>(); state && state->session)
        state->session->Stop();
    connection->clearContext();
    logI("Telnet WebSocket closed");
}
} // namespace spacestation::telnet
