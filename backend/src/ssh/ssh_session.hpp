#pragma once

#include "config/config_store.hpp"

#include <drogon/WebSocketConnection.h>
#include <nlohmann/json.hpp>

#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
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
    struct ResumeResult
    {
        bool gap = false;
        std::uint64_t earliest_sequence = 0;
        std::uint64_t latest_sequence = 0;
        std::size_t replayed_messages = 0;
    };

    SshSession(ConfigStore& config_store,
               drogon::WebSocketConnectionPtr connection,
               std::string connection_id,
               plugins::TerminalPluginService* plugin_service = nullptr);
    ~SshSession();

    SshSession(const SshSession&) = delete;
    SshSession& operator=(const SshSession&) = delete;

    void Start(SshConnectOptions options);
    void Write(std::string data);
    void Resize(int columns, int rows);
    void ConfirmHostKey(bool trusted);
    ResumeResult Attach(drogon::WebSocketConnectionPtr connection,
                        std::string connection_id,
                        std::uint64_t last_sequence);
    bool Detach(const std::string& connection_id);
    bool IsAttachedTo(const std::string& connection_id) const;
    bool DetachedFor(std::chrono::steady_clock::duration duration) const;
    void RequestStop();
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

    struct BufferedMessage
    {
        std::uint64_t sequence = 0;
        drogon::WebSocketMessageType type = drogon::WebSocketMessageType::Text;
        std::string data;
    };

    void Run(std::stop_token stop_token, SshConnectOptions options);
    void SendEvent(const nlohmann::json& event);
    void SendOutput(const char* data, std::size_t size);
    void StoreBufferedMessage(BufferedMessage message);

    ConfigStore& config_store_;
    plugins::TerminalPluginService* plugin_service_ = nullptr;
    std::string plugin_session_id_;
    std::weak_ptr<drogon::WebSocketConnection> connection_;
    std::string connection_id_;
    std::optional<std::chrono::steady_clock::time_point> detached_at_;
    std::deque<BufferedMessage> output_buffer_;
    std::uint64_t next_output_sequence_ = 1;
    std::size_t output_buffer_bytes_ = 0;
    std::jthread worker_;
    std::stop_source stop_source_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Command> commands_;
    bool trust_answered_ = false;
    bool host_trusted_ = false;
};
} // namespace spacestation::ssh
