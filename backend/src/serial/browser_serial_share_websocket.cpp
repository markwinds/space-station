#include "serial/browser_serial_share_websocket.hpp"

#include "logging/logger.hpp"

#include <stdexcept>

namespace spacestation::serial
{
namespace
{
struct ConnectionState
{
    std::string role;
    std::string share_id;
    std::string subscription_id;
};

void Send(const drogon::WebSocketConnectionPtr& connection, const std::string& type, const std::string& message)
{
    connection->send(nlohmann::json({{"type", type}, {"message", message}}).dump());
}
} // namespace

BrowserSerialShareWebSocketController::BrowserSerialShareWebSocketController(BrowserSerialShareService& service)
    : service_(service)
{
}

void BrowserSerialShareWebSocketController::handleNewConnection(const drogon::HttpRequestPtr& request,
                                                                 const drogon::WebSocketConnectionPtr& connection)
{
    if (!request->isOnSecureConnection() && !request->peerAddr().isLoopbackIp())
    {
        connection->shutdown(drogon::CloseCode::kViolation, "Browser serial sharing requires HTTPS");
        return;
    }
    connection->setContext(std::make_shared<ConnectionState>());
    connection->send(nlohmann::json({{"type", "ready"}}).dump());
}

void BrowserSerialShareWebSocketController::handleNewMessage(const drogon::WebSocketConnectionPtr& connection,
                                                              std::string&& message,
                                                              const drogon::WebSocketMessageType& type)
{
    const auto state = connection->getContext<ConnectionState>();
    if (!state) return;
    try
    {
        if (type == drogon::WebSocketMessageType::Binary)
        {
            if (state->role == "owner") service_.OwnerData(state->share_id, connection, std::move(message));
            else if (state->role == "subscriber") service_.SubscriberWrite(state->subscription_id, std::move(message));
            else throw std::runtime_error("共享串口角色尚未建立。");
            return;
        }
        if (type != drogon::WebSocketMessageType::Text) return;
        const auto payload = nlohmann::json::parse(message, nullptr, false);
        if (!payload.is_object()) throw std::runtime_error("共享串口消息格式无效。");
        const auto action = payload.value("type", "");
        if (action == "ping")
        {
            connection->send(nlohmann::json({{"type", "pong"}, {"at", payload.value("at", 0LL)}}).dump());
        }
        else if (action == "publish")
        {
            if (!state->role.empty()) throw std::runtime_error("当前连接已经绑定共享串口。");
            state->role = "owner";
            state->share_id = payload.value("shareId", "");
            service_.Publish(state->share_id, payload.value("name", ""), payload.value("portLabel", ""),
                             payload.value("writeEnabled", false), connection);
            connection->send(nlohmann::json({{"type", "published"}, {"shareId", state->share_id}}).dump());
        }
        else if (action == "subscribe")
        {
            if (!state->role.empty()) throw std::runtime_error("当前连接已经绑定共享串口。");
            state->role = "subscriber";
            state->share_id = payload.value("shareId", "");
            state->subscription_id = service_.Subscribe(state->share_id, connection);
        }
        else if (action == "update")
        {
            if (state->role != "owner") throw std::runtime_error("只有共享拥有者可以修改共享设置。");
            service_.Update(state->share_id, payload.value("name", ""), payload.value("writeEnabled", false), connection);
        }
        else throw std::runtime_error("未知的共享串口操作。");
    }
    catch (const std::exception& error)
    {
        Send(connection, type == drogon::WebSocketMessageType::Binary ? "write-error" : "error", error.what());
    }
}

void BrowserSerialShareWebSocketController::handleConnectionClosed(const drogon::WebSocketConnectionPtr& connection)
{
    if (const auto state = connection->getContext<ConnectionState>())
    {
        if (state->role == "owner") service_.Unpublish(state->share_id, connection);
        else if (state->role == "subscriber") service_.Detach(state->subscription_id);
    }
    connection->clearContext();
}
} // namespace spacestation::serial
