#pragma once

#include <drogon/WebSocketConnection.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace spacestation::plugins
{
class TerminalPluginService;
}

namespace spacestation::serial
{
struct SerialOptions
{
    int baud_rate = 115200;
    int data_bits = 8;
    int stop_bits = 1;
    std::string parity = "none";
    std::string flow_control = "none";

    bool operator==(const SerialOptions&) const = default;
    nlohmann::json ToJson() const;
    static SerialOptions FromJson(const nlohmann::json& value);
};

class SerialService
{
  public:
    explicit SerialService(plugins::TerminalPluginService* plugin_service = nullptr);
    ~SerialService();

    nlohmann::json ListPorts() const;
    std::string Attach(const std::string& port,
                       const SerialOptions& options,
                       const drogon::WebSocketConnectionPtr& connection);
    void Detach(const std::string& subscription_id);
    void Write(const std::string& subscription_id, std::string data);

  private:
    class Session;
    struct Subscription
    {
        std::weak_ptr<Session> session;
        std::string port;
    };

    mutable std::mutex mutex_;
    plugins::TerminalPluginService* plugin_service_ = nullptr;
    std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;
    std::unordered_map<std::string, Subscription> subscriptions_;
};
} // namespace spacestation::serial
