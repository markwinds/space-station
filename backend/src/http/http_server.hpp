#pragma once

#include "authenticator/authenticator_service.hpp"
#include "config/config_store.hpp"
#include "serial/serial_service.hpp"
#include "serial/browser_serial_share_service.hpp"
#include "ssh/sftp_service.hpp"
#include "transfer/transfer_manager.hpp"

#include <memory>

#include <drogon/drogon.h>
#include <atomic>
#include <cstdint>
#include <thread>

namespace spacestation
{
namespace ssh
{
class SshWebSocketController;
}
namespace serial
{
class SerialWebSocketController;
class BrowserSerialShareWebSocketController;
}
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
    authenticator::AuthenticatorService authenticator_service_;
    transfer::TransferManager transfer_manager_;
    serial::SerialService serial_service_;
    serial::BrowserSerialShareService browser_serial_share_service_;
    ssh::SftpService sftp_service_;
    std::shared_ptr<serial::SerialWebSocketController> serial_websocket_controller_;
    std::shared_ptr<serial::BrowserSerialShareWebSocketController> browser_serial_share_websocket_controller_;
    std::shared_ptr<ssh::SshWebSocketController> ssh_websocket_controller_;
    std::thread server_thread_;
    std::atomic<bool> started_{false};
};
} // namespace spacestation
