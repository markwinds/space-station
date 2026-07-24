#pragma once

#include "config/config_store.hpp"

#include <drogon/WebSocketConnection.h>
#include <nlohmann/json.hpp>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

namespace spacestation::plugins
{
class TerminalPluginService;
}

namespace spacestation::ssh
{
struct SshConnectOptions
{
    std::string host_id;
    std::string host;
    std::string username;
    std::string password;
    std::string private_key;
    std::string passphrase;
    std::string expected_fingerprint;
    std::string jump_host_id;
    bool save_credential = false;
    bool use_agent = false;
    int port = 22;
    int columns = 100;
    int rows = 30;
};

class SshSession : public std::enable_shared_from_this<SshSession>
{
  public:
    SshSession(ConfigStore& config_store,
               drogon::WebSocketConnectionPtr connection,
               plugins::TerminalPluginService* plugin_service = nullptr);
    ~SshSession();

    SshSession(const SshSession&) = delete;
    SshSession& operator=(const SshSession&) = delete;

    void Start(SshConnectOptions options);
    void Write(std::string data);
    void Resize(int columns, int rows);
    void ConfirmHostKey(bool trusted);
    void Stop();

  private:
    enum class CommandType
    {
        Input,
        Resize,
    };

    struct Command
    {
        CommandType type;
        std::string data;
        int columns = 0;
        int rows = 0;
    };

    void Run(std::stop_token stop_token, SshConnectOptions options);
    void SendEvent(const nlohmann::json& event) const;
    void SendOutput(const char* data, std::size_t size) const;

    ConfigStore& config_store_;
    plugins::TerminalPluginService* plugin_service_ = nullptr;
    std::string plugin_session_id_;
    std::weak_ptr<drogon::WebSocketConnection> connection_;
    std::jthread worker_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Command> commands_;
    bool trust_answered_ = false;
    bool host_trusted_ = false;
};
} // namespace spacestation::ssh
