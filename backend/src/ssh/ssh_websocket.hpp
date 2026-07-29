#pragma once

#include "config/config_store.hpp"
#include "ssh/ssh_session.hpp"

#include <drogon/WebSocketController.h>
#include <mutex>
#include <string>
#include <unordered_map>

namespace spacestation::ssh
{
class SshWebSocketController : public drogon::WebSocketController<SshWebSocketController, false>
{
  public:
    SshWebSocketController(ConfigStore& config_store, plugins::TerminalPluginService& plugin_service);

    void handleNewMessage(const drogon::WebSocketConnectionPtr& connection,
                          std::string&& message,
                          const drogon::WebSocketMessageType& type) override;
    void handleNewConnection(const drogon::HttpRequestPtr& request,
                             const drogon::WebSocketConnectionPtr& connection) override;
    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& connection) override;
    bool TerminateSession(const std::string& resume_token);

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/api/tools/ssh/terminal");
    WS_PATH_LIST_END

  private:
    void HandleConnect(const drogon::WebSocketConnectionPtr& connection, const nlohmann::json& message);
    void HandleResume(const drogon::WebSocketConnectionPtr& connection, const nlohmann::json& message);
    void HandleTerminate(const drogon::WebSocketConnectionPtr& connection);
    void ExpireDetachedSession(const std::string& resume_token, const std::shared_ptr<SshSession>& session);

    ConfigStore& config_store_;
    plugins::TerminalPluginService& plugin_service_;
    std::mutex sessions_mutex_;
    std::unordered_map<std::string, std::shared_ptr<SshSession>> resumable_sessions_;
};
} // namespace spacestation::ssh
