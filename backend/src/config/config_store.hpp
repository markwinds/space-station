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
    int port = 34827;
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

  private:
    nlohmann::json LoadJsonUnlocked();
    void SaveJsonUnlocked(const nlohmann::json& json);
    nlohmann::json BuildDefaultJson() const;

    std::filesystem::path config_path_;
    mutable std::mutex mutex_;
};
} // namespace spacestation
