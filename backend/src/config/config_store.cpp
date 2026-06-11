#include "config/config_store.hpp"

#include <array>
#include <fstream>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace spacestation
{
namespace
{
int ClampPort(int port)
{
    if (port < 1 || port > 65535)
    {
        return 443;
    }
    return port;
}

int ClampHttpPort(int port)
{
    if (port < 1 || port > 65535)
    {
        return 80;
    }
    return port;
}

std::string StringOrFallback(const nlohmann::json& json, const char* key, const std::filesystem::path& fallback)
{
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string() || it->get<std::string>().empty())
    {
        return fallback.string();
    }
    return it->get<std::string>();
}

std::string OptionalString(const nlohmann::json& json, const char* key)
{
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string())
    {
        return {};
    }
    return it->get<std::string>();
}

std::string NormalizeLogLevel(std::string level)
{
    if (level == "trace" || level == "debug" || level == "info" || level == "warn" || level == "error")
    {
        return level;
    }
    return "info";
}

std::filesystem::path ExecutableDirectory()
{
#ifdef __APPLE__
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0)
    {
        return std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str())).parent_path();
    }
#elif defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0)
    {
        buffer.resize(length);
        return std::filesystem::path(buffer).parent_path();
    }
#else
    std::array<char, 4096> buffer{};
    const auto length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length > 0)
    {
        return std::filesystem::path(std::string(buffer.data(), static_cast<std::size_t>(length))).parent_path();
    }
#endif
    return std::filesystem::current_path();
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
    config.port = ClampPort(json.value("port", 443));
    config.http_enabled = json.value("httpEnabled", false);
    config.http_port = ClampHttpPort(json.value("httpPort", 80));
    config.certificate_path = StringOrFallback(json, "certificatePath", DefaultCertificatePath());
    config.private_key_path = StringOrFallback(json, "privateKeyPath", DefaultPrivateKeyPath());
    config.trusted_root_certificate_path = OptionalString(json, "trustedRootCertificatePath");
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
    current["port"] = ClampPort(current.value("port", 443));
    current["httpEnabled"] = current.value("httpEnabled", false);
    current["httpPort"] = ClampHttpPort(current.value("httpPort", 80));
    current["certificatePath"] = StringOrFallback(current, "certificatePath", DefaultCertificatePath());
    current["privateKeyPath"] = StringOrFallback(current, "privateKeyPath", DefaultPrivateKeyPath());
    current["trustedRootCertificatePath"] = OptionalString(current, "trustedRootCertificatePath");
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
        {"httpEnabled", config.http_enabled},
        {"httpPort", config.http_port},
        {"certificatePath", config.certificate_path},
        {"privateKeyPath", config.private_key_path},
        {"trustedRootCertificatePath", config.trusted_root_certificate_path},
        {"configPath", config_path_.string()},
        {"logPath", (std::filesystem::path(config.data_path) / "logs" / "space-station.log").string()},
    };
}

nlohmann::json ConfigStore::LoadSchedulerState()
{
    std::lock_guard lock(mutex_);
    auto json = LoadJsonUnlocked();
    auto state = json.value("schedulerState", BuildDefaultSchedulerStateJson());
    if (!state.is_object())
    {
        state = BuildDefaultSchedulerStateJson();
    }
    if (!state.contains("tasks") || !state["tasks"].is_array())
    {
        state["tasks"] = nlohmann::json::array();
    }
    if (!state.contains("tags") || !state["tags"].is_array())
    {
        state["tags"] = nlohmann::json::array();
    }
    if (!state.contains("settings") || !state["settings"].is_object())
    {
        state["settings"] = BuildDefaultSchedulerStateJson()["settings"];
    }
    if (!state.contains("scenes") || !state["scenes"].is_array())
    {
        state["scenes"] = nlohmann::json::array();
    }
    return state;
}

void ConfigStore::SaveSchedulerState(const nlohmann::json& json)
{
    std::lock_guard lock(mutex_);
    auto current = LoadJsonUnlocked();
    auto state = BuildDefaultSchedulerStateJson();
    if (json.is_object())
    {
        state.merge_patch(json);
    }
    if (!state["tasks"].is_array())
    {
        state["tasks"] = nlohmann::json::array();
    }
    if (!state["tags"].is_array())
    {
        state["tags"] = nlohmann::json::array();
    }
    if (!state["settings"].is_object())
    {
        state["settings"] = BuildDefaultSchedulerStateJson()["settings"];
    }
    if (!state["scenes"].is_array())
    {
        state["scenes"] = nlohmann::json::array();
    }
    current["schedulerState"] = state;
    SaveJsonUnlocked(current);
}

nlohmann::json ConfigStore::LoadFileShares()
{
    std::lock_guard lock(mutex_);
    auto json = LoadJsonUnlocked();
    auto shares = json.value("fileShares", BuildDefaultFileSharesJson());
    if (!shares.is_array())
    {
        shares = BuildDefaultFileSharesJson();
    }
    return shares;
}

void ConfigStore::SaveFileShares(const nlohmann::json& json)
{
    std::lock_guard lock(mutex_);
    auto current = LoadJsonUnlocked();
    auto shares = nlohmann::json::array();
    if (json.is_array())
    {
        for (const auto& item : json)
        {
            if (!item.is_object())
            {
                continue;
            }
            const auto id = item.value("id", "");
            const auto name = item.value("name", "");
            const auto path = item.value("path", "");
            if (id.empty() || name.empty() || path.empty())
            {
                continue;
            }
            shares.push_back({
                {"id", id},
                {"name", name},
                {"path", path},
            });
        }
    }
    current["fileShares"] = shares;
    SaveJsonUnlocked(current);
}

std::filesystem::path ConfigStore::DefaultDataPath()
{
    return ExecutableDirectory() / "data";
}

std::filesystem::path ConfigStore::DefaultConfigPath()
{
    return DefaultDataPath() / "config.json";
}

std::filesystem::path ConfigStore::DefaultCertificatePath()
{
    return DefaultDataPath() / "tls" / "server.crt";
}

std::filesystem::path ConfigStore::DefaultPrivateKeyPath()
{
    return DefaultDataPath() / "tls" / "server.key";
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
        {"port", 443},
        {"httpEnabled", false},
        {"httpPort", 80},
        {"certificatePath", DefaultCertificatePath().string()},
        {"privateKeyPath", DefaultPrivateKeyPath().string()},
        {"trustedRootCertificatePath", ""},
        {"schedulerState", BuildDefaultSchedulerStateJson()},
        {"fileShares", BuildDefaultFileSharesJson()},
    };
}

nlohmann::json ConfigStore::BuildDefaultSchedulerStateJson() const
{
    return {
        {"tasks", nlohmann::json::array()},
        {"tags", nlohmann::json::array()},
        {"scenes", nlohmann::json::array()},
        {"settings",
         {
             {"horizonDays", 14},
             {"overdueDays", 3},
             {"dayStartHour", 8},
             {"dayEndHour", 22},
         }},
    };
}

nlohmann::json ConfigStore::BuildDefaultFileSharesJson() const
{
    return nlohmann::json::array({
        {
            {"id", "default"},
            {"name", "默认共享"},
            {"path", (DefaultDataPath() / "shared").string()},
        },
    });
}
} // namespace spacestation
