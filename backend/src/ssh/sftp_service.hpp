#pragma once

#include "config/config_store.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace spacestation::ssh
{
class SftpService
{
  public:
    explicit SftpService(ConfigStore& config_store);
    ~SftpService();

    nlohmann::json List(const std::string& host_id, const std::string& path) const;
    void CreateDirectory(const std::string& host_id, const std::string& path) const;
    void Remove(const std::string& host_id, const std::string& path, bool directory) const;
    void Rename(const std::string& host_id, const std::string& from, const std::string& to) const;
    void StartUploadChunk(const std::string& upload_id,
                          const std::string& host_id,
                          const std::string& path,
                          std::uint64_t offset,
                          std::uint64_t total_size,
                          std::string content,
                          std::function<void(const std::string&)> on_complete);
    void StartDownload(const std::string& host_id,
                       const std::string& path,
                       std::function<bool(std::string_view)> on_chunk,
                       std::function<void(const std::string&)> on_complete);
    nlohmann::json StartForward(const std::string& host_id, int local_port,
                                const std::string& remote_host, int remote_port);
    void StopForward(const std::string& id);
    nlohmann::json ForwardState() const;

  private:
    struct Forward;
    struct DownloadWorker;
    struct UploadWorker;
    ConfigStore& config_store_;
    mutable std::mutex forwards_mutex_;
    std::unordered_map<std::string, std::shared_ptr<Forward>> forwards_;
    std::mutex downloads_mutex_;
    std::vector<std::shared_ptr<DownloadWorker>> downloads_;
    std::mutex uploads_mutex_;
    std::unordered_map<std::string, std::shared_ptr<UploadWorker>> uploads_;
};
} // namespace spacestation::ssh
