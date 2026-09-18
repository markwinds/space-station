#include "config/config_store.hpp"

#include <sqlite3.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>
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

inline constexpr std::string_view kSshCredentialVaultKey = "sshCredentials";
inline constexpr std::string_view kAuthenticatorVaultKey = "authenticatorEntries";

struct CipherContextDeleter
{
    void operator()(EVP_CIPHER_CTX* context) const
    {
        EVP_CIPHER_CTX_free(context);
    }
};

using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, CipherContextDeleter>;

std::string HexEncode(const unsigned char* data, std::size_t size)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string encoded(size * 2, '\0');
    for (std::size_t index = 0; index < size; ++index)
    {
        encoded[index * 2] = digits[data[index] >> 4];
        encoded[index * 2 + 1] = digits[data[index] & 0x0f];
    }
    return encoded;
}

std::vector<unsigned char> HexDecode(const std::string& value)
{
    if (value.size() % 2 != 0)
    {
        throw std::runtime_error("SSH 凭据密文格式无效。");
    }
    const auto nibble = [](char character) -> int {
        if (character >= '0' && character <= '9') return character - '0';
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        return -1;
    };
    std::vector<unsigned char> decoded(value.size() / 2);
    for (std::size_t index = 0; index < decoded.size(); ++index)
    {
        const auto high = nibble(value[index * 2]);
        const auto low = nibble(value[index * 2 + 1]);
        if (high < 0 || low < 0)
        {
            throw std::runtime_error("SSH 凭据密文格式无效。");
        }
        decoded[index] = static_cast<unsigned char>((high << 4) | low);
    }
    return decoded;
}

std::array<unsigned char, 32> LoadOrCreateVaultKey(const std::filesystem::path& path)
{
    std::array<unsigned char, 32> key{};
    if (std::filesystem::exists(path))
    {
        std::ifstream input(path, std::ios::binary);
        input.read(reinterpret_cast<char*>(key.data()), static_cast<std::streamsize>(key.size()));
        if (input.gcount() != static_cast<std::streamsize>(key.size()))
        {
            throw std::runtime_error("SSH 凭据库主密钥长度无效。");
        }
        return key;
    }
    std::filesystem::create_directories(path.parent_path());
    if (RAND_bytes(key.data(), static_cast<int>(key.size())) != 1)
    {
        throw std::runtime_error("SSH 凭据库主密钥生成失败。");
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(key.data()), static_cast<std::streamsize>(key.size()));
    output.close();
    if (!output)
    {
        OPENSSL_cleanse(key.data(), key.size());
        throw std::runtime_error("SSH 凭据库主密钥写入失败。");
    }
    std::error_code permission_error;
    std::filesystem::permissions(path,
                                 std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace,
                                 permission_error);
    return key;
}

nlohmann::json EncryptCredential(const std::filesystem::path& key_path,
                                 const std::string& host_id,
                                 const nlohmann::json& credential)
{
    auto key = LoadOrCreateVaultKey(key_path);
    std::array<unsigned char, 12> nonce{};
    std::array<unsigned char, 16> tag{};
    if (RAND_bytes(nonce.data(), static_cast<int>(nonce.size())) != 1)
    {
        OPENSSL_cleanse(key.data(), key.size());
        throw std::runtime_error("SSH 凭据加密随机数生成失败。");
    }
    const auto plaintext = credential.dump();
    std::vector<unsigned char> ciphertext(plaintext.size() + 16);
    CipherContext context(EVP_CIPHER_CTX_new());
    int written = 0;
    int total = 0;
    const auto ok = context && EVP_EncryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
                    EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) == 1 &&
                    EVP_EncryptInit_ex(context.get(), nullptr, nullptr, key.data(), nonce.data()) == 1 &&
                    EVP_EncryptUpdate(context.get(), nullptr, &written,
                                      reinterpret_cast<const unsigned char*>(host_id.data()), host_id.size()) == 1 &&
                    EVP_EncryptUpdate(context.get(), ciphertext.data(), &written,
                                      reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size()) == 1;
    total = written;
    const auto final_ok = ok && EVP_EncryptFinal_ex(context.get(), ciphertext.data() + total, &written) == 1;
    total += written;
    const auto tag_ok = final_ok && EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG, tag.size(), tag.data()) == 1;
    OPENSSL_cleanse(key.data(), key.size());
    if (!tag_ok)
    {
        throw std::runtime_error("SSH 凭据加密失败。");
    }
    ciphertext.resize(static_cast<std::size_t>(total));
    return {
        {"version", 1},
        {"nonce", HexEncode(nonce.data(), nonce.size())},
        {"ciphertext", HexEncode(ciphertext.data(), ciphertext.size())},
        {"tag", HexEncode(tag.data(), tag.size())},
    };
}

