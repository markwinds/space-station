#include "plugins/terminal_plugin_service.hpp"

#include "logging/logger.hpp"

#include <curl/curl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <quickjs.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <future>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace spacestation::plugins
{
namespace
{
using namespace std::chrono_literals;

constexpr std::size_t kMaximumPluginSourceBytes = 1024 * 1024;
constexpr std::size_t kMaximumRequestBodyBytes = 256 * 1024;
constexpr std::size_t kMaximumHeaderCount = 64;

struct SessionContext
{
    std::string id;
    std::string transport;
    std::string target;
};

struct SessionEntry : SessionContext
{
    TerminalPluginService::WriteCallback write;
};

struct PluginManifest
{
    std::string id;
    std::string name;
    std::filesystem::path entry;
    std::vector<std::string> transports{"serial"};
    std::vector<std::string> targets{"*"};
    std::vector<std::string> allowed_hosts;
    std::size_t memory_limit_bytes = 8 * 1024 * 1024;
    std::chrono::milliseconds execution_timeout{50};
    std::size_t max_concurrent_requests = 4;
    std::size_t max_response_bytes = 1024 * 1024;
};

struct HttpRequestSpec
{
    std::string url;
    std::string host;
    std::string method = "GET";
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::string proxy;
    bool verify_tls = true;
    std::chrono::milliseconds timeout{5000};
    std::size_t max_response_bytes = 1024 * 1024;
};

struct HttpResponseData
{
    int status = 0;
    std::unordered_map<std::string, std::string> headers;
    std::unordered_map<std::string, std::vector<std::string>> header_values;
    std::vector<std::string> set_cookies;
    std::string body;
    std::string effective_url;
};

struct PluginEvent
{
    enum class Type
    {
        Open,
        Output,
        Close,
        HttpComplete,
        Reload,
    };

    Type type = Type::Output;
    SessionContext session;
    std::string data;
    std::string plugin_id;
    std::string request_id;
    HttpResponseData response;
    std::string error;
    std::shared_ptr<std::promise<void>> completion;
};

struct EventQueue
{
    std::mutex mutex;
    std::condition_variable condition;
    std::deque<PluginEvent> events;
    bool accepting = true;
};

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool ValidPluginId(const std::string& value)
{
    if (value.empty() || value.size() > 80) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isalnum(character) || character == '-' || character == '_' || character == '.';
    });
}

std::string ReadTextFile(const std::filesystem::path& path, std::size_t maximum_size)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > maximum_size)
        throw std::runtime_error("文件不存在或超过允许大小: " + path.string());
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("无法读取文件: " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<std::string> StringArray(const nlohmann::json& value,
                                     std::vector<std::string> fallback = {})
{
    if (!value.is_array()) return fallback;
    std::vector<std::string> result;
    for (const auto& item : value)
    {
        if (item.is_string() && !item.get_ref<const std::string&>().empty())
            result.push_back(item.get<std::string>());
    }
    return result.empty() ? fallback : result;
}

bool IsRelativeChildPath(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute()) return false;
    for (const auto& part : path.lexically_normal())
        if (part == "..") return false;
    return true;
}

PluginManifest ParseManifest(const std::filesystem::path& directory,
                             const nlohmann::json& json)
{
    if (!json.is_object()) throw std::runtime_error("manifest.json 必须是对象。");
    PluginManifest manifest;
    manifest.id = json.value("id", "");
    if (!ValidPluginId(manifest.id)) throw std::runtime_error("插件 id 无效。");
    manifest.name = json.value("name", manifest.id);
    if (manifest.name.empty() || manifest.name.size() > 120)
        throw std::runtime_error("插件名称不能为空且不能超过 120 个字符。");
    const auto entry = std::filesystem::path(json.value("entry", "index.js")).lexically_normal();
    if (!IsRelativeChildPath(entry)) throw std::runtime_error("插件入口必须位于插件目录内。");
    manifest.entry = directory / entry;
    manifest.transports = StringArray(json.value("transports", nlohmann::json::array()), {"serial"});
    manifest.targets = StringArray(json.value("targets", nlohmann::json::array()), {"*"});
    manifest.allowed_hosts = StringArray(json.value("allowedHosts", nlohmann::json::array()));
    for (auto& host : manifest.allowed_hosts) host = Lower(host);
    const auto memory_mib = std::clamp(json.value("memoryLimitMiB", 8), 1, 64);
    manifest.memory_limit_bytes = static_cast<std::size_t>(memory_mib) * 1024 * 1024;
    manifest.execution_timeout = std::chrono::milliseconds(
        std::clamp(json.value("executionTimeoutMs", 50), 5, 1000));
    manifest.max_concurrent_requests = static_cast<std::size_t>(
        std::clamp(json.value("maxConcurrentRequests", 4), 1, 16));
    manifest.max_response_bytes = static_cast<std::size_t>(
        std::clamp(json.value("maxResponseBytes", 1024 * 1024), 1024, 4 * 1024 * 1024));
    return manifest;
}

bool Matches(const std::vector<std::string>& patterns, const std::string& value)
{
    return std::any_of(patterns.begin(), patterns.end(), [&](const auto& pattern) {
        return pattern == "*" || pattern == value;
    });
}

bool HostAllowed(const std::vector<std::string>& patterns, const std::string& host)
{
    for (const auto& pattern : patterns)
    {
        if (pattern == host) return true;
        if (pattern.size() > 2 && pattern.starts_with("*.") && host.size() > pattern.size() - 1)
        {
            const auto suffix = std::string_view(pattern).substr(1);
            if (std::string_view(host).ends_with(suffix)) return true;
        }
    }
    return false;
}

struct ParsedUrl
{
    std::string host;
};

ParsedUrl ParseUrl(const std::string& url)
{
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) throw std::runtime_error("HTTP URL 缺少协议。");
    const auto scheme = Lower(url.substr(0, scheme_end));
    if (scheme != "http" && scheme != "https") throw std::runtime_error("只允许 HTTP 或 HTTPS URL。");
    const auto authority_start = scheme_end + 3;
    const auto path_start = url.find_first_of("/?#", authority_start);
    const auto authority = url.substr(authority_start, path_start - authority_start);
    if (authority.empty() || authority.find('@') != std::string::npos)
        throw std::runtime_error("HTTP URL 主机无效。");

    std::string host;
    std::string port;
    if (authority.front() == '[')
    {
        const auto bracket = authority.find(']');
        if (bracket == std::string::npos) throw std::runtime_error("HTTP URL IPv6 主机无效。");
        host = authority.substr(1, bracket - 1);
        if (bracket + 1 < authority.size())
        {
            if (authority[bracket + 1] != ':') throw std::runtime_error("HTTP URL 端口无效。");
            port = authority.substr(bracket + 2);
        }
    }
    else
    {
        const auto colon = authority.rfind(':');
        host = colon == std::string::npos ? authority : authority.substr(0, colon);
        if (colon != std::string::npos)
        {
            port = authority.substr(colon + 1);
        }
    }
    host = Lower(host);
    if (host.empty()) throw std::runtime_error("HTTP URL 主机无效。");
    if (!port.empty())
    {
        if (port.size() > 5 || !std::all_of(port.begin(), port.end(), [](unsigned char character) {
                return std::isdigit(character);
            }))
            throw std::runtime_error("HTTP URL 端口无效。");
        const auto value = std::stoi(port);
        if (value < 1 || value > 65535) throw std::runtime_error("HTTP URL 端口超出范围。");
    }
    else if (authority.ends_with(':'))
    {
        throw std::runtime_error("HTTP URL 端口无效。");
    }

    return {std::move(host)};
}

