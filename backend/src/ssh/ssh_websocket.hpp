#pragma once

#include "config/config_store.hpp"
#include "ssh/ssh_session.hpp"

#include <drogon/WebSocketController.h>

namespace spacestation::ssh
{
class SshWebSocketController : public drogon::WebSocketController<SshWebSocketController, false>
{
  public:
    explicit SshWebSocketController(ConfigStore& config_store);

    void handleNewMessage(const drogon::WebSocketConnectionPtr& connection,
                          std::string&& message,
                          const drogon::WebSocketMessageType& type) override;
    void handleNewConnection(const drogon::HttpRequestPtr& request,
                             const drogon::WebSocketConnectionPtr& connection) override;
    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& connection) override;

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/api/tools/ssh/terminal");
    WS_PATH_LIST_END

  private:
    void HandleConnect(const drogon::WebSocketConnectionPtr& connection, const nlohmann::json& message);

    ConfigStore& config_store_;
};
} // namespace spacestation::ssh