nlohmann::json DecryptCredential(const std::filesystem::path& key_path,
                                 const std::string& host_id,
                                 const nlohmann::json& envelope)
{
    if (!envelope.is_object() || envelope.value("version", 0) != 1)
    {
        throw std::runtime_error("SSH 凭据密文版本无效。");
    }
    auto nonce = HexDecode(envelope.value("nonce", ""));
    auto ciphertext = HexDecode(envelope.value("ciphertext", ""));
    auto tag = HexDecode(envelope.value("tag", ""));
    if (nonce.size() != 12 || tag.size() != 16)
    {
        throw std::runtime_error("SSH 凭据密文格式无效。");
    }
    auto key = LoadOrCreateVaultKey(key_path);
    std::vector<unsigned char> plaintext(ciphertext.size() + 1);
    CipherContext context(EVP_CIPHER_CTX_new());
    int written = 0;
    int total = 0;
    const auto ok = context && EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
                    EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) == 1 &&
                    EVP_DecryptInit_ex(context.get(), nullptr, nullptr, key.data(), nonce.data()) == 1 &&
                    EVP_DecryptUpdate(context.get(), nullptr, &written,
                                      reinterpret_cast<const unsigned char*>(host_id.data()), host_id.size()) == 1 &&
                    EVP_DecryptUpdate(context.get(), plaintext.data(), &written, ciphertext.data(), ciphertext.size()) == 1;
    total = written;
    const auto tag_ok = ok && EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG, tag.size(), tag.data()) == 1;
    const auto final_ok = tag_ok && EVP_DecryptFinal_ex(context.get(), plaintext.data() + total, &written) == 1;
    total += written;
    OPENSSL_cleanse(key.data(), key.size());
    if (!final_ok)
    {
        OPENSSL_cleanse(plaintext.data(), plaintext.size());
        throw std::runtime_error("SSH 凭据解密失败或密文已被修改。");
    }
    const auto parsed = nlohmann::json::parse(plaintext.begin(), plaintext.begin() + total, nullptr, false);
    OPENSSL_cleanse(plaintext.data(), plaintext.size());
    if (parsed.is_discarded() || !parsed.is_object())
    {
        throw std::runtime_error("SSH 凭据内容无效。");
    }
    return parsed;
}

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

std::string JsonString(const nlohmann::json& json, const char* key, const std::string& fallback = {})
{
    const auto it = json.find(key);
    return it != json.end() && it->is_string() ? it->get<std::string>() : fallback;
}

int JsonInt(const nlohmann::json& json, const char* key, int fallback)
{
    const auto it = json.find(key);
    return it != json.end() && it->is_number_integer() ? it->get<int>() : fallback;
}

