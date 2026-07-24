#pragma once

#include <drogon/WebSocketConnection.h>
#include <nlohmann/json.hpp>

#include <mutex>
#include <string>
#include <unordered_map>

namespace spacestation::plugins
{
class TerminalPluginService;
}

namespace spacestation::serial
{
class BrowserSerialShareService
{
  public:
    explicit BrowserSerialShareService(plugins::TerminalPluginService* plugin_service = nullptr);
    ~BrowserSerialShareService();

    nlohmann::json ListShares();
    void Publish(const std::string& share_id,
                 const std::string& name,
                 const std::string& port_label,
                 bool write_enabled,
                 const drogon::WebSocketConnectionPtr& owner);
    void Update(const std::string& share_id,
                const std::string& name,
                bool write_enabled,
                const drogon::WebSocketConnectionPtr& owner);
    std::string Subscribe(const std::string& share_id, const drogon::WebSocketConnectionPtr& connection);
    void OwnerData(const std::string& share_id,
                   const drogon::WebSocketConnectionPtr& owner,
                   std::string data);
    void SubscriberWrite(const std::string& subscription_id, std::string data);
    void Unpublish(const std::string& share_id, const drogon::WebSocketConnectionPtr& owner);
    void Detach(const std::string& subscription_id);

  private:
    struct Share
    {
        std::string id;
        std::string name;
        std::string port_label;
        std::string plugin_session_id;
        bool write_enabled = false;
        std::weak_ptr<drogon::WebSocketConnection> owner;
        std::unordered_map<std::string, std::weak_ptr<drogon::WebSocketConnection>> subscribers;
        std::string backlog;
    };

    void BroadcastState(const std::string& share_id);
    void PluginWrite(const std::string& share_id, std::string data);

    plugins::TerminalPluginService* plugin_service_ = nullptr;
    std::mutex mutex_;
    std::unordered_map<std::string, Share> shares_;
    std::unordered_map<std::string, std::string> subscriptions_;
};
} // namespace spacestation::serial
