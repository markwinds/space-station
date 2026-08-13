#pragma once

#include <drogon/WebSocketController.h>

namespace spacestation::network
{
class NetworkScanWebSocketController
    : public drogon::WebSocketController<NetworkScanWebSocketController, false>
{
  public:
    void handleNewMessage(const drogon::WebSocketConnectionPtr& connection,
                          std::string&& message,
                          const drogon::WebSocketMessageType& type) override;
    void handleNewConnection(const drogon::HttpRequestPtr& request,
                             const drogon::WebSocketConnectionPtr& connection) override;
    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& connection) override;

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/api/tools/network/scan");
    WS_PATH_LIST_END
};
} // namespace spacestation::network
