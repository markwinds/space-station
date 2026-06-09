#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <mutex>
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
};

class ConfigStore
{
  public:
    ConfigStore();
    explicit ConfigStore(std::filesystem::path config_path);

    AppConfig Load();
    void SavePartial(const nlohmann::json& patch);
    nlohmann::json ToJson(const AppConfig& config) const;

    static std::filesystem::path DefaultDataPath();
    static std::filesystem::path DefaultConfigPath();
    static std::filesystem::path DefaultCertificatePath();
    static std::filesystem::path DefaultPrivateKeyPath();

  private:
    nlohmann::json LoadJsonUnlocked();
    void SaveJsonUnlocked(const nlohmann::json& json);
    nlohmann::json BuildDefaultJson() const;

    std::filesystem::path config_path_;
    mutable std::mutex mutex_;
};
} // namespace spacestation