bool JsonBool(const nlohmann::json& json, const char* key, bool fallback = false)
{
    const auto it = json.find(key);
    return it != json.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

std::vector<int> NormalizeWeekdays(const nlohmann::json& value)
{
    std::vector<int> weekdays;
    if (value.is_array())
    {
        for (const auto& item : value)
        {
            if (!item.is_number_integer())
            {
                continue;
            }
            const auto day = item.get<int>();
            if (day >= 0 && day <= 6 && std::find(weekdays.begin(), weekdays.end(), day) == weekdays.end())
            {
                weekdays.push_back(day);
            }
        }
    }
    std::sort(weekdays.begin(), weekdays.end());
    return weekdays;
}

nlohmann::json NormalizeHabit(const nlohmann::json& input)
{
    if (!input.is_object())
    {
        return nullptr;
    }
    const auto id = JsonString(input, "id");
    const auto title = JsonString(input, "title");
    if (id.empty() || title.empty())
    {
        return nullptr;
    }

    const auto kind = JsonString(input, "kind") == "reduce" ? std::string("reduce") : std::string("build");
    auto mode = std::string("weekdays");
    auto weekdays = std::vector<int>{1, 2, 3, 4, 5};
    auto target_per_week = 3;
    const auto schedule_it = input.find("schedule");
    if (schedule_it != input.end() && schedule_it->is_object())
    {
        mode = JsonString(*schedule_it, "mode") == "weeklyTarget" ? "weeklyTarget" : "weekdays";
        weekdays = NormalizeWeekdays(schedule_it->value("weekdays", nlohmann::json::array()));
        target_per_week = std::clamp(JsonInt(*schedule_it, "targetPerWeek", 3), 1, 7);
    }
    else
    {
        mode = JsonString(input, "schedule") == "weekly" ? "weeklyTarget" : "weekdays";
        weekdays = NormalizeWeekdays(input.value("weekdays", nlohmann::json::array()));
        target_per_week = std::clamp(JsonInt(input, "targetCount", 3), 1, 7);
    }
    if (weekdays.empty())
    {
        weekdays = {1, 2, 3, 4, 5};
    }
    if (kind == "reduce")
    {
        mode = "weekdays";
        weekdays = {0, 1, 2, 3, 4, 5, 6};
        target_per_week = 1;
    }

    return {
        {"id", id},
        {"title", title},
        {"kind", kind},
        {"color", JsonString(input, "color", "#4f7c66")},
        {"schedule",
         {
             {"mode", mode},
             {"weekdays", weekdays},
             {"targetPerWeek", target_per_week},
         }},
        {"alternative", JsonString(input, "alternative", JsonString(input, "replacementAction"))},
        {"archived", JsonBool(input, "archived")},
        {"archivedAt", JsonString(input, "archivedAt")},
        {"createdAt", JsonString(input, "createdAt")},
        {"updatedAt", JsonString(input, "updatedAt")},
    };
}

nlohmann::json NormalizeHabitState(const nlohmann::json& input)
{
    nlohmann::json state = {
        {"version", 2},
        {"habits", nlohmann::json::array()},
        {"logs", nlohmann::json::array()},
        {"settings", {{"weekStartsOn", 1}}},
    };
    if (!input.is_object())
    {
        return state;
    }

    std::unordered_set<std::string> habit_ids;
    const auto habits_it = input.find("habits");
    if (habits_it != input.end() && habits_it->is_array())
    {
        for (const auto& item : *habits_it)
        {
            auto habit = NormalizeHabit(item);
            if (!habit.is_object())
            {
                continue;
            }
            const auto id = habit["id"].get<std::string>();
            if (habit_ids.insert(id).second)
            {
                state["habits"].push_back(std::move(habit));
            }
        }
    }

    std::unordered_map<std::string, std::size_t> log_indices;
    const auto append_log = [&](const nlohmann::json& item, bool legacy) {
        if (!item.is_object())
        {
            return;
        }
        const auto habit_id = JsonString(item, "habitId");
        const auto date = JsonString(item, "date");
        if (!habit_ids.contains(habit_id) || date.size() != 10)
        {
            return;
        }

        auto completed = JsonBool(item, "completed");
        auto skipped = JsonBool(item, "skipped");
        auto occurrences = std::max(0, JsonInt(item, "occurrences", 0));
        auto replacements = std::max(0, JsonInt(item, "replacements", 0));
        auto confirmed = JsonBool(item, "confirmed");
        if (legacy)
        {
            const auto status = JsonString(item, "status");
            completed = status == "completed";
            skipped = status == "skipped";
            occurrences = status == "occurred" ? std::max(1, JsonInt(item, "count", 1)) : 0;
            replacements = status == "replaced" ? 1 : 0;
            confirmed = completed || skipped || occurrences > 0 || replacements > 0;
        }

        nlohmann::json log = {
            {"habitId", habit_id},
            {"date", date},
            {"completed", completed},
            {"skipped", completed ? false : skipped},
            {"occurrences", occurrences},
            {"replacements", replacements},
            {"confirmed", confirmed || occurrences > 0 || replacements > 0},
            {"note", JsonString(item, "note")},
            {"updatedAt", JsonString(item, "updatedAt", JsonString(item, "createdAt"))},
        };
        const auto key = habit_id + '\n' + date;
        const auto found = log_indices.find(key);
        if (found == log_indices.end())
        {
            log_indices.emplace(key, state["logs"].size());
            state["logs"].push_back(std::move(log));
        }
        else
        {
            state["logs"][found->second] = std::move(log);
        }
    };

    const auto logs_it = input.find("logs");
    if (logs_it != input.end() && logs_it->is_array())
    {
        for (const auto& item : *logs_it)
        {
            append_log(item, false);
        }
    }
    const auto records_it = input.find("records");
    if (records_it != input.end() && records_it->is_array())
    {
        for (const auto& item : *records_it)
        {
            append_log(item, true);
        }
    }

    const auto settings_it = input.find("settings");
    if (settings_it != input.end() && settings_it->is_object())
    {
        state["settings"]["weekStartsOn"] = JsonInt(*settings_it, "weekStartsOn", 1) == 0 ? 0 : 1;
    }
    return state;
}

nlohmann::json NormalizeCommandSnippet(const nlohmann::json& input)
{
    if (!input.is_object())
    {
        return nullptr;
    }
    const auto id = JsonString(input, "id");
    const auto name = JsonString(input, "name");
    const auto command = JsonString(input, "command");
    if (id.empty() || id.size() > 128 || name.empty() || name.size() > 120 || command.empty() || command.size() > 65536)
    {
        return nullptr;
    }
    return {
        {"id", id},
        {"name", name},
        {"command", command},
        {"action", JsonString(input, "action") == "insert" ? "insert" : "run"},
    };
}

nlohmann::json NormalizeCommandSnippets(const nlohmann::json& input)
{
    auto result = nlohmann::json::array();
    if (!input.is_array())
    {
        return result;
    }
    std::unordered_set<std::string> ids;
    for (const auto& item : input)
    {
        auto snippet = NormalizeCommandSnippet(item);
        if (!snippet.is_object())
        {
            continue;
        }
        const auto id = snippet["id"].get<std::string>();
        if (ids.insert(id).second)
        {
            result.push_back(std::move(snippet));
        }
    }
    return result;
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
    if (!state.contains("collapsedNodeIds") || !state["collapsedNodeIds"].is_array())
    {
        state["collapsedNodeIds"] = nlohmann::json::array();
    }
    if (!state.contains("settings") || !state["settings"].is_object())
    {
        state["settings"] = BuildDefaultTimeManagerStateJson()["settings"];
    }
    else
    {
        const auto defaults = BuildDefaultTimeManagerStateJson()["settings"];
        for (const auto& [key, value] : defaults.items())
        {
            if (!state["settings"].contains(key))
            {
                state["settings"][key] = value;
            }
        }
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
    if (!state["collapsedNodeIds"].is_array())
    {
        state["collapsedNodeIds"] = nlohmann::json::array();
    }
    if (!state["settings"].is_object())
    {
        state["settings"] = BuildDefaultTimeManagerStateJson()["settings"];
    }
    else
    {
        const auto defaults = BuildDefaultTimeManagerStateJson()["settings"];
        for (const auto& [key, value] : defaults.items())
        {
            if (!state["settings"].contains(key))
            {
                state["settings"][key] = value;
            }
        }
    }
    SaveBusinessJsonUnlocked("timeManagerState", state);
}

nlohmann::json ConfigStore::LoadHabitState()
{
    std::lock_guard lock(mutex_);
    return NormalizeHabitState(LoadBusinessJsonUnlocked("habitState", BuildDefaultHabitStateJson()));
}

void ConfigStore::SaveHabitState(const nlohmann::json& json)
{
    std::lock_guard lock(mutex_);
    SaveBusinessJsonUnlocked("habitState", NormalizeHabitState(json));
}

nlohmann::json ConfigStore::LoadCommandSnippets()
{
    std::lock_guard lock(mutex_);
    return NormalizeCommandSnippets(LoadBusinessJsonUnlocked("commandSnippets", nlohmann::json::array()));
}

nlohmann::json ConfigStore::SaveCommandSnippet(const nlohmann::json& json)
{
    auto snippet = NormalizeCommandSnippet(json);
    if (!snippet.is_object())
    {
        throw std::runtime_error("片段必须包含有效的 id、名称和命令。");
    }
    std::lock_guard lock(mutex_);
    auto snippets = NormalizeCommandSnippets(LoadBusinessJsonUnlocked("commandSnippets", nlohmann::json::array()));
    const auto id = snippet["id"].get<std::string>();
    const auto found = std::find_if(snippets.begin(), snippets.end(), [&id](const auto& item) {
        return item.value("id", "") == id;
    });
    if (found == snippets.end())
    {
        snippets.push_back(snippet);
    }
    else
    {
        *found = snippet;
    }
    SaveBusinessJsonUnlocked("commandSnippets", snippets);
    return snippet;
}

bool ConfigStore::DeleteCommandSnippet(const std::string& id)
{
    if (id.empty())
    {
        return false;
    }
    std::lock_guard lock(mutex_);
    auto snippets = NormalizeCommandSnippets(LoadBusinessJsonUnlocked("commandSnippets", nlohmann::json::array()));
    const auto original_size = snippets.size();
    snippets.erase(std::remove_if(snippets.begin(), snippets.end(), [&id](const auto& item) {
        return item.value("id", "") == id;
    }), snippets.end());
    if (snippets.size() == original_size)
    {
        return false;
    }
    SaveBusinessJsonUnlocked("commandSnippets", snippets);
    return true;
}

nlohmann::json ConfigStore::LoadTransferConfig()
{
    std::lock_guard lock(mutex_);
    const auto json = LoadJsonUnlocked();
    const auto found = json.find("transferServer");
    return found != json.end() && found->is_object() ? *found : nlohmann::json::object();
}

void ConfigStore::SaveTransferConfig(const nlohmann::json& transfer_config)
{
    std::lock_guard lock(mutex_);
    auto json = LoadJsonUnlocked();
    json["transferServer"] = transfer_config.is_object() ? transfer_config : nlohmann::json::object();
    SaveJsonUnlocked(json);
}

nlohmann::json ConfigStore::LoadSshHosts()
{
    std::lock_guard lock(mutex_);
    auto hosts = LoadBusinessJsonUnlocked("sshHosts", nlohmann::json::array());
    return hosts.is_array() ? hosts : nlohmann::json::array();
}

void ConfigStore::SaveSshHosts(const nlohmann::json& json)
{
    std::lock_guard lock(mutex_);
    auto hosts = nlohmann::json::array();
    std::unordered_set<std::string> ids;
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
            const auto host = item.value("host", "");
            const auto username = item.value("username", "");
            const auto protocol = item.value("protocol", "ssh") == "telnet" ? "telnet" : "ssh";
            const auto port = std::clamp(item.value("port", protocol == std::string("telnet") ? 23 : 22), 1, 65535);
            if (id.empty() || name.empty() || host.empty() ||
                (protocol == std::string("ssh") && username.empty()) || !ids.insert(id).second)
            {
                continue;
            }
            hosts.push_back({
                {"id", id},
                {"name", name},
                {"host", host},
                {"port", port},
                {"username", username},
                {"protocol", protocol},
                {"group", item.value("group", "")},
                {"hostKeySha256", item.value("hostKeySha256", "")},
                {"useAgent", item.value("useAgent", false)},
                {"jumpHostId", item.value("jumpHostId", "")},
                {"favorite", item.value("favorite", false)},
                {"lastUsedAt", item.value("lastUsedAt", "")},
            });
        }
    }
    for (const auto& host : hosts)
    {
        const auto id = host.value("id", "");
        const auto jump_host_id = host.value("jumpHostId", "");
        if (jump_host_id.empty()) continue;
        if (host.value("protocol", "ssh") != "ssh")
            throw std::runtime_error("Telnet 主机不能使用 SSH 跳板机。");
        if (jump_host_id == id) throw std::runtime_error("SSH 主机不能将自己设置为跳板机。");
        if (!ids.contains(jump_host_id)) throw std::runtime_error("SSH 主机引用的跳板机不存在。");
        const auto jump_host = std::find_if(hosts.begin(), hosts.end(), [&](const auto& candidate) {
            return candidate.value("id", "") == jump_host_id;
        });
        if (jump_host != hosts.end() && !jump_host->value("jumpHostId", "").empty())
            throw std::runtime_error("当前只支持单层跳板，跳板机自身不能再配置跳板机。");
        if (jump_host != hosts.end() && jump_host->value("protocol", "ssh") != "ssh")
            throw std::runtime_error("跳板机必须使用 SSH 协议。");
    }
    SaveBusinessJsonUnlocked("sshHosts", hosts);
    auto vault = LoadBusinessJsonUnlocked(std::string(kSshCredentialVaultKey), nlohmann::json::object());
    if (vault.is_object())
    {
        const auto previous_size = vault.size();
        for (auto item = vault.begin(); item != vault.end();)
        {
            item = ids.contains(item.key()) ? std::next(item) : vault.erase(item);
        }
        if (vault.size() != previous_size)
            SaveBusinessJsonUnlocked(std::string(kSshCredentialVaultKey), vault);
    }
}