bool UnsafeHeader(const std::string& name)
{
    const auto lower = Lower(name);
    return lower == "host" || lower == "content-length" || lower == "connection" ||
           lower == "transfer-encoding" || lower.starts_with("proxy-");
}

bool InvalidHeaderText(const std::string& name, const std::string& value)
{
    if (name.empty() || name.find(':') != std::string::npos ||
        name.find_first_of("\r\n") != std::string::npos ||
        value.find_first_of("\r\n") != std::string::npos)
        return true;
    return std::any_of(name.begin(), name.end(), [](unsigned char character) {
        return character <= 0x20 || character == 0x7f;
    });
}

void ValidateHttpMethod(const std::string& method)
{
    if (method == "GET" || method == "POST" || method == "PUT" || method == "PATCH" ||
        method == "DELETE" || method == "HEAD")
        return;
    throw std::runtime_error("不支持的 HTTP 方法: " + method);
}

std::optional<std::string> JsString(JSContext* context, JSValueConst value)
{
    if (JS_IsUndefined(value) || JS_IsNull(value)) return std::nullopt;
    std::size_t size = 0;
    const auto* text = JS_ToCStringLen(context, &size, value);
    if (!text) return std::nullopt;
    std::string result(text, size);
    JS_FreeCString(context, text);
    return result;
}

std::optional<std::string> JsStringProperty(JSContext* context,
                                            JSValueConst object,
                                            const char* name)
{
    auto value = JS_GetPropertyStr(context, object, name);
    if (JS_IsException(value)) return std::nullopt;
    auto result = JsString(context, value);
    JS_FreeValue(context, value);
    return result;
}

int JsIntegerProperty(JSContext* context, JSValueConst object, const char* name, int fallback)
{
    auto value = JS_GetPropertyStr(context, object, name);
    if (JS_IsUndefined(value) || JS_IsNull(value))
    {
        JS_FreeValue(context, value);
        return fallback;
    }
    std::int32_t result = fallback;
    if (JS_ToInt32(context, &result, value) < 0) result = fallback;
    JS_FreeValue(context, value);
    return result;
}

bool JsBooleanProperty(JSContext* context, JSValueConst object, const char* name, bool fallback)
{
    auto value = JS_GetPropertyStr(context, object, name);
    if (JS_IsUndefined(value) || JS_IsNull(value))
    {
        JS_FreeValue(context, value);
        return fallback;
    }
    const auto result = JS_ToBool(context, value);
    JS_FreeValue(context, value);
    return result < 0 ? fallback : result != 0;
}

std::string OpenSslError()
{
    const auto code = ERR_get_error();
    if (code == 0) return "未知 OpenSSL 错误";
    char buffer[256]{};
    ERR_error_string_n(code, buffer, sizeof(buffer));
    return buffer;
}

std::string Base64Encode(const unsigned char* data, std::size_t size)
{
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("待编码数据过大。");
    std::string encoded(4 * ((size + 2) / 3), '\0');
    const auto written = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(encoded.data()),
                                         data,
                                         static_cast<int>(size));
    if (written < 0) throw std::runtime_error("Base64 编码失败。");
    encoded.resize(static_cast<std::size_t>(written));
    return encoded;
}

std::string TrimHttpValue(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

struct CurlResponseContext
{
    HttpResponseData response;
    std::size_t maximum_body_bytes = 0;
    bool body_too_large = false;
};

std::size_t CurlWrite(char* data, std::size_t size, std::size_t count, void* opaque)
{
    auto* context = static_cast<CurlResponseContext*>(opaque);
    const auto bytes = size * count;
    if (bytes > context->maximum_body_bytes -
                    std::min(context->maximum_body_bytes, context->response.body.size()))
    {
        context->body_too_large = true;
        return 0;
    }
    context->response.body.append(data, bytes);
    return bytes;
}

std::size_t CurlHeader(char* data, std::size_t size, std::size_t count, void* opaque)
{
    auto* context = static_cast<CurlResponseContext*>(opaque);
    const auto bytes = size * count;
    std::string line(data, bytes);
    if (line.starts_with("HTTP/"))
    {
        context->response.headers.clear();
        context->response.header_values.clear();
        context->response.set_cookies.clear();
        return bytes;
    }
    const auto colon = line.find(':');
    if (colon == std::string::npos) return bytes;
    auto name = Lower(TrimHttpValue(line.substr(0, colon)));
    auto value = TrimHttpValue(line.substr(colon + 1));
    if (name.empty()) return bytes;
    context->response.header_values[name].push_back(value);
    if (name == "set-cookie") context->response.set_cookies.push_back(value);
    const auto found = context->response.headers.find(name);
    if (found == context->response.headers.end()) context->response.headers.emplace(name, value);
    else if (name != "set-cookie") found->second += ", " + value;
    else found->second = value;
    return bytes;
}

HttpResponseData PerformHttpRequest(const HttpRequestSpec& spec)
{
    static const auto curl_initialized = [] {
        const auto result = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (result != CURLE_OK) throw std::runtime_error("libcurl 全局初始化失败。");
        return true;
    }();
    (void)curl_initialized;

    auto* handle = curl_easy_init();
    if (!handle) throw std::runtime_error("无法创建 libcurl 请求。");
    struct HandleGuard
    {
        CURL* value;
        ~HandleGuard() { curl_easy_cleanup(value); }
    } handle_guard{handle};

    curl_slist* request_headers = nullptr;
    struct HeaderGuard
    {
        curl_slist*& value;
        ~HeaderGuard() { curl_slist_free_all(value); }
    } header_guard{request_headers};
    for (const auto& [name, value] : spec.headers)
    {
        const auto header = name + ": " + value;
        auto* appended = curl_slist_append(request_headers, header.c_str());
        if (!appended) throw std::runtime_error("无法分配 HTTP 请求头。");
        request_headers = appended;
    }

    CurlResponseContext context;
    context.maximum_body_bytes = spec.max_response_bytes;
    char error_buffer[CURL_ERROR_SIZE]{};
    curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, error_buffer);
    curl_easy_setopt(handle, CURLOPT_URL, spec.url.c_str());
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, static_cast<long>(spec.timeout.count()));
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS,
                     static_cast<long>(std::min(spec.timeout, std::chrono::milliseconds(10000)).count()));
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &CurlWrite);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &context);
    curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, &CurlHeader);
    curl_easy_setopt(handle, CURLOPT_HEADERDATA, &context);
    curl_easy_setopt(handle, CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, request_headers);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, spec.verify_tls ? 1L : 0L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, spec.verify_tls ? 2L : 0L);
    // 显式传空字符串会关闭环境变量中的代理，保证是否走代理完全由插件决定。
    curl_easy_setopt(handle, CURLOPT_PROXY, spec.proxy.c_str());

    if (spec.method == "HEAD") curl_easy_setopt(handle, CURLOPT_NOBODY, 1L);
    else if (spec.method != "GET") curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, spec.method.c_str());
    if (!spec.body.empty() || spec.method == "POST" || spec.method == "PUT" || spec.method == "PATCH")
    {
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, spec.body.data());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(spec.body.size()));
    }

    const auto result = curl_easy_perform(handle);
    if (result != CURLE_OK)
    {
        if (context.body_too_large) throw std::runtime_error("HTTP 响应超过插件允许的大小。");
        const auto detail = error_buffer[0] != '\0' ? error_buffer : curl_easy_strerror(result);
        throw std::runtime_error("HTTP 请求失败: " + std::string(detail));
    }
    long status = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    context.response.status = static_cast<int>(status);
    char* effective_url = nullptr;
    curl_easy_getinfo(handle, CURLINFO_EFFECTIVE_URL, &effective_url);
    if (effective_url) context.response.effective_url = effective_url;
    return std::move(context.response);
}
} // namespace

