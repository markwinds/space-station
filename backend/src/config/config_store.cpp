#include "config/config_store.hpp"

#include <sqlite3.h>

#include <array>
#include <fstream>
#include <memory>
#include <stdexcept>
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
std::filesystem::path ExecutableDirectory();

struct SqliteDeleter
{
    void operator()(sqlite3* db) const
    {
        if (db)
        {
            sqlite3_close(db);
        }
    }
};

using SqliteDb = std::unique_ptr<sqlite3, SqliteDeleter>;

struct StatementDeleter
{
    void operator()(sqlite3_stmt* statement) const
    {
        if (statement)
        {
            sqlite3_finalize(statement);
        }
    }
};

using SqliteStatement = std::unique_ptr<sqlite3_stmt, StatementDeleter>;

SqliteDb OpenDatabase(const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());
    sqlite3* db = nullptr;
    if (sqlite3_open(path.string().c_str(), &db) != SQLITE_OK)
    {
        std::string message = db ? sqlite3_errmsg(db) : "unknown sqlite error";
        sqlite3_close(db);
        throw std::runtime_error("SQLite 数据库打开失败: " + message);
    }
    return SqliteDb(db);
}

void ExecuteSql(sqlite3* db, const char* sql)
{
    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK)
    {
        std::string message = error ? error : "unknown sqlite error";
        sqlite3_free(error);
        throw std::runtime_error("SQLite 执行失败: " + message);
    }
}

void EnsureSchema(sqlite3* db)
{
    ExecuteSql(db,
               "CREATE TABLE IF NOT EXISTS app_state ("
               "key TEXT PRIMARY KEY,"
               "value TEXT NOT NULL,"
               "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
               ")");
}

SqliteStatement PrepareStatement(sqlite3* db, const char* sql)
{
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        throw std::runtime_error(std::string("SQLite 语句准备失败: ") + sqlite3_errmsg(db));
    }
    return SqliteStatement(statement);
}

nlohmann::json LoadJsonFromDatabase(const std::filesystem::path& path,
                                    const std::string& key,
                                    const nlohmann::json& fallback)
{
    const auto db = OpenDatabase(path);
    EnsureSchema(db.get());

    const auto statement = PrepareStatement(db.get(), "SELECT value FROM app_state WHERE key = ?");
    sqlite3_bind_text(statement.get(), 1, key.c_str(), -1, SQLITE_TRANSIENT);
    const auto rc = sqlite3_step(statement.get());
    if (rc == SQLITE_DONE)
    {
        return fallback;
    }
    if (rc != SQLITE_ROW)
    {
        throw std::runtime_error(std::string("SQLite 读取失败: ") + sqlite3_errmsg(db.get()));
    }

    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 0));
    if (!text)
    {
        return fallback;
    }
    const auto parsed = nlohmann::json::parse(text, nullptr, false);
    return parsed.is_discarded() ? fallback : parsed;
}

void SaveJsonToDatabase(const std::filesystem::path& path, const std::string& key, const nlohmann::json& json)
{
    const auto db = OpenDatabase(path);
    EnsureSchema(db.get());

    const auto statement = PrepareStatement(
        db.get(),
        "INSERT INTO app_state (key, value, updated_at) VALUES (?, ?, CURRENT_TIMESTAMP) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value, updated_at = CURRENT_TIMESTAMP");
    const auto value = json.dump();
    sqlite3_bind_text(statement.get(), 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement.get(), 2, value.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(statement.get()) != SQLITE_DONE)
    {
        throw std::runtime_error(std::string("SQLite 写入失败: ") + sqlite3_errmsg(db.get()));
    }
}

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

std::filesystem::path RelativeDefaultDataPath()
{
    return "data";
}

std::filesystem::path RelativeDefaultCertificatePath()
{
    return RelativeDefaultDataPath() / "tls" / "server.crt";
}

std::filesystem::path RelativeDefaultPrivateKeyPath()
{
    return RelativeDefaultDataPath() / "tls" / "server.key";
}