bool ConfigStore::SaveSshHostFingerprint(const std::string& id, const std::string& fingerprint)
{
    std::lock_guard lock(mutex_);
    auto hosts = LoadBusinessJsonUnlocked("sshHosts", nlohmann::json::array());
    if (!hosts.is_array())
    {
        return false;
    }
    for (auto& host : hosts)
    {
        if (host.is_object() && host.value("id", "") == id)
        {
            host["hostKeySha256"] = fingerprint;
            SaveBusinessJsonUnlocked("sshHosts", hosts);
            return true;
        }
    }
    return false;
}

bool ConfigStore::HasSshCredential(const std::string& host_id)
{
    std::lock_guard lock(mutex_);
    const auto vault = LoadBusinessJsonUnlocked(std::string(kSshCredentialVaultKey), nlohmann::json::object());
    return vault.is_object() && vault.contains(host_id) && vault[host_id].is_object();
}

std::optional<nlohmann::json> ConfigStore::LoadSshCredential(const std::string& host_id)
{
    std::lock_guard lock(mutex_);
    const auto vault = LoadBusinessJsonUnlocked(std::string(kSshCredentialVaultKey), nlohmann::json::object());
    if (!vault.is_object() || !vault.contains(host_id) || !vault[host_id].is_object())
    {
        return std::nullopt;
    }
    const auto database_path = DatabasePathForConfigJson(LoadJsonUnlocked());
    return DecryptCredential(database_path.parent_path() / "ssh" / "vault.key", host_id, vault[host_id]);
}