class TerminalPluginService::Impl
{
  public:
    class Runtime;

    explicit Impl(std::filesystem::path plugin_directory)
        : plugin_directory_(std::move(plugin_directory))
    {
    }

    ~Impl() { Stop(); }

    void Start()
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (worker_.joinable()) return;
        queue_ = std::make_shared<EventQueue>();
        worker_ = std::jthread([this](std::stop_token token) { Run(token); });
    }

    void Stop()
    {
        std::jthread worker;
        std::shared_ptr<EventQueue> queue;
        {
            std::lock_guard lock(lifecycle_mutex_);
            if (!worker_.joinable()) return;
            queue = queue_;
            worker = std::move(worker_);
            queue_.reset();
        }
        if (queue)
        {
            {
                std::lock_guard lock(queue->mutex);
                queue->accepting = false;
                queue->events.clear();
            }
        }
        worker.request_stop();
        if (queue) queue->condition.notify_all();
        if (worker.joinable()) worker.join();
        std::lock_guard sessions_lock(sessions_mutex_);
        sessions_.clear();
    }

    void RegisterSession(std::string session_id,
                         std::string transport,
                         std::string target,
                         TerminalPluginService::WriteCallback write)
    {
        if (session_id.empty() || !write) return;
        SessionEntry entry;
        entry.id = std::move(session_id);
        entry.transport = std::move(transport);
        entry.target = std::move(target);
        entry.write = std::move(write);
        SessionContext context{entry.id, entry.transport, entry.target};
        {
            std::lock_guard lock(sessions_mutex_);
            sessions_[entry.id] = std::move(entry);
        }
        PluginEvent event;
        event.type = PluginEvent::Type::Open;
        event.session = std::move(context);
        Post(std::move(event));
    }

    void UnregisterSession(const std::string& session_id)
    {
        SessionContext context;
        {
            std::lock_guard lock(sessions_mutex_);
            const auto found = sessions_.find(session_id);
            if (found == sessions_.end()) return;
            context = {found->second.id, found->second.transport, found->second.target};
            sessions_.erase(found);
        }
        PluginEvent event;
        event.type = PluginEvent::Type::Close;
        event.session = std::move(context);
        Post(std::move(event));
    }

    void OnOutput(const std::string& session_id, std::string data)
    {
        if (data.empty()) return;
        SessionContext context;
        {
            std::lock_guard lock(sessions_mutex_);
            const auto found = sessions_.find(session_id);
            if (found == sessions_.end()) return;
            context = {found->second.id, found->second.transport, found->second.target};
        }
        PluginEvent event;
        event.type = PluginEvent::Type::Output;
        event.session = std::move(context);
        event.data = std::move(data);
        Post(std::move(event));
    }

    nlohmann::json ListPlugins() const
    {
        std::lock_guard lock(status_mutex_);
        return {{"directory", plugin_directory_.string()}, {"plugins", plugin_status_}};
    }

    nlohmann::json ListEffectivePlugins(const std::string& transport,
                                        const std::string& target) const
    {
        nlohmann::json effective = nlohmann::json::array();
        std::lock_guard lock(status_mutex_);
        for (const auto& status : plugin_status_)
        {
            if (!status.is_object() || !status.value("loaded", false)) continue;
            const auto transports = StringArray(status.value("transports", nlohmann::json::array()));
            const auto targets = StringArray(status.value("targets", nlohmann::json::array()));
            if (Matches(transports, transport) && Matches(targets, target)) effective.push_back(status);
        }
        return {{"transport", transport}, {"target", target}, {"plugins", std::move(effective)}};
    }

    nlohmann::json GetPlugin(const std::string& plugin_id) const
    {
        if (!ValidPluginId(plugin_id)) throw std::runtime_error("插件 id 无效。");
        std::lock_guard lock(plugin_files_mutex_);
        const auto directory = plugin_directory_ / plugin_id;
        std::error_code error;
        if (!std::filesystem::is_directory(directory, error) || std::filesystem::is_symlink(directory, error))
            throw std::runtime_error("插件不存在。");
        const auto manifest_text = ReadTextFile(directory / "manifest.json", 256 * 1024);
        const auto manifest = nlohmann::json::parse(manifest_text, nullptr, false);
        if (manifest.is_discarded() || !manifest.is_object())
            throw std::runtime_error("manifest.json 不是有效 JSON。");
        const auto entry = std::filesystem::path(manifest.value("entry", "index.js")).lexically_normal();
        if (!IsRelativeChildPath(entry)) throw std::runtime_error("插件入口必须位于插件目录内。");
        return {{"manifest", manifest}, {"source", ReadTextFile(directory / entry, kMaximumPluginSourceBytes)}};
    }

    void SavePlugin(const std::string& plugin_id,
                    const nlohmann::json& manifest,
                    const std::string& source)
    {
        if (!ValidPluginId(plugin_id)) throw std::runtime_error("插件 id 无效。");
        if (!manifest.is_object()) throw std::runtime_error("manifest 必须是对象。");
        if (source.empty() || source.size() > kMaximumPluginSourceBytes)
            throw std::runtime_error("插件源码不能为空且不能超过 1 MiB。");
        {
            std::lock_guard lock(plugin_files_mutex_);
            const auto directory = plugin_directory_ / plugin_id;
            std::error_code error;
            if (std::filesystem::is_symlink(directory, error))
                throw std::runtime_error("插件目录不能是符号链接。");
            auto saved_manifest = manifest;
            saved_manifest["id"] = plugin_id;
            saved_manifest["entry"] = "index.js";
            (void)ParseManifest(directory, saved_manifest);
            std::filesystem::create_directories(directory);
            {
                std::ofstream output(directory / "manifest.json", std::ios::binary | std::ios::trunc);
                output << saved_manifest.dump(2) << '\n';
                if (!output) throw std::runtime_error("无法保存插件 manifest.json。");
            }
            {
                std::ofstream output(directory / "index.js", std::ios::binary | std::ios::trunc);
                output.write(source.data(), static_cast<std::streamsize>(source.size()));
                if (!output) throw std::runtime_error("无法保存插件 index.js。");
            }
        }
        Reload();
    }

    void DeletePlugin(const std::string& plugin_id)
    {
        if (!ValidPluginId(plugin_id)) throw std::runtime_error("插件 id 无效。");
        {
            std::lock_guard lock(plugin_files_mutex_);
            const auto directory = plugin_directory_ / plugin_id;
            std::error_code error;
            if (!std::filesystem::exists(directory, error)) return;
            if (std::filesystem::is_symlink(directory, error) || !std::filesystem::is_directory(directory, error))
                throw std::runtime_error("插件路径不是可管理的目录。");
            std::filesystem::remove_all(directory, error);
            if (error) throw std::runtime_error("无法删除插件目录: " + error.message());
        }
        Reload();
    }

    void Reload()
    {
        auto completion = std::make_shared<std::promise<void>>();
        auto completed = completion->get_future();
        PluginEvent event;
        event.type = PluginEvent::Type::Reload;
        event.completion = std::move(completion);
        if (!Post(std::move(event))) throw std::runtime_error("插件服务尚未启动。");
        if (completed.wait_for(5s) != std::future_status::ready)
            throw std::runtime_error("插件重新加载超时。");
        completed.get();
    }

    void WriteTerminal(const std::string& session_id, std::string data)
    {
        TerminalPluginService::WriteCallback write;
        {
            std::lock_guard lock(sessions_mutex_);
            const auto found = sessions_.find(session_id);
            if (found == sessions_.end()) throw std::runtime_error("终端会话已经关闭。");
            write = found->second.write;
        }
        write(std::move(data));
    }

    void DispatchHttp(const std::string& plugin_id,
                      const std::string& request_id,
                      const std::string& session_id,
                      HttpRequestSpec spec)
    {
        std::shared_ptr<EventQueue> queue;
        {
            std::lock_guard lock(lifecycle_mutex_);
            queue = queue_;
        }
        if (!queue) throw std::runtime_error("插件服务已经停止。");

        std::thread(
            [queue, plugin_id, request_id, session_id, spec = std::move(spec)]() mutable {
                PluginEvent event;
                event.type = PluginEvent::Type::HttpComplete;
                event.plugin_id = plugin_id;
                event.request_id = request_id;
                event.session.id = session_id;
                try
                {
                    event.response = PerformHttpRequest(spec);
                }
                catch (const std::exception& error)
                {
                    event.error = error.what();
                }
                {
                    std::lock_guard lock(queue->mutex);
                    if (!queue->accepting) return;
                    queue->events.push_back(std::move(event));
                }
                queue->condition.notify_one();
            })
            .detach();
    }

    std::string NextHttpRequestId(const std::string& plugin_id)
    {
        return plugin_id + "-" + std::to_string(++next_http_request_id_);
    }

  private:
    bool Post(PluginEvent event)
    {
        std::shared_ptr<EventQueue> queue;
        {
            std::lock_guard lock(lifecycle_mutex_);
            queue = queue_;
        }
        if (!queue) return false;
        {
            std::lock_guard lock(queue->mutex);
            if (!queue->accepting) return false;
            queue->events.push_back(std::move(event));
        }
        queue->condition.notify_one();
        return true;
    }

    void Run(std::stop_token token);
    void LoadPlugins();
    void Dispatch(const PluginEvent& event);

    std::filesystem::path plugin_directory_;
    mutable std::mutex lifecycle_mutex_;
    std::jthread worker_;
    std::shared_ptr<EventQueue> queue_;
    mutable std::mutex sessions_mutex_;
    std::unordered_map<std::string, SessionEntry> sessions_;
    mutable std::mutex status_mutex_;
    nlohmann::json plugin_status_ = nlohmann::json::array();
    mutable std::mutex plugin_files_mutex_;
    std::atomic<std::uint64_t> next_http_request_id_{0};
    std::vector<std::unique_ptr<Runtime>> plugins_;
};

