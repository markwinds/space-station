#pragma once

#include "config/config_store.hpp"
#include "transfer/transfer_manager.hpp"

#include <drogon/drogon.h>
#include <atomic>
#include <cstdint>
#include <thread>

namespace spacestation
{
inline constexpr std::uint16_t kDefaultPort = 443;

class HttpServer
{
  public:
    explicit HttpServer(ConfigStore& config_store, AppConfig config);
    ~HttpServer();

    void Start();
    void Stop();
    std::string UiUrl() const;

  private:
    void RegisterRoutes();
    void HandleStaticAsset(const std::string& request_path,
                           std::function<void(const drogon::HttpResponsePtr&)>&& callback) const;

    std::uint16_t port_;
    bool http_enabled_;
    std::uint16_t http_port_;
    std::string certificate_path_;
    std::string private_key_path_;
    std::string trusted_root_certificate_path_;
    ConfigStore& config_store_;
    transfer::TransferManager transfer_manager_;
    std::thread server_thread_;
    std::atomic<bool> started_{false};
};
} // namespace spacestation