std::filesystem::path ResolveExecutableRelativePath(const std::string& path)
{
    const auto parsed = std::filesystem::path(path);
    if (parsed.is_absolute())
    {
        return parsed.lexically_normal();
    }
    return (ExecutableDirectory() / parsed).lexically_normal();
}

std::string ResolvePathString(const std::string& path)
{
    if (path.empty())
    {
        return {};
    }
    return ResolveExecutableRelativePath(path).string();
}

bool IsPathInside(const std::filesystem::path& root, const std::filesystem::path& path)
{
    const auto normalized_root = root.lexically_normal();
    const auto normalized_path = path.lexically_normal();
    auto root_it = normalized_root.begin();
    auto path_it = normalized_path.begin();
    for (; root_it != normalized_root.end(); ++root_it, ++path_it)
    {
        if (path_it == normalized_path.end() || *root_it != *path_it)
        {
            return false;
        }
    }
    return true;
}

std::string DisplayPath(const std::filesystem::path& path)
{
    const auto root = ExecutableDirectory().lexically_normal();
    const auto normalized = path.lexically_normal();
    if (IsPathInside(root, normalized))
    {
        std::error_code ec;
        const auto relative = std::filesystem::relative(normalized, root, ec);
        if (!ec && !relative.empty() && relative != ".")
        {
            return relative.generic_string();
        }
    }
    return normalized.string();
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
    config.data_path = ResolvePathString(json.value("dataPath", RelativeDefaultDataPath().string()));
    config.log_level = NormalizeLogLevel(json.value("logLevel", "info"));
    config.port = ClampPort(json.value("port", 443));
    config.http_enabled = json.value("httpEnabled", false);
    config.http_port = ClampHttpPort(json.value("httpPort", 80));
    config.certificate_path = ResolvePathString(StringOrFallback(json, "certificatePath", RelativeDefaultCertificatePath()));
    config.private_key_path = ResolvePathString(StringOrFallback(json, "privateKeyPath", RelativeDefaultPrivateKeyPath()));
    config.trusted_root_certificate_path = ResolvePathString(OptionalString(json, "trustedRootCertificatePath"));
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
    current["certificatePath"] = StringOrFallback(current, "certificatePath", RelativeDefaultCertificatePath());
    current["privateKeyPath"] = StringOrFallback(current, "privateKeyPath", RelativeDefaultPrivateKeyPath());
    current["trustedRootCertificatePath"] = OptionalString(current, "trustedRootCertificatePath");
    if (!current.contains("dataPath") || !current["dataPath"].is_string() || current["dataPath"].get<std::string>().empty())
    {
        current["dataPath"] = RelativeDefaultDataPath().string();
    }
    SaveJsonUnlocked(current);
}

nlohmann::json ConfigStore::ToJson(const AppConfig& config) const
{
    return {
        {"dataPath", DisplayPath(config.data_path)},
        {"logLevel", config.log_level},
        {"port", config.port},
        {"httpEnabled", config.http_enabled},
        {"httpPort", config.http_port},
        {"certificatePath", DisplayPath(config.certificate_path)},
        {"privateKeyPath", DisplayPath(config.private_key_path)},
        {"trustedRootCertificatePath",
         config.trusted_root_certificate_path.empty() ? "" : DisplayPath(config.trusted_root_certificate_path)},
        {"configPath", config_path_.string()},
        {"logPath", DisplayPath(std::filesystem::path(config.data_path) / "logs" / "space-station.log")},
    };
}

nlohmann::json ConfigStore::LoadSchedulerState()
{
    std::lock_guard lock(mutex_);
    auto state = LoadBusinessJsonUnlocked("schedulerState", BuildDefaultSchedulerStateJson());
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
    SaveBusinessJsonUnlocked("schedulerState", state);
}