class TerminalPluginService::Impl::Runtime
{
  public:
    Runtime(Impl& service, PluginManifest manifest)
        : service_(service), manifest_(std::move(manifest))
    {
        try
        {
            runtime_ = JS_NewRuntime();
            if (!runtime_) throw std::runtime_error("无法创建 QuickJS Runtime。");
            JS_SetMemoryLimit(runtime_, manifest_.memory_limit_bytes);
            JS_SetMaxStackSize(runtime_, 512 * 1024);
            JS_SetInterruptHandler(runtime_, &Runtime::Interrupt, this);
            JS_SetHostPromiseRejectionTracker(runtime_, &Runtime::PromiseRejected, this);
            context_ = JS_NewContext(runtime_);
            if (!context_) throw std::runtime_error("无法创建 QuickJS Context。");
            JS_SetContextOpaque(context_, this);
            InstallApi();
            const auto source = ReadTextFile(manifest_.entry, kMaximumPluginSourceBytes);
            BeginExecution();
            auto result = JS_Eval(context_, source.data(), source.size(), manifest_.entry.string().c_str(), JS_EVAL_TYPE_GLOBAL);
            if (JS_IsException(result))
            {
                EndExecution();
                throw std::runtime_error("插件脚本加载失败: " + TakeException());
            }
            JS_FreeValue(context_, result);
            DrainJobs();
            EndExecution();
            auto global = JS_GetGlobalObject(context_);
            auto handler = JS_GetPropertyStr(context_, global, "onTerminalData");
            const auto valid_handler = JS_IsFunction(context_, handler);
            JS_FreeValue(context_, handler);
            JS_FreeValue(context_, global);
            if (!valid_handler) throw std::runtime_error("插件必须定义 onTerminalData(event) 函数。");
        }
        catch (...)
        {
            DestroyRuntime();
            throw;
        }
    }

