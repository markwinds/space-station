#include "config/config_store.hpp"

#include <fstream>

namespace spacestation
{
namespace
{
int ClampPort(int port)
{
    if (port < 1 || port > 65535)
    {
        return 34827;
    }
    return port;
}

std::string NormalizeLogLevel(std::string level)
{
    if (level == "trace" || level == "debug" || level == "info" || level == "warn" || level == "error")
    {
        return level;
    }
    return "info";
}
} // namespace

ConfigStore::ConfigStore() : ConfigStore(DefaultConfigPath())
{
}

ConfigStore::ConfigStore(std::filesystem::path config_path) : config_path_(std::move(config_path))
{
}

AppConfig ConfigStore::Load()
{
    std::lock_guard lock(mutex_);
    const auto json = LoadJsonUnlocked();
    AppConfig config;
    config.data_path = json.value("dataPath", DefaultDataPath().string());
    config.log_level = NormalizeLogLevel(json.value("logLevel", "info"));
    config.port = ClampPort(json.value("port", 34827));
    return config;
}

void ConfigStore::SavePartial(const nlohmann::json& patch)
{
    if (!patch.is_object())
    {
        return;
    }

    std::lock_guard lock(mutex_);
    auto current = LoadJsonUnlocked();
    current.merge_patch(patch);
    current["logLevel"] = NormalizeLogLevel(current.value("logLevel", "info"));
    current["port"] = ClampPort(current.value("port", 34827));
    if (!current.contains("dataPath") || !current["dataPath"].is_string() || current["dataPath"].get<std::string>().empty())
    {
        current["dataPath"] = DefaultDataPath().string();
    }
    SaveJsonUnlocked(current);
}

nlohmann::json ConfigStore::ToJson(const AppConfig& config) const
{
    return {
        {"dataPath", config.data_path},
        {"logLevel", config.log_level},
        {"port", config.port},
        {"configPath", config_path_.string()},
        {"logPath", (std::filesystem::path(config.data_path) / "logs" / "space-station.log").string()},
    };
}

std::filesystem::path ConfigStore::DefaultDataPath()
{
    return std::filesystem::temp_directory_path() / "SpaceStationData";
}

std::filesystem::path ConfigStore::DefaultConfigPath()
{
    return DefaultDataPath() / "config.json";
}

nlohmann::json ConfigStore::LoadJsonUnlocked()
{
    std::filesystem::create_directories(config_path_.parent_path());
    std::ifstream input(config_path_);
    if (!input)
    {
        const auto defaults = BuildDefaultJson();
        SaveJsonUnlocked(defaults);
        return defaults;
    }

    nlohmann::json json = nlohmann::json::parse(input, nullptr, false);
    if (json.is_discarded() || !json.is_object())
    {
        json = BuildDefaultJson();
        SaveJsonUnlocked(json);
    }
    return json;
}

void ConfigStore::SaveJsonUnlocked(const nlohmann::json& json)
{
    std::filesystem::create_directories(config_path_.parent_path());
    std::ofstream output(config_path_, std::ios::trunc);
    output << json.dump(2);
}

nlohmann::json ConfigStore::BuildDefaultJson() const
{
    return {
        {"dataPath", DefaultDataPath().string()},
        {"logLevel", "info"},
        {"port", 34827},
    };
}
} // namespace spacestation
