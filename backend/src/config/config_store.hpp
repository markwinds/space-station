#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

namespace spacestation
{
struct AppConfig
{
    std::string data_path;
    std::string log_level = "info";
    int port = 443;
    bool http_enabled = false;
    int http_port = 80;
    std::string certificate_path;
    std::string private_key_path;
    std::string trusted_root_certificate_path;
};

class ConfigStore
{
  public:
    ConfigStore();
    explicit ConfigStore(std::filesystem::path config_path);

    AppConfig Load();
    void SavePartial(const nlohmann::json& patch);
    nlohmann::json ToJson(const AppConfig& config) const;
    nlohmann::json LoadTimeManagerState();
    void SaveTimeManagerState(const nlohmann::json& json);
    nlohmann::json LoadHabitState();
    void SaveHabitState(const nlohmann::json& json);
    nlohmann::json LoadFileShares();
    void SaveFileShares(const nlohmann::json& json);
    nlohmann::json LoadSshHosts();
    void SaveSshHosts(const nlohmann::json& json);
    bool SaveSshHostFingerprint(const std::string& id, const std::string& fingerprint);
    bool HasSshCredential(const std::string& host_id);
    std::optional<nlohmann::json> LoadSshCredential(const std::string& host_id);
    void SaveSshCredential(const std::string& host_id, const nlohmann::json& credential);
    void DeleteSshCredential(const std::string& host_id);
    nlohmann::json LoadTransferConfig();
    void SaveTransferConfig(const nlohmann::json& json);

    static std::filesystem::path DefaultDataPath();
    static std::filesystem::path DefaultConfigPath();
    static std::filesystem::path DefaultCertificatePath();
    static std::filesystem::path DefaultPrivateKeyPath();

  private:
    nlohmann::json LoadJsonUnlocked();
    void SaveJsonUnlocked(const nlohmann::json& json);
    nlohmann::json BuildDefaultJson() const;
    nlohmann::json BuildDefaultTimeManagerStateJson() const;
    nlohmann::json BuildDefaultHabitStateJson() const;
    nlohmann::json BuildDefaultFileSharesJson() const;
    std::filesystem::path DatabasePathForConfigJson(const nlohmann::json& json) const;
    nlohmann::json LoadBusinessJsonUnlocked(const std::string& key, const nlohmann::json& fallback);
    void SaveBusinessJsonUnlocked(const std::string& key, const nlohmann::json& json);

    std::filesystem::path config_path_;
    mutable std::mutex mutex_;
};
} // namespace spacestation