    ~Runtime() { DestroyRuntime(); }

    const std::string& Id() const { return manifest_.id; }

    bool Matches(const SessionContext& session) const
    {
        return plugins::Matches(manifest_.transports, session.transport) &&
               plugins::Matches(manifest_.targets, session.target);
    }

    void Handle(const PluginEvent& event)
    {
        const char* handler_name = nullptr;
        if (event.type == PluginEvent::Type::Open) handler_name = "onTerminalOpen";
        else if (event.type == PluginEvent::Type::Output) handler_name = "onTerminalData";
        else if (event.type == PluginEvent::Type::Close) handler_name = "onTerminalClose";
        if (!handler_name) return;

        auto global = JS_GetGlobalObject(context_);
        auto handler = JS_GetPropertyStr(context_, global, handler_name);
        if (!JS_IsFunction(context_, handler))
        {
            JS_FreeValue(context_, handler);
            JS_FreeValue(context_, global);
            return;
        }
        auto argument = EventObject(event);
        active_session_id_ = event.session.id;
        BeginExecution();
        auto result = JS_Call(context_, handler, global, 1, &argument);
        JS_FreeValue(context_, argument);
        JS_FreeValue(context_, handler);
        JS_FreeValue(context_, global);
        if (JS_IsException(result)) LogError(std::string(handler_name) + " 执行失败: " + TakeException());
        else JS_FreeValue(context_, result);
        DrainJobs();
        EndExecution();
        active_session_id_.clear();
    }

    void CompleteHttp(const PluginEvent& event)
    {
        const auto found = pending_.find(event.request_id);
        if (found == pending_.end()) return;
        auto pending = found->second;
        pending_.erase(found);
        active_session_id_ = pending.session_id;
        BeginExecution();
        JSValue argument;
        JSValue callback;
        if (!event.error.empty())
        {
            argument = JS_NewStringLen(context_, event.error.data(), event.error.size());
            callback = pending.reject;
        }
        else
        {
            argument = ResponseObject(event.response);
            callback = pending.resolve;
        }
        auto result = JS_Call(context_, callback, JS_UNDEFINED, 1, &argument);
        JS_FreeValue(context_, argument);
        JS_FreeValue(context_, pending.resolve);
        JS_FreeValue(context_, pending.reject);
        if (JS_IsException(result)) LogError("HTTP Promise 恢复失败: " + TakeException());
        else JS_FreeValue(context_, result);
        DrainJobs();
        EndExecution();
        active_session_id_.clear();
    }

  private:
    struct PendingRequest
    {
        JSValue resolve = JS_UNDEFINED;
        JSValue reject = JS_UNDEFINED;
        std::string session_id;
    };

    static int Interrupt(JSRuntime*, void* opaque)
    {
        const auto* self = static_cast<Runtime*>(opaque);
        return self->execution_active_ && std::chrono::steady_clock::now() > self->execution_deadline_;
    }

    static void PromiseRejected(JSContext* context,
                                JSValueConst,
                                JSValueConst reason,
                                bool is_handled,
                                void* opaque)
    {
        if (is_handled) return;
        auto* self = static_cast<Runtime*>(opaque);
        const auto message = JsString(context, reason).value_or("未知 Promise 异常");
        self->LogError("未处理的 Promise 拒绝: " + message);
    }

    static JSValue HttpRequest(JSContext* context,
                               JSValueConst,
                               int argument_count,
                               JSValueConst* arguments)
    {
        auto* self = static_cast<Runtime*>(JS_GetContextOpaque(context));
        if (!self || argument_count < 1 || !JS_IsObject(arguments[0]))
            return JS_ThrowTypeError(context, "space.http.request(options) requires an object");
        try
        {
            return self->StartHttp(arguments[0]);
        }
        catch (const std::exception& error)
        {
            return JS_ThrowTypeError(context, "%s", error.what());
        }
    }

    static JSValue TerminalWrite(JSContext* context,
                                 JSValueConst,
                                 int argument_count,
                                 JSValueConst* arguments)
    {
        auto* self = static_cast<Runtime*>(JS_GetContextOpaque(context));
        if (!self || argument_count < 1)
            return JS_ThrowTypeError(context, "space.terminal.write(data) requires data");
        try
        {
            if (self->active_session_id_.empty()) throw std::runtime_error("当前没有终端事件上下文。");
            std::string data;
            if (JS_IsArrayBuffer(arguments[0]))
            {
                std::size_t size = 0;
                const auto* bytes = JS_GetArrayBuffer(context, &size, arguments[0]);
                if (!bytes) throw std::runtime_error("无法读取 ArrayBuffer。");
                data.assign(reinterpret_cast<const char*>(bytes), size);
            }
            else
            {
                const auto text = JsString(context, arguments[0]);
                if (!text) throw std::runtime_error("写入内容必须是字符串或 ArrayBuffer。");
                data = *text;
            }
            self->service_.WriteTerminal(self->active_session_id_, std::move(data));
            return JS_NewBool(context, true);
        }
        catch (const std::exception& error)
        {
            return JS_ThrowInternalError(context, "%s", error.what());
        }
    }

    static JSValue PluginLog(JSContext* context,
                             JSValueConst,
                             int argument_count,
                             JSValueConst* arguments)
    {
        auto* self = static_cast<Runtime*>(JS_GetContextOpaque(context));
        if (!self) return JS_UNDEFINED;
        const auto level = argument_count > 1 ? JsString(context, arguments[0]).value_or("info") : "info";
        const auto message_index = argument_count > 1 ? 1 : 0;
        const auto message = argument_count > message_index
            ? JsString(context, arguments[message_index]).value_or("")
            : std::string{};
        self->Log(level, message);
        return JS_UNDEFINED;
    }

    static JSValue CryptoRandomBytes(JSContext* context,
                                     JSValueConst,
                                     int argument_count,
                                     JSValueConst* arguments)
    {
        std::int32_t size = 0;
        if (argument_count < 1 || JS_ToInt32(context, &size, arguments[0]) < 0 || size < 1 || size > 65536)
            return JS_ThrowRangeError(context, "space.crypto.randomBytes(size) requires 1..65536");
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        if (RAND_bytes(bytes.data(), size) != 1)
            return JS_ThrowInternalError(context, "secure random generation failed: %s", OpenSslError().c_str());
        return JS_NewUint8ArrayCopy(context, bytes.data(), bytes.size());
    }

