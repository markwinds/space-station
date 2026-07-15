#pragma once

#include "config/config_store.hpp"
#include "transfer/transfer.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace spacestation::transfer
{
class TransferManager
{
  public:
    explicit TransferManager(ConfigStore& config_store);
    ~TransferManager();

    nlohmann::json State() const;
    nlohmann::json UpdateServerConfig(const nlohmann::json& json);
    nlohmann::json StartServer();
    nlohmann::json StopServer();
    nlohmann::json StartClientJob(const nlohmann::json& json);

  private:
    struct Job
    {
        std::string id;
        std::string status = "queued";
        std::vector<FileProgress> files;
        std::string error;
        std::thread thread;
    };

    void SaveStateUnlocked();
    static nlohmann::json JobJson(const Job& job);

    ConfigStore& config_store_;
    mutable std::mutex mutex_;
    ServerConfig server_config_;
    std::unique_ptr<Server> server_;
    std::unordered_map<std::string, std::unique_ptr<Job>> jobs_;
};
} // namespace spacestation::transfer
