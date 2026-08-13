#pragma once

#include "config/config_store.hpp"

#include <drogon/WebSocketConnection.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

namespace spacestation::telnet
{
struct TelnetConnectOptions
{
    std::string host_id;
    std::string host;
    std::string username;
    std::string password;
    bool save_credential = false;
    int port = 23;
    int columns = 100;
    int rows = 30;
};

class TelnetSession
{
  public:
    TelnetSession(ConfigStore& config_store, drogon::WebSocketConnectionPtr connection);
    ~TelnetSession();

    TelnetSession(const TelnetSession&) = delete;
    TelnetSession& operator=(const TelnetSession&) = delete;

    void Start(TelnetConnectOptions options);
    void Write(std::string data);
    void Resize(int columns, int rows);
    void Stop();

  private:
    struct Command
    {
        bool resize = false;
        std::string data;
        int columns = 0;
        int rows = 0;
    };

    void Run(std::stop_token stop_token, TelnetConnectOptions options);
    void SendEvent(const nlohmann::json& event) const;

    ConfigStore& config_store_;
    std::weak_ptr<drogon::WebSocketConnection> connection_;
    std::jthread worker_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Command> commands_;
};
} // namespace spacestation::telnet