    static JSValue CryptoRsaEncryptPkcs1v15(JSContext* context,
                                            JSValueConst,
                                            int argument_count,
                                            JSValueConst* arguments)
    {
        if (argument_count < 1 || !JS_IsObject(arguments[0]))
            return JS_ThrowTypeError(context,
                                     "space.crypto.rsaEncryptPkcs1v15(options) requires an object");
        try
        {
            const auto public_key = JsStringProperty(context, arguments[0], "publicKey").value_or("");
            const auto data = JsStringProperty(context, arguments[0], "data").value_or("");
            if (public_key.empty() || public_key.size() > 64 * 1024)
                throw std::runtime_error("RSA 公钥不能为空且不能超过 64 KiB。");
            if (data.size() > 64 * 1024) throw std::runtime_error("RSA 明文不能超过 64 KiB。");

            auto* bio = BIO_new_mem_buf(public_key.data(), static_cast<int>(public_key.size()));
            if (!bio) throw std::runtime_error("无法创建 RSA 公钥缓冲区。");
            struct BioGuard
            {
                BIO* value;
                ~BioGuard() { BIO_free(value); }
            } bio_guard{bio};
            auto* key = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
            if (!key) throw std::runtime_error("RSA 公钥解析失败: " + OpenSslError());
            struct KeyGuard
            {
                EVP_PKEY* value;
                ~KeyGuard() { EVP_PKEY_free(value); }
            } key_guard{key};
            if (EVP_PKEY_base_id(key) != EVP_PKEY_RSA)
                throw std::runtime_error("公钥不是 RSA 公钥。");

            auto* key_context = EVP_PKEY_CTX_new(key, nullptr);
            if (!key_context) throw std::runtime_error("无法创建 RSA 加密上下文。");
            struct KeyContextGuard
            {
                EVP_PKEY_CTX* value;
                ~KeyContextGuard() { EVP_PKEY_CTX_free(value); }
            } key_context_guard{key_context};
            if (EVP_PKEY_encrypt_init(key_context) <= 0 ||
                EVP_PKEY_CTX_set_rsa_padding(key_context, RSA_PKCS1_PADDING) <= 0)
                throw std::runtime_error("RSA PKCS#1 v1.5 初始化失败: " + OpenSslError());
            std::size_t encrypted_size = 0;
            if (EVP_PKEY_encrypt(key_context,
                                 nullptr,
                                 &encrypted_size,
                                 reinterpret_cast<const unsigned char*>(data.data()),
                                 data.size()) <= 0)
                throw std::runtime_error("RSA 明文长度不适合当前公钥: " + OpenSslError());
            std::vector<unsigned char> encrypted(encrypted_size);
            if (EVP_PKEY_encrypt(key_context,
                                 encrypted.data(),
                                 &encrypted_size,
                                 reinterpret_cast<const unsigned char*>(data.data()),
                                 data.size()) <= 0)
                throw std::runtime_error("RSA 加密失败: " + OpenSslError());
            const auto encoded = Base64Encode(encrypted.data(), encrypted_size);
            return JS_NewStringLen(context, encoded.data(), encoded.size());
        }
        catch (const std::exception& error)
        {
            return JS_ThrowInternalError(context, "%s", error.what());
        }
    }

    void InstallApi()
    {
        auto global = JS_GetGlobalObject(context_);
        auto space = JS_NewObject(context_);
        auto http = JS_NewObject(context_);
        auto terminal = JS_NewObject(context_);
        auto crypto = JS_NewObject(context_);
        JS_SetPropertyStr(context_, http, "request", JS_NewCFunction(context_, &Runtime::HttpRequest, "request", 1));
        JS_SetPropertyStr(context_, terminal, "write", JS_NewCFunction(context_, &Runtime::TerminalWrite, "write", 1));
        JS_SetPropertyStr(context_, crypto, "randomBytes",
                          JS_NewCFunction(context_, &Runtime::CryptoRandomBytes, "randomBytes", 1));
        JS_SetPropertyStr(context_, crypto, "rsaEncryptPkcs1v15",
                          JS_NewCFunction(context_, &Runtime::CryptoRsaEncryptPkcs1v15,
                                          "rsaEncryptPkcs1v15", 1));
        JS_SetPropertyStr(context_, space, "http", http);
        JS_SetPropertyStr(context_, space, "terminal", terminal);
        JS_SetPropertyStr(context_, space, "crypto", crypto);
        JS_SetPropertyStr(context_, space, "log", JS_NewCFunction(context_, &Runtime::PluginLog, "log", 2));
        JS_SetPropertyStr(context_, global, "space", space);
        JS_FreeValue(context_, global);
    }