nlohmann::json ConfigStore::LoadTimeManagerState()
{
    std::lock_guard lock(mutex_);
    auto state = LoadBusinessJsonUnlocked("timeManagerState", BuildDefaultTimeManagerStateJson());
    if (!state.is_object())
    {
        state = BuildDefaultTimeManagerStateJson();
    }
    if (!state.contains("tasks") || !state["tasks"].is_array())
    {
        state["tasks"] = nlohmann::json::array();
    }
    if (!state.contains("tags") || !state["tags"].is_array())
    {
        state["tags"] = nlohmann::json::array();
    }
    if (!state.contains("filters") || !state["filters"].is_array())
    {
        state["filters"] = nlohmann::json::array();
    }
    if (!state.contains("settings") || !state["settings"].is_object())
    {
        state["settings"] = BuildDefaultTimeManagerStateJson()["settings"];
    }
    return state;
}

void ConfigStore::SaveTimeManagerState(const nlohmann::json& json)
{
    std::lock_guard lock(mutex_);
    auto state = BuildDefaultTimeManagerStateJson();
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
    if (!state["filters"].is_array())
    {
        state["filters"] = nlohmann::json::array();
    }
    if (!state["settings"].is_object())
    {
        state["settings"] = BuildDefaultTimeManagerStateJson()["settings"];
    }
    SaveBusinessJsonUnlocked("timeManagerState", state);
}

nlohmann::json ConfigStore::LoadFileShares()
{
    std::lock_guard lock(mutex_);
    auto shares = LoadBusinessJsonUnlocked("fileShares", BuildDefaultFileSharesJson());
    if (!shares.is_array())
    {
        shares = BuildDefaultFileSharesJson();
    }
    return shares;
}

void ConfigStore::SaveFileShares(const nlohmann::json& json)
{
    std::lock_guard lock(mutex_);
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
    SaveBusinessJsonUnlocked("fileShares", shares);
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
        {"dataPath", RelativeDefaultDataPath().generic_string()},
        {"logLevel", "info"},
        {"port", 443},
        {"httpEnabled", false},
        {"httpPort", 80},
        {"certificatePath", RelativeDefaultCertificatePath().generic_string()},
        {"privateKeyPath", RelativeDefaultPrivateKeyPath().generic_string()},
        {"trustedRootCertificatePath", ""},
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

nlohmann::json ConfigStore::BuildDefaultTimeManagerStateJson() const
{
    return {
        {"tasks", nlohmann::json::array()},
        {"tags",
         nlohmann::json::array({
             {{"id", "focus"}, {"name", "专注"}, {"color", "#2563eb"}},
             {{"id", "life"}, {"name", "生活"}, {"color", "#16a34a"}},
         })},
        {"filters", nlohmann::json::array()},
        {"settings",
         {
             {"calendarStartHour", 7},
             {"calendarEndHour", 22},
         }},
    };
}

nlohmann::json ConfigStore::BuildDefaultFileSharesJson() const
{
    return nlohmann::json::array({
        {
            {"id", "default"},
            {"name", "默认共享"},
            {"path", (RelativeDefaultDataPath() / "shared").generic_string()},
        },
    });
}

std::filesystem::path ConfigStore::DatabasePathForConfigJson(const nlohmann::json& json) const
{
    const auto data_path = ResolveExecutableRelativePath(json.value("dataPath", RelativeDefaultDataPath().string()));
    return data_path / "space-station.db";
}

nlohmann::json ConfigStore::LoadBusinessJsonUnlocked(const std::string& key, const nlohmann::json& fallback)
{
    const auto config_json = LoadJsonUnlocked();
    return LoadJsonFromDatabase(DatabasePathForConfigJson(config_json), key, fallback);
}

void ConfigStore::SaveBusinessJsonUnlocked(const std::string& key, const nlohmann::json& json)
{
    const auto config_json = LoadJsonUnlocked();
    SaveJsonToDatabase(DatabasePathForConfigJson(config_json), key, json);
}
} // namespace spacestation
