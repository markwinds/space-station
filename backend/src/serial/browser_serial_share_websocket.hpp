#pragma once

#include "serial/browser_serial_share_service.hpp"

#include <drogon/WebSocketController.h>

namespace spacestation::serial
{
class BrowserSerialShareWebSocketController
    : public drogon::WebSocketController<BrowserSerialShareWebSocketController, false>
{
  public:
    explicit BrowserSerialShareWebSocketController(BrowserSerialShareService& service);
    void handleNewMessage(const drogon::WebSocketConnectionPtr& connection,
                          std::string&& message,
                          const drogon::WebSocketMessageType& type) override;
    void handleNewConnection(const drogon::HttpRequestPtr& request,
                             const drogon::WebSocketConnectionPtr& connection) override;
    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& connection) override;

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/api/tools/serial/browser-share");
    WS_PATH_LIST_END

  private:
    BrowserSerialShareService& service_;
};
} // namespace spacestation::serial
