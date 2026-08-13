#pragma once

#include "config/config_store.hpp"
#include "telnet/telnet_session.hpp"

#include <drogon/WebSocketController.h>

namespace spacestation::telnet
{
class TelnetWebSocketController : public drogon::WebSocketController<TelnetWebSocketController, false>
{
  public:
    explicit TelnetWebSocketController(ConfigStore& config_store);

    void handleNewMessage(const drogon::WebSocketConnectionPtr& connection,
                          std::string&& message,
                          const drogon::WebSocketMessageType& type) override;
    void handleNewConnection(const drogon::HttpRequestPtr& request,
                             const drogon::WebSocketConnectionPtr& connection) override;
    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& connection) override;

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/api/tools/telnet/terminal");
    WS_PATH_LIST_END

  private:
    ConfigStore& config_store_;
};
} // namespace spacestation::telnet