    JSValue StartHttp(JSValueConst options)
    {
        if (active_session_id_.empty()) throw std::runtime_error("HTTP 请求必须从终端事件中发起。");
        if (pending_.size() >= manifest_.max_concurrent_requests)
            throw std::runtime_error("插件 HTTP 并发请求数已达到上限。");
        HttpRequestSpec spec;
        const auto url = JsStringProperty(context_, options, "url").value_or("");
        if (url.empty() || url.size() > 4096) throw std::runtime_error("HTTP URL 不能为空且不能超过 4096 字节。");
        const auto parsed = ParseUrl(url);
        if (!HostAllowed(manifest_.allowed_hosts, parsed.host))
            throw std::runtime_error("HTTP 主机不在插件 allowedHosts 白名单中: " + parsed.host);
        spec.url = url;
        spec.host = parsed.host;
        spec.method = Lower(JsStringProperty(context_, options, "method").value_or("get"));
        std::transform(spec.method.begin(), spec.method.end(), spec.method.begin(), [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
        ValidateHttpMethod(spec.method);
        spec.body = JsStringProperty(context_, options, "body").value_or("");
        if (spec.body.size() > kMaximumRequestBodyBytes)
            throw std::runtime_error("HTTP 请求体不能超过 256 KiB。");
        spec.timeout = std::chrono::milliseconds(
            std::clamp(JsIntegerProperty(context_, options, "timeoutMs", 5000), 100, 30000));
        spec.proxy = JsStringProperty(context_, options, "proxy").value_or("");
        if (spec.proxy.size() > 4096) throw std::runtime_error("HTTP 代理地址不能超过 4096 字节。");
        spec.verify_tls = JsBooleanProperty(context_, options, "verifyTls", true);
        spec.max_response_bytes = manifest_.max_response_bytes;

        auto headers = JS_GetPropertyStr(context_, options, "headers");
        if (JS_IsObject(headers))
        {
            JSPropertyEnum* properties = nullptr;
            std::uint32_t count = 0;
            if (JS_GetOwnPropertyNames(context_, &properties, &count, headers,
                                       JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
            {
                JS_FreeValue(context_, headers);
                throw std::runtime_error("无法读取 HTTP headers。");
            }
            if (count > kMaximumHeaderCount)
            {
                for (std::uint32_t index = 0; index < count; ++index) JS_FreeAtom(context_, properties[index].atom);
                js_free(context_, properties);
                JS_FreeValue(context_, headers);
                throw std::runtime_error("HTTP headers 数量超过限制。");
            }
            for (std::uint32_t index = 0; index < count; ++index)
            {
                const auto* name_text = JS_AtomToCString(context_, properties[index].atom);
                auto value = JS_GetProperty(context_, headers, properties[index].atom);
                const auto value_text = JsString(context_, value);
                if (name_text && value_text && !UnsafeHeader(name_text))
                {
                    if (InvalidHeaderText(name_text, *value_text))
                    {
                        if (name_text) JS_FreeCString(context_, name_text);
                        JS_FreeValue(context_, value);
                        for (std::uint32_t rest = index; rest < count; ++rest)
                            JS_FreeAtom(context_, properties[rest].atom);
                        js_free(context_, properties);
                        JS_FreeValue(context_, headers);
                        throw std::runtime_error("HTTP header 名称或内容无效。");
                    }
                    spec.headers.emplace(name_text, *value_text);
                }
                if (name_text) JS_FreeCString(context_, name_text);
                JS_FreeValue(context_, value);
                JS_FreeAtom(context_, properties[index].atom);
            }
            js_free(context_, properties);
        }
        JS_FreeValue(context_, headers);

        JSValue resolving[2];
        auto promise = JS_NewPromiseCapability(context_, resolving);
        if (JS_IsException(promise)) throw std::runtime_error("无法创建 HTTP Promise。");
        const auto request_id = service_.NextHttpRequestId(manifest_.id);
        pending_.emplace(request_id, PendingRequest{resolving[0], resolving[1], active_session_id_});
        try
        {
            service_.DispatchHttp(manifest_.id, request_id, active_session_id_, std::move(spec));
        }
        catch (...)
        {
            const auto found = pending_.find(request_id);
            JS_FreeValue(context_, found->second.resolve);
            JS_FreeValue(context_, found->second.reject);
            pending_.erase(found);
            JS_FreeValue(context_, promise);
            throw;
        }
        return promise;
    }

    JSValue EventObject(const PluginEvent& event)
    {
        auto object = JS_NewObject(context_);
        JS_SetPropertyStr(context_, object, "sessionId", JS_NewString(context_, event.session.id.c_str()));
        JS_SetPropertyStr(context_, object, "transport", JS_NewString(context_, event.session.transport.c_str()));
        JS_SetPropertyStr(context_, object, "target", JS_NewString(context_, event.session.target.c_str()));
        if (event.type == PluginEvent::Type::Output)
        {
            JS_SetPropertyStr(context_, object, "data", JS_NewStringLen(context_, event.data.data(), event.data.size()));
            JS_SetPropertyStr(context_, object, "bytes",
                              JS_NewUint8ArrayCopy(context_, reinterpret_cast<const std::uint8_t*>(event.data.data()),
                                                   event.data.size()));
        }
        return object;
    }

    JSValue ResponseObject(const HttpResponseData& response)
    {
        auto object = JS_NewObject(context_);
        JS_SetPropertyStr(context_, object, "status", JS_NewInt32(context_, response.status));
        JS_SetPropertyStr(context_, object, "ok", JS_NewBool(context_, response.status >= 200 && response.status < 300));
        JS_SetPropertyStr(context_, object, "body",
                          JS_NewStringLen(context_, response.body.data(), response.body.size()));
        JS_SetPropertyStr(context_, object, "url",
                          JS_NewStringLen(context_, response.effective_url.data(), response.effective_url.size()));
        auto headers = JS_NewObject(context_);
        for (const auto& [name, value] : response.headers)
            JS_SetPropertyStr(context_, headers, name.c_str(), JS_NewStringLen(context_, value.data(), value.size()));
        JS_SetPropertyStr(context_, object, "headers", headers);
        auto header_values = JS_NewObject(context_);
        for (const auto& [name, values] : response.header_values)
        {
            auto array = JS_NewArray(context_);
            for (std::uint32_t index = 0; index < values.size(); ++index)
                JS_SetPropertyUint32(context_, array, index,
                                     JS_NewStringLen(context_, values[index].data(), values[index].size()));
            JS_SetPropertyStr(context_, header_values, name.c_str(), array);
        }
        JS_SetPropertyStr(context_, object, "headerValues", header_values);
        auto set_cookies = JS_NewArray(context_);
        for (std::uint32_t index = 0; index < response.set_cookies.size(); ++index)
            JS_SetPropertyUint32(context_, set_cookies, index,
                                 JS_NewStringLen(context_, response.set_cookies[index].data(),
                                                response.set_cookies[index].size()));
        JS_SetPropertyStr(context_, object, "setCookies", set_cookies);
        return object;
    }

    void BeginExecution()
    {
        execution_active_ = true;
        execution_deadline_ = std::chrono::steady_clock::now() + manifest_.execution_timeout;
    }

    void EndExecution() { execution_active_ = false; }

    void DrainJobs()
    {
        JSContext* job_context = nullptr;
        int result = 0;
        while ((result = JS_ExecutePendingJob(runtime_, &job_context)) > 0)
        {
            if (std::chrono::steady_clock::now() > execution_deadline_) break;
        }
        if (result < 0) LogError("异步 JavaScript 任务失败: " + TakeException());
    }

    void DestroyRuntime()
    {
        if (context_)
        {
            for (auto& [ignored, pending] : pending_)
            {
                JS_FreeValue(context_, pending.resolve);
                JS_FreeValue(context_, pending.reject);
            }
            pending_.clear();
            JS_FreeContext(context_);
            context_ = nullptr;
        }
        if (runtime_)
        {
            JS_FreeRuntime(runtime_);
            runtime_ = nullptr;
        }
    }

    std::string TakeException()
    {
        auto exception = JS_GetException(context_);
        auto message = JsString(context_, exception).value_or("未知 JavaScript 异常");
        auto stack = JS_GetPropertyStr(context_, exception, "stack");
        if (const auto stack_text = JsString(context_, stack); stack_text && *stack_text != message)
            message += "\n" + *stack_text;
        JS_FreeValue(context_, stack);
        JS_FreeValue(context_, exception);
        return message;
    }

    void Log(const std::string& level, const std::string& message) const
    {
        const auto rendered = "Plugin [" + manifest_.id + "]: " + message;
        if (level == "error") logE(rendered.c_str());
        else if (level == "warn") logW(rendered.c_str());
        else if (level == "debug") logD(rendered.c_str());
        else logI(rendered.c_str());
    }

    void LogError(const std::string& message) const { Log("error", message); }

    Impl& service_;
    PluginManifest manifest_;
    JSRuntime* runtime_ = nullptr;
    JSContext* context_ = nullptr;
    std::unordered_map<std::string, PendingRequest> pending_;
    std::string active_session_id_;
    bool execution_active_ = false;
    std::chrono::steady_clock::time_point execution_deadline_{};
};

void TerminalPluginService::Impl::Run(std::stop_token token)
{
    LoadPlugins();
    std::shared_ptr<EventQueue> queue;
    {
        std::lock_guard lock(lifecycle_mutex_);
        queue = queue_;
    }
    if (!queue) return;
    while (!token.stop_requested())
    {
        PluginEvent event;
        {
            std::unique_lock lock(queue->mutex);
            queue->condition.wait(lock, [&] { return token.stop_requested() || !queue->events.empty(); });
            if (token.stop_requested()) break;
            event = std::move(queue->events.front());
            queue->events.pop_front();
        }
        Dispatch(event);
    }
    plugins_.clear();
}

void TerminalPluginService::Impl::LoadPlugins()
{
    std::lock_guard files_lock(plugin_files_mutex_);
    nlohmann::json statuses = nlohmann::json::array();
    std::error_code error;
    std::filesystem::create_directories(plugin_directory_, error);
    std::vector<std::filesystem::path> directories;
    for (const auto& entry : std::filesystem::directory_iterator(plugin_directory_, error))
        if (entry.is_directory() && !entry.is_symlink()) directories.push_back(entry.path());
    std::sort(directories.begin(), directories.end());

    std::unordered_set<std::string> ids;
    for (const auto& directory : directories)
    {
        nlohmann::json status{{"directory", directory.filename().string()}, {"loaded", false}};
        try
        {
            const auto manifest_text = ReadTextFile(directory / "manifest.json", 256 * 1024);
            const auto json = nlohmann::json::parse(manifest_text, nullptr, false);
            if (json.is_discarded()) throw std::runtime_error("manifest.json 不是有效 JSON。");
            const auto enabled = json.value("enabled", false);
            status["id"] = json.value("id", directory.filename().string());
            status["name"] = json.value("name", status["id"].get<std::string>());
            status["enabled"] = enabled;
            auto manifest = ParseManifest(directory, json);
            if (!ids.insert(manifest.id).second) throw std::runtime_error("插件 id 重复。");
            status["id"] = manifest.id;
            status["name"] = manifest.name;
            status["transports"] = manifest.transports;
            status["targets"] = manifest.targets;
            status["allowedHosts"] = manifest.allowed_hosts;
            if (!enabled)
            {
                statuses.push_back(std::move(status));
                continue;
            }
            plugins_.push_back(std::make_unique<Runtime>(*this, std::move(manifest)));
            status["loaded"] = true;
            const auto message = "Terminal plugin loaded: " + plugins_.back()->Id();
            logI(message.c_str());
        }
        catch (const std::exception& exception)
        {
            status["error"] = exception.what();
            const auto message = "Terminal plugin load failed: " + directory.string() + ": " + exception.what();
            logE(message.c_str());
        }
        statuses.push_back(std::move(status));
    }
    {
        std::lock_guard lock(status_mutex_);
        plugin_status_ = std::move(statuses);
    }
}

void TerminalPluginService::Impl::Dispatch(const PluginEvent& event)
{
    if (event.type == PluginEvent::Type::Reload)
    {
        try
        {
            plugins_.clear();
            LoadPlugins();
            std::vector<SessionContext> sessions;
            {
                std::lock_guard lock(sessions_mutex_);
                sessions.reserve(sessions_.size());
                for (const auto& [ignored, session] : sessions_)
                    sessions.push_back({session.id, session.transport, session.target});
            }
            for (const auto& session : sessions)
            {
                PluginEvent opened;
                opened.type = PluginEvent::Type::Open;
                opened.session = session;
                Dispatch(opened);
            }
            if (event.completion) event.completion->set_value();
        }
        catch (...)
        {
            if (event.completion) event.completion->set_exception(std::current_exception());
            else logE("Terminal plugin reload failed with an unknown exception");
        }
        return;
    }
    if (event.type == PluginEvent::Type::HttpComplete)
    {
        const auto found = std::find_if(plugins_.begin(), plugins_.end(), [&](const auto& plugin) {
            return plugin->Id() == event.plugin_id;
        });
        if (found != plugins_.end()) (*found)->CompleteHttp(event);
        return;
    }
    for (const auto& plugin : plugins_)
        if (plugin->Matches(event.session)) plugin->Handle(event);
}

TerminalPluginService::TerminalPluginService(std::filesystem::path plugin_directory)
    : impl_(std::make_unique<Impl>(std::move(plugin_directory)))
{
}

TerminalPluginService::~TerminalPluginService() = default;

void TerminalPluginService::Start() { impl_->Start(); }
void TerminalPluginService::Stop() { impl_->Stop(); }

void TerminalPluginService::RegisterSession(std::string session_id,
                                            std::string transport,
                                            std::string target,
                                            WriteCallback write)
{
    impl_->RegisterSession(std::move(session_id), std::move(transport), std::move(target), std::move(write));
}

void TerminalPluginService::UnregisterSession(const std::string& session_id)
{
    impl_->UnregisterSession(session_id);
}

void TerminalPluginService::OnOutput(const std::string& session_id, std::string data)
{
    impl_->OnOutput(session_id, std::move(data));
}

nlohmann::json TerminalPluginService::ListPlugins() const { return impl_->ListPlugins(); }

nlohmann::json TerminalPluginService::ListEffectivePlugins(const std::string& transport,
                                                            const std::string& target) const
{
    return impl_->ListEffectivePlugins(transport, target);
}

nlohmann::json TerminalPluginService::GetPlugin(const std::string& plugin_id) const
{
    return impl_->GetPlugin(plugin_id);
}

void TerminalPluginService::SavePlugin(const std::string& plugin_id,
                                       const nlohmann::json& manifest,
                                       const std::string& source)
{
    impl_->SavePlugin(plugin_id, manifest, source);
}

void TerminalPluginService::DeletePlugin(const std::string& plugin_id)
{
    impl_->DeletePlugin(plugin_id);
}

void TerminalPluginService::Reload() { impl_->Reload(); }
} // namespace spacestation::plugins
