#pragma once

#include "config/config_store.hpp"

#include <drogon/drogon.h>
#include <atomic>
#include <cstdint>
#include <thread>

namespace spacestation
{
inline constexpr std::uint16_t kDefaultPort = 34827;

class HttpServer
{
  public:
    explicit HttpServer(ConfigStore& config_store, std::uint16_t port = kDefaultPort);
    ~HttpServer();

    void Start();
    void Stop();
    std::string UiUrl() const;

  private:
    void RegisterRoutes();
    void HandleStaticAsset(const std::string& request_path,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

    std::uint16_t port_;
    ConfigStore& config_store_;
    std::thread server_thread_;
    std::atomic<bool> started_{false};
};
} // namespace spacestation
