#include "serial/serial_websocket.hpp"
#include "logging/logger.hpp"

#include <stdexcept>
#include <utility>

namespace spacestation::serial
{
namespace
{
struct ConnectionState
{
    std::string subscription_id;
};

void SendError(const drogon::WebSocketConnectionPtr& connection, const std::string& message)
{
    connection->send(nlohmann::json({{"type", "error"}, {"message", message}}).dump());
}
} // namespace

SerialWebSocketController::SerialWebSocketController(SerialService& service) : service_(service) {}

void SerialWebSocketController::handleNewConnection(const drogon::HttpRequestPtr& request,
                                                     const drogon::WebSocketConnectionPtr& connection)
{
    if (!request->isOnSecureConnection() && !request->peerAddr().isLoopbackIp())
    {
        connection->shutdown(drogon::CloseCode::kViolation, "Serial requires HTTPS");
        return;
    }
    connection->setContext(std::make_shared<ConnectionState>());
    const auto log_message = "Serial WebSocket opened: peer=" + request->peerAddr().toIpPort();
    logI(log_message.c_str());
    connection->send(nlohmann::json({{"type", "ready"}}).dump());
}

void SerialWebSocketController::handleNewMessage(const drogon::WebSocketConnectionPtr& connection,
                                                  std::string&& message,
                                                  const drogon::WebSocketMessageType& type)
{
    const auto state = connection->getContext<ConnectionState>();
    if (!state) return;
    try
    {
        if (type == drogon::WebSocketMessageType::Binary)
        {
            service_.Write(state->subscription_id, std::move(message));
            return;
        }
        if (type != drogon::WebSocketMessageType::Text) return;
        const auto payload = nlohmann::json::parse(message, nullptr, false);
        if (!payload.is_object()) throw std::runtime_error("串口消息格式无效。");
        const auto action = payload.value("type", "");
        if (action == "ping")
        {
            connection->send(nlohmann::json({{"type", "pong"}, {"at", payload.value("at", 0LL)}}).dump());
        }
        else if (action == "open")
        {
            if (!state->subscription_id.empty()) throw std::runtime_error("当前连接已经打开串口。");
            state->subscription_id = service_.Attach(payload.value("port", ""),
                                                     SerialOptions::FromJson(payload.value("options", nlohmann::json::object())),
                                                     connection);
        }
        else if (action == "close")
        {
            service_.Detach(std::exchange(state->subscription_id, {}));
            connection->send(nlohmann::json({{"type", "closed"}}).dump());
        }
        else throw std::runtime_error("未知的串口操作。");
    }
    catch (const std::exception& error)
    {
        const auto log_message = std::string("Serial WebSocket request failed: ") + error.what();
        logW(log_message.c_str());
        SendError(connection, error.what());
    }
}

void SerialWebSocketController::handleConnectionClosed(const drogon::WebSocketConnectionPtr& connection)
{
    logI("Serial WebSocket closed");
    if (const auto state = connection->getContext<ConnectionState>())
        service_.Detach(state->subscription_id);
    connection->clearContext();
}
} // namespace spacestation::serial
