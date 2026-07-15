#include "transfer/transfer_manager.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace spacestation::transfer
{
namespace
{
std::string DisplayPath(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return {};
    }
    const auto executable_root = ConfigStore::DefaultDataPath().parent_path().lexically_normal();
    const auto normalized = path.lexically_normal();
    auto root = executable_root.begin();
    auto current = normalized.begin();
    while (root != executable_root.end() && current != normalized.end() && *root == *current)
    {
        ++root;
        ++current;
    }
    if (root == executable_root.end())
    {
        std::error_code error;
        const auto relative = std::filesystem::relative(normalized, executable_root, error);
        if (!error && !relative.empty())
        {
            return relative.generic_string();
        }
    }
    return normalized.string();
}

nlohmann::json DisplayServerConfig(const ServerConfig& config)
{
    auto result = ToJson(config);
    result["certificatePath"] = DisplayPath(config.certificate_path);
    result["privateKeyPath"] = DisplayPath(config.private_key_path);
    result["clientCaPath"] = DisplayPath(config.client_ca_path);
    result["destinationRoot"] = DisplayPath(config.destination_root);
    auto roots = nlohmann::json::array();
    for (const auto& root : config.basis_roots)
    {
        roots.push_back(DisplayPath(root));
    }
    result["basisRoots"] = roots;
    return result;
}

std::string JobId()
{
    static std::atomic<std::uint64_t> sequence{0};
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    return std::to_string(now) + "-" + std::to_string(++sequence);
}

nlohmann::json ProgressJson(const FileProgress& progress)
{
    return {{"fileId", progress.file_id},
            {"path", progress.path},
            {"stage", progress.stage},
            {"fileSize", progress.file_size},
            {"matchedBytes", progress.matched_bytes},
            {"uploadedBytes", progress.uploaded_bytes},
            {"error", progress.error}};
}
} // namespace

TransferManager::TransferManager(ConfigStore& config_store) : config_store_(config_store)
{
    const auto saved = config_store_.LoadTransferConfig();
    if (!saved.empty())
    {
        server_config_ = ServerConfigFromJson(saved, ConfigStore::DefaultDataPath().parent_path());
    }
    else
    {
        const auto app = config_store_.Load();
        server_config_.certificate_path = app.certificate_path;
        server_config_.private_key_path = app.private_key_path;
        server_config_.client_ca_path = app.trusted_root_certificate_path;
        server_config_.destination_root = std::filesystem::path(app.data_path) / "transfer" / "incoming";
        server_config_.basis_roots = {std::filesystem::path(app.data_path) / "shared"};
    }
}

TransferManager::~TransferManager()
{
    if (server_)
    {
        server_->Stop();
    }
    for (auto& [id, job] : jobs_)
    {
        if (job->thread.joinable())
        {
            job->thread.join();
        }
    }
}

nlohmann::json TransferManager::JobJson(const Job& job)
{
    auto files = nlohmann::json::array();
    for (const auto& file : job.files)
    {
        files.push_back(ProgressJson(file));
    }
    return {{"id", job.id}, {"status", job.status}, {"error", job.error}, {"files", files}};
}

nlohmann::json TransferManager::State() const
{
    std::lock_guard lock(mutex_);
    auto jobs = nlohmann::json::array();
    for (const auto& [id, job] : jobs_)
    {
        jobs.push_back(JobJson(*job));
    }
    std::sort(jobs.begin(), jobs.end(), [](const auto& left, const auto& right) {
        return left.value("id", "") > right.value("id", "");
    });
    return {{"server", DisplayServerConfig(server_config_)},
            {"serverRunning", server_ && server_->Running()},
            {"jobs", jobs}};
}

void TransferManager::SaveStateUnlocked()
{
    config_store_.SaveTransferConfig(DisplayServerConfig(server_config_));
}

nlohmann::json TransferManager::UpdateServerConfig(const nlohmann::json& json)
{
    std::lock_guard lock(mutex_);
    if (server_ && server_->Running())
    {
        throw std::runtime_error("请先停止传输服务端再修改配置");
    }
    server_config_ = ServerConfigFromJson(json, ConfigStore::DefaultDataPath().parent_path());
    SaveStateUnlocked();
    return DisplayServerConfig(server_config_);
}

nlohmann::json TransferManager::StartServer()
{
    std::lock_guard lock(mutex_);
    if (server_ && server_->Running())
    {
        return {{"ok", true}, {"running", true}};
    }
    server_ = std::make_unique<Server>(server_config_);
    server_->Start();
    return {{"ok", true}, {"running", true}};
}

nlohmann::json TransferManager::StopServer()
{
    std::unique_ptr<Server> server;
    {
        std::lock_guard lock(mutex_);
        server = std::move(server_);
    }
    if (server)
    {
        server->Stop();
    }
    return {{"ok", true}, {"running", false}};
}

nlohmann::json TransferManager::StartClientJob(const nlohmann::json& json)
{
    auto config = ClientConfigFromJson(json, ConfigStore::DefaultDataPath().parent_path());
    auto job = std::make_unique<Job>();
    job->id = JobId();
    const auto id = job->id;
    auto* job_ptr = job.get();
    {
        std::lock_guard lock(mutex_);
        jobs_[id] = std::move(job);
    }
    job_ptr->thread = std::thread([this, job_ptr, config = std::move(config)]() mutable {
        {
            std::lock_guard lock(mutex_);
            job_ptr->status = "running";
        }
        try
        {
            Client client(std::move(config));
            const auto results = client.Send([this, job_ptr](const FileProgress& progress) {
                std::lock_guard lock(mutex_);
                const auto found = std::find_if(job_ptr->files.begin(), job_ptr->files.end(), [&](const auto& item) {
                    return item.file_id == progress.file_id;
                });
                if (found == job_ptr->files.end())
                {
                    job_ptr->files.push_back(progress);
                }
                else
                {
                    *found = progress;
                }
            });
            std::lock_guard lock(mutex_);
            job_ptr->files = results;
            const auto failed = std::find_if(results.begin(), results.end(), [](const auto& item) {
                return !item.error.empty();
            });
            job_ptr->status = failed == results.end() ? "completed" : "failed";
            if (failed != results.end())
            {
                job_ptr->error = failed->error;
            }
        }
        catch (const std::exception& error)
        {
            std::lock_guard lock(mutex_);
            job_ptr->status = "failed";
            job_ptr->error = error.what();
        }
    });
    return {{"ok", true}, {"jobId", id}};
}
} // namespace spacestation::transfer
