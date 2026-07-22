#pragma once

#include "serial/serial_service.hpp"

#include <drogon/WebSocketController.h>

namespace spacestation::serial
{
class SerialWebSocketController : public drogon::WebSocketController<SerialWebSocketController, false>
{
  public:
    explicit SerialWebSocketController(SerialService& service);

    void handleNewMessage(const drogon::WebSocketConnectionPtr& connection,
                          std::string&& message,
                          const drogon::WebSocketMessageType& type) override;
    void handleNewConnection(const drogon::HttpRequestPtr& request,
                             const drogon::WebSocketConnectionPtr& connection) override;
    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& connection) override;

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/api/tools/serial/session");
    WS_PATH_LIST_END

  private:
    SerialService& service_;
};
} // namespace spacestation::serial