void ConfigStore::SaveSshCredential(const std::string& host_id, const nlohmann::json& credential)
{
    if (host_id.empty() || !credential.is_object())
    {
        throw std::invalid_argument("SSH 凭据不能为空。");
    }
    const auto method = credential.value("method", "");
    const auto password = credential.value("password", "");
    const auto private_key = credential.value("privateKey", "");
    if ((method != "password" && method != "privateKey") ||
        (method == "password" && password.empty()) || (method == "privateKey" && private_key.empty()))
    {
        throw std::invalid_argument("SSH 凭据内容无效。");
    }
    std::lock_guard lock(mutex_);
    auto vault = LoadBusinessJsonUnlocked(std::string(kSshCredentialVaultKey), nlohmann::json::object());
    if (!vault.is_object())
    {
        vault = nlohmann::json::object();
    }
    const auto database_path = DatabasePathForConfigJson(LoadJsonUnlocked());
    vault[host_id] = EncryptCredential(database_path.parent_path() / "ssh" / "vault.key", host_id, {
        {"method", method},
        {"password", method == "password" ? password : ""},
        {"privateKey", method == "privateKey" ? private_key : ""},
        {"passphrase", credential.value("passphrase", "")},
    });
    SaveBusinessJsonUnlocked(std::string(kSshCredentialVaultKey), vault);
}

