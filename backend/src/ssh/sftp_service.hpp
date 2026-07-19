#pragma once

#include "config/config_store.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace spacestation::ssh
{
struct SftpDownload
{
    std::string filename;
    std::string content;
};

class SftpService
{
  public:
    explicit SftpService(ConfigStore& config_store);
    ~SftpService();

    nlohmann::json List(const std::string& host_id, const std::string& path) const;
    void CreateDirectory(const std::string& host_id, const std::string& path) const;
    void Remove(const std::string& host_id, const std::string& path, bool directory) const;
    void Rename(const std::string& host_id, const std::string& from, const std::string& to) const;
    void Upload(const std::string& host_id, const std::string& path, std::string_view content) const;
    SftpDownload Download(const std::string& host_id, const std::string& path) const;
    nlohmann::json StartForward(const std::string& host_id, int local_port,
                                const std::string& remote_host, int remote_port);
    void StopForward(const std::string& id);
    nlohmann::json ForwardState() const;

  private:
    struct Forward;
    ConfigStore& config_store_;
    mutable std::mutex forwards_mutex_;
    std::unordered_map<std::string, std::shared_ptr<Forward>> forwards_;
};
} // namespace spacestation::ssh