void ConfigStore::DeleteSshCredential(const std::string& host_id)
{
    std::lock_guard lock(mutex_);
    auto vault = LoadBusinessJsonUnlocked(std::string(kSshCredentialVaultKey), nlohmann::json::object());
    if (!vault.is_object() || vault.erase(host_id) == 0)
    {
        return;
    }
    SaveBusinessJsonUnlocked(std::string(kSshCredentialVaultKey), vault);
}

nlohmann::json ConfigStore::LoadAuthenticatorEntries()
{
    std::lock_guard lock(mutex_);
    const auto vault = LoadBusinessJsonUnlocked(std::string(kAuthenticatorVaultKey), nlohmann::json::object());
    auto entries = nlohmann::json::array();
    if (!vault.is_object())
    {
        return entries;
    }
    const auto database_path = DatabasePathForConfigJson(LoadJsonUnlocked());
    const auto key_path = database_path.parent_path() / "authenticator" / "vault.key";
    for (const auto& [id, envelope] : vault.items())
    {
        if (!envelope.is_object()) continue;
        auto entry = DecryptCredential(key_path, id, envelope);
        entry["id"] = id;
        entries.push_back(std::move(entry));
    }
    return entries;
}

void ConfigStore::SaveAuthenticatorEntry(const nlohmann::json& entry)
{
    if (!entry.is_object()) throw std::invalid_argument("认证器条目不能为空。");
    const auto id = entry.value("id", "");
    const auto name = entry.value("name", "");
    const auto secret = entry.value("secret", "");
    if (id.empty() || name.empty() || secret.empty()) throw std::invalid_argument("认证器条目内容不完整。");
    std::lock_guard lock(mutex_);
    auto vault = LoadBusinessJsonUnlocked(std::string(kAuthenticatorVaultKey), nlohmann::json::object());
    if (!vault.is_object()) vault = nlohmann::json::object();
    const auto database_path = DatabasePathForConfigJson(LoadJsonUnlocked());
    vault[id] = EncryptCredential(database_path.parent_path() / "authenticator" / "vault.key", id, entry);
    SaveBusinessJsonUnlocked(std::string(kAuthenticatorVaultKey), vault);
}

void ConfigStore::DeleteAuthenticatorEntry(const std::string& id)
{
    std::lock_guard lock(mutex_);
    auto vault = LoadBusinessJsonUnlocked(std::string(kAuthenticatorVaultKey), nlohmann::json::object());
    if (!vault.is_object() || vault.erase(id) == 0) return;
    SaveBusinessJsonUnlocked(std::string(kAuthenticatorVaultKey), vault);
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
        {"collapsedNodeIds", nlohmann::json::array()},
        {"settings",
         {
             {"calendarStartHour", 7},
             {"calendarEndHour", 22},
             {"openDetailsOnNodeClick", false},
         }},
    };
}

nlohmann::json ConfigStore::BuildDefaultHabitStateJson() const
{
    return {
        {"version", 2},
        {"habits", nlohmann::json::array()},
        {"logs", nlohmann::json::array()},
        {"settings", {{"weekStartsOn", 1}}},
    };
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
