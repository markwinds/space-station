#include "http/http_server.hpp"

#include "cert/certificate_service.hpp"
#include "embedded_assets.hpp"
#include "logging/logger.hpp"

#include <nlohmann/json.hpp>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

namespace spacestation
{
namespace
{
template <typename T, void (*Deleter)(T*)>
using OpenSslPtr = std::unique_ptr<T, decltype(Deleter)>;

std::string StripPrefix(std::string path, std::string_view prefix)
{
    if (path == prefix)
    {
        return "/";
    }
    if (path.starts_with(prefix) && path.size() > prefix.size() && path[prefix.size()] == '/')
    {
        return path.substr(prefix.size());
    }
    return path;
}

drogon::HttpResponsePtr JsonResponse(const nlohmann::json& payload,
                                     drogon::HttpStatusCode status = drogon::k200OK)
{
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    response->setStatusCode(status);
    response->setBody(payload.dump());
    return response;
}

bool ParseJsonBody(const drogon::HttpRequestPtr& req,
                   nlohmann::json& body,
                   std::function<void(const drogon::HttpResponsePtr&)>& callback)
{
    body = nlohmann::json::parse(req->body(), nullptr, false);
    if (body.is_discarded() || !body.is_object())
    {
        callback(JsonResponse({{"code", "invalid_json"}, {"message", "request body must be a JSON object"}},
                              drogon::k400BadRequest));
        return false;
    }
    return true;
}

drogon::HttpStatusCode StatusForToolResult(const nlohmann::json& result)
{
    return result.value("ok", false) ? drogon::k200OK : drogon::k400BadRequest;
}

void AddCertificateExtension(X509* certificate, int nid, const char* value)
{
    X509V3_CTX context{};
    X509V3_set_ctx(&context, certificate, certificate, nullptr, nullptr, 0);
    OpenSslPtr<X509_EXTENSION, X509_EXTENSION_free> extension(
        X509V3_EXT_conf_nid(nullptr, &context, nid, const_cast<char*>(value)),
        X509_EXTENSION_free);
    if (!extension || X509_add_ext(certificate, extension.get(), -1) != 1)
    {
        throw std::runtime_error("TLS 默认证书扩展生成失败。");
    }
}

void WritePrivateKey(EVP_PKEY* key, const std::filesystem::path& path)
{
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path());
    }
    OpenSslPtr<BIO, BIO_free_all> bio(BIO_new_file(path.string().c_str(), "w"), BIO_free_all);
    if (!bio || PEM_write_bio_PrivateKey(bio.get(), key, nullptr, nullptr, 0, nullptr, nullptr) != 1)
    {
        throw std::runtime_error("TLS 默认私钥写入失败。");
    }
}

void WriteCertificate(X509* certificate, const std::filesystem::path& path)
{
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path());
    }
    OpenSslPtr<BIO, BIO_free_all> bio(BIO_new_file(path.string().c_str(), "w"), BIO_free_all);
    if (!bio || PEM_write_bio_X509(bio.get(), certificate) != 1)
    {
        throw std::runtime_error("TLS 默认证书写入失败。");
    }
}

void GenerateDefaultTlsCertificate(const std::filesystem::path& certificate_path,
                                   const std::filesystem::path& private_key_path)
{
    OpenSslPtr<EVP_PKEY_CTX, EVP_PKEY_CTX_free> key_context(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr),
                                                            EVP_PKEY_CTX_free);
    EVP_PKEY* raw_key = nullptr;
    if (!key_context || EVP_PKEY_keygen_init(key_context.get()) != 1 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(key_context.get(), 2048) != 1 ||
        EVP_PKEY_keygen(key_context.get(), &raw_key) != 1)
    {
        throw std::runtime_error("TLS 默认私钥生成失败。");
    }
    OpenSslPtr<EVP_PKEY, EVP_PKEY_free> key(raw_key, EVP_PKEY_free);

    OpenSslPtr<X509, X509_free> certificate(X509_new(), X509_free);
    OpenSslPtr<BIGNUM, BN_free> serial(BN_new(), BN_free);
    OpenSslPtr<ASN1_INTEGER, ASN1_INTEGER_free> serial_integer(ASN1_INTEGER_new(), ASN1_INTEGER_free);
    if (!certificate || !serial || !serial_integer || BN_rand(serial.get(), 128, 0, 0) != 1 ||
        BN_to_ASN1_INTEGER(serial.get(), serial_integer.get()) == nullptr ||
        X509_set_version(certificate.get(), 2L) != 1 ||
        X509_set_serialNumber(certificate.get(), serial_integer.get()) != 1 ||
        X509_gmtime_adj(X509_getm_notBefore(certificate.get()), 0) == nullptr ||
        X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 3650L * 24L * 60L * 60L) == nullptr ||
        X509_set_pubkey(certificate.get(), key.get()) != 1)
    {
        throw std::runtime_error("TLS 默认证书生成失败。");
    }

    X509_NAME* name = X509_get_subject_name(certificate.get());
    if (!name ||
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char*>("localhost"), -1, -1, 0) != 1 ||
        X509_set_issuer_name(certificate.get(), name) != 1)
    {
        throw std::runtime_error("TLS 默认证书主题生成失败。");
    }

    AddCertificateExtension(certificate.get(), NID_basic_constraints, "critical,CA:FALSE");
    AddCertificateExtension(certificate.get(), NID_key_usage, "critical,digitalSignature,keyEncipherment");
    AddCertificateExtension(certificate.get(), NID_ext_key_usage, "serverAuth");
    AddCertificateExtension(certificate.get(), NID_subject_alt_name, "DNS:localhost,IP:127.0.0.1");

    if (X509_sign(certificate.get(), key.get(), EVP_sha256()) <= 0)
    {
        throw std::runtime_error("TLS 默认证书签名失败。");
    }

    WritePrivateKey(key.get(), private_key_path);
    WriteCertificate(certificate.get(), certificate_path);
}

void EnsureTlsCertificate(const std::string& certificate_path, const std::string& private_key_path)
{
    const auto cert = std::filesystem::path(certificate_path);
    const auto key = std::filesystem::path(private_key_path);
    if (std::filesystem::exists(cert) && std::filesystem::exists(key))
    {
        return;
    }
    GenerateDefaultTlsCertificate(cert, key);
}

std::vector<std::pair<std::string, std::string>> BuildMutualTlsConfig(const std::string& trusted_root_certificate_path)
{
    if (trusted_root_certificate_path.empty())
    {
        return {};
    }

    if (!std::filesystem::exists(trusted_root_certificate_path))
    {
        throw std::runtime_error("TLS 客户端根证书文件不存在：" + trusted_root_certificate_path);
    }

    return {
        {"VerifyCAFile", trusted_root_certificate_path},
        {"RequestCAFile", trusted_root_certificate_path},
        {"VerifyMode", "Request,Require"},
    };
}

std::string SanitizeFileName(std::string file_name)
{
    for (auto& character : file_name)
    {
        if (character == '/' || character == '\\' || character == ':' || character == '\0')
        {
            character = '_';
        }
    }
    if (file_name.empty() || file_name == "." || file_name == "..")
    {
        return "download";
    }
    return file_name;
}

std::filesystem::path NormalizeRootPath(const std::string& path)
{
    auto parsed = std::filesystem::path(path);
    if (!parsed.is_absolute())
    {
        parsed = ConfigStore::DefaultDataPath().parent_path() / parsed;
    }
    const auto absolute = std::filesystem::absolute(parsed).lexically_normal();
    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(absolute, ec);
    return ec ? absolute : canonical;
}

bool IsPathInside(const std::filesystem::path& root, const std::filesystem::path& path)
{
    auto root_it = root.begin();
    auto path_it = path.begin();
    for (; root_it != root.end(); ++root_it, ++path_it)
    {
        if (path_it == path.end() || *root_it != *path_it)
        {
            return false;
        }
    }
    return true;
}

bool ValidateRelativePath(const std::string& relative_path, std::filesystem::path& path)
{
    path = std::filesystem::path(relative_path.empty() ? "." : relative_path).lexically_normal();
    if (path.is_absolute())
    {
        return false;
    }
    for (const auto& part : path)
    {
        if (part == "..")
        {
            return false;
        }
    }
    return true;
}

bool ResolveSharedPath(const nlohmann::json& shares,
                       const std::string& share_id,
                       const std::string& relative_path,
                       std::filesystem::path& root,
                       std::filesystem::path& target,
                       nlohmann::json* share_json = nullptr)
{
    if (!shares.is_array())
    {
        return false;
    }

    for (const auto& share : shares)
    {
        if (!share.is_object() || share.value("id", "") != share_id)
        {
            continue;
        }

        const auto root_path = share.value("path", "");
        if (root_path.empty())
        {
            return false;
        }
        std::filesystem::path normalized_relative;
        if (!ValidateRelativePath(relative_path, normalized_relative))
        {
            return false;
        }

        root = NormalizeRootPath(root_path);
        target = (root / normalized_relative).lexically_normal();
        std::error_code ec;
        if (std::filesystem::exists(target, ec))
        {
            const auto canonical_target = std::filesystem::weakly_canonical(target, ec);
            if (!ec)
            {
                target = canonical_target;
            }
        }
        if (!IsPathInside(root, target))
        {
            return false;
        }
        if (share_json)
        {
            *share_json = share;
        }
        return true;
    }
    return false;
}

nlohmann::json FileShareItemJson(const std::filesystem::directory_entry& entry,
                                 const std::filesystem::path& root,
                                 const std::filesystem::path& full_path)
{
    std::error_code ec;
    const auto status = entry.symlink_status(ec);
    std::string type = "file";
    if (!ec && std::filesystem::is_directory(status))
    {
        type = "directory";
    }
    else if (!ec && std::filesystem::is_symlink(status))
    {
        type = "symlink";
    }

    auto relative = std::filesystem::relative(full_path, root, ec);
    if (ec)
    {
        relative = full_path.filename();
    }
    const auto modified = entry.last_write_time(ec);
    return {
        {"name", full_path.filename().string()},
        {"path", relative.lexically_normal().generic_string()},
        {"type", type},
        {"size", type == "file" ? entry.file_size(ec) : 0},
        {"modifiedAt", ec ? 0 : modified.time_since_epoch().count()},
    };
}
} // namespace

HttpServer::HttpServer(ConfigStore& config_store, AppConfig config)
    : port_(static_cast<std::uint16_t>(config.port)),
      http_enabled_(config.http_enabled),
      http_port_(static_cast<std::uint16_t>(config.http_port)),
      certificate_path_(std::move(config.certificate_path)),
      private_key_path_(std::move(config.private_key_path)),
      trusted_root_certificate_path_(std::move(config.trusted_root_certificate_path)),
      config_store_(config_store)
{
    RegisterRoutes();
}

HttpServer::~HttpServer()
{
    Stop();
}

void HttpServer::Start()
{
    if (started_.exchange(true))
    {
        return;
    }

    drogon::app().setThreadNum(std::max(2u, std::thread::hardware_concurrency()));
    drogon::app().disableSigtermHandling();
    EnsureTlsCertificate(certificate_path_, private_key_path_);
    if (http_enabled_)
    {
        drogon::app().addListener("0.0.0.0", http_port_, false);
    }
    const auto mutual_tls_config = BuildMutualTlsConfig(trusted_root_certificate_path_);
    if (!mutual_tls_config.empty())
    {
        logI("HTTPS mutual TLS enabled");
        drogon::app().setSSLFiles(certificate_path_, private_key_path_);
        drogon::app().setSSLConfigCommands(mutual_tls_config);
        drogon::app().addListener("0.0.0.0", port_, true);
        server_thread_ = std::thread([] { drogon::app().run(); });
        return;
    }
    drogon::app().addListener("0.0.0.0", port_, true, certificate_path_, private_key_path_);
    server_thread_ = std::thread([] { drogon::app().run(); });
}

void HttpServer::Stop()
{
    if (!started_.exchange(false))
    {
        return;
    }

    drogon::app().quit();
    if (server_thread_.joinable())
    {
        server_thread_.join();
    }
}

std::string HttpServer::UiUrl() const
{
    return "https://127.0.0.1:" + std::to_string(port_) + "/web/";
}

void HttpServer::RegisterRoutes()
{
    drogon::app().registerHandler(
        "/",
        [](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            callback(drogon::HttpResponse::newRedirectionResponse("/web/"));
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/health",
        [](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            callback(JsonResponse({
                {"ok", true},
                {"service", "space-station"},
                {"version", "0.1.0"},
            }));
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/config",
        [this](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            callback(JsonResponse(config_store_.ToJson(config_store_.Load())));
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/config",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            const auto body = nlohmann::json::parse(req->body(), nullptr, false);
            if (body.is_discarded() || !body.is_object())
            {
                callback(JsonResponse({{"code", "invalid_json"}, {"message", "request body must be a JSON object"}},
                                      drogon::k400BadRequest));
                return;
            }

            config_store_.SavePartial(body);
            const auto config = config_store_.Load();
            Logger::Instance().SetLevel(ParseLogLevel(config.log_level));
            logI("Configuration updated");
            callback(JsonResponse(config_store_.ToJson(config)));
        },
        {drogon::Put});

    drogon::app().registerHandler(
        "/api/certificates/generate",
        [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback))
            {
                return;
            }
            const auto result = cert::GenerateCertificateBundle(body);
            callback(JsonResponse(result, StatusForToolResult(result)));
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/certificates/sign",
        [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback))
            {
                return;
            }
            const auto result = cert::SignCertificateRequest(body);
            callback(JsonResponse(result, StatusForToolResult(result)));
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/certificates/parse",
        [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback))
            {
                return;
            }
            const auto result = cert::ParseCertificate(body);
            callback(JsonResponse(result, StatusForToolResult(result)));
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/certificates/parse-csr",
        [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback))
            {
                return;
            }
            const auto result = cert::ParseCsr(body);
            callback(JsonResponse(result, StatusForToolResult(result)));
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/certificates/p12",
        [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback))
            {
                return;
            }
            const auto result = cert::CreatePkcs12(body);
            callback(JsonResponse(result, StatusForToolResult(result)));
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/certificates/parse-p12",
        [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback))
            {
                return;
            }
            const auto result = cert::ParsePkcs12(body);
            callback(JsonResponse(result, StatusForToolResult(result)));
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/tools/scheduler/state",
        [this](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            callback(JsonResponse(config_store_.LoadSchedulerState()));
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/tools/scheduler/state",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            const auto body = nlohmann::json::parse(req->body(), nullptr, false);
            if (body.is_discarded() || !body.is_object())
            {
                callback(JsonResponse({{"code", "invalid_json"}, {"message", "调度器状态不是合法 JSON。"}},
                                      drogon::k400BadRequest));
                return;
            }
            config_store_.SaveSchedulerState(body);
            callback(JsonResponse({{"ok", true}}));
        },
        {drogon::Put});

    drogon::app().registerHandler(
        "/api/tools/file-share/state",
        [this](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            const auto shares = config_store_.LoadFileShares();
            auto items = nlohmann::json::array();
            for (const auto& share : shares)
            {
                if (!share.is_object())
                {
                    continue;
                }
                const auto root = NormalizeRootPath(share.value("path", ""));
                std::error_code ec;
                items.push_back({
                    {"id", share.value("id", "")},
                    {"name", share.value("name", "")},
                    {"path", share.value("path", "")},
                    {"exists", std::filesystem::is_directory(root, ec)},
                });
            }
            callback(JsonResponse({{"shares", items}}));
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/tools/file-share/shares",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback))
            {
                return;
            }
            const auto shares = body.value("shares", nlohmann::json::array());
            if (!shares.is_array())
            {
                callback(JsonResponse({{"code", "invalid_shares"}, {"message", "共享目录必须是数组。"}},
                                      drogon::k400BadRequest));
                return;
            }
            config_store_.SaveFileShares(shares);
            callback(JsonResponse({{"ok", true}}));
        },
        {drogon::Put});

    drogon::app().registerHandler(
        "/api/tools/file-share/list",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            const auto share_id = req->getParameter("shareId");
            const auto relative_path = req->getParameter("path");
            std::filesystem::path root;
            std::filesystem::path target;
            nlohmann::json share;
            if (!ResolveSharedPath(config_store_.LoadFileShares(), share_id, relative_path, root, target, &share))
            {
                callback(JsonResponse({{"code", "invalid_share_or_path"}}, drogon::k400BadRequest));
                return;
            }

            std::error_code ec;
            if (!std::filesystem::exists(root, ec))
            {
                std::filesystem::create_directories(root, ec);
            }
            if (!std::filesystem::is_directory(target, ec))
            {
                callback(JsonResponse({{"code", "directory_not_found"}}, drogon::k404NotFound));
                return;
            }

            auto items = nlohmann::json::array();
            for (const auto& entry : std::filesystem::directory_iterator(target, ec))
            {
                if (ec)
                {
                    break;
                }
                items.push_back(FileShareItemJson(entry, root, entry.path()));
            }
            std::sort(items.begin(), items.end(), [](const auto& left, const auto& right) {
                const auto left_dir = left.value("type", "") == "directory";
                const auto right_dir = right.value("type", "") == "directory";
                if (left_dir != right_dir)
                {
                    return left_dir;
                }
                return left.value("name", "") < right.value("name", "");
            });

            const auto current = std::filesystem::relative(target, root, ec);
            const auto parent = current.empty() || current == "." ? std::filesystem::path() : current.parent_path();
            callback(JsonResponse({
                {"share", share},
                {"path", current.empty() || current == "." ? "" : current.generic_string()},
                {"parentPath", parent.generic_string()},
                {"items", items},
            }));
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/tools/file-share/upload",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            drogon::MultiPartParser parser;
            if (parser.parse(req) != 0)
            {
                callback(JsonResponse({{"code", "invalid_multipart"}}, drogon::k400BadRequest));
                return;
            }

            const auto share_id = parser.getParameter<std::string>("shareId");
            const auto relative_path = parser.getParameter<std::string>("path");
            const auto& files = parser.getFiles();
            if (share_id.empty() || files.empty())
            {
                callback(JsonResponse({{"code", "missing_share_or_file"}}, drogon::k400BadRequest));
                return;
            }

            std::filesystem::path root;
            std::filesystem::path target;
            if (!ResolveSharedPath(config_store_.LoadFileShares(), share_id, relative_path, root, target))
            {
                callback(JsonResponse({{"code", "invalid_share_or_path"}}, drogon::k400BadRequest));
                return;
            }

            std::error_code ec;
            std::filesystem::create_directories(target, ec);
            if (ec || !std::filesystem::is_directory(target, ec))
            {
                callback(JsonResponse({{"code", "directory_not_writable"}}, drogon::k400BadRequest));
                return;
            }

            int uploaded = 0;
            for (const auto& file : files)
            {
                const auto destination = (target / SanitizeFileName(file.getFileName())).lexically_normal();
                if (!IsPathInside(root, destination))
                {
                    continue;
                }
                std::ofstream output(destination, std::ios::binary | std::ios::trunc);
                if (!output)
                {
                    continue;
                }
                output.write(file.fileData(), static_cast<std::streamsize>(file.fileLength()));
                ++uploaded;
            }
            callback(JsonResponse({{"ok", uploaded > 0}, {"uploaded", uploaded}},
                                  uploaded > 0 ? drogon::k200OK : drogon::k400BadRequest));
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/tools/file-share/folder",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback))
            {
                return;
            }
            const auto share_id = body.value("shareId", "");
            const auto relative_path = body.value("path", "");
            const auto name = SanitizeFileName(body.value("name", ""));
            if (share_id.empty() || name.empty() || name == "download")
            {
                callback(JsonResponse({{"code", "missing_folder_name"}}, drogon::k400BadRequest));
                return;
            }
            std::filesystem::path root;
            std::filesystem::path parent;
            if (!ResolveSharedPath(config_store_.LoadFileShares(), share_id, relative_path, root, parent))
            {
                callback(JsonResponse({{"code", "invalid_share_or_path"}}, drogon::k400BadRequest));
                return;
            }
            const auto target = (parent / name).lexically_normal();
            if (!IsPathInside(root, target))
            {
                callback(JsonResponse({{"code", "invalid_folder_path"}}, drogon::k400BadRequest));
                return;
            }
            std::error_code ec;
            std::filesystem::create_directories(target, ec);
            callback(JsonResponse({{"ok", !ec}}, ec ? drogon::k400BadRequest : drogon::k200OK));
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/tools/file-share/download",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            const auto share_id = req->getParameter("shareId");
            const auto relative_path = req->getParameter("path");
            std::filesystem::path root;
            std::filesystem::path target;
            if (!ResolveSharedPath(config_store_.LoadFileShares(), share_id, relative_path, root, target) ||
                !std::filesystem::is_regular_file(target))
            {
                callback(JsonResponse({{"code", "file_not_found"}}, drogon::k404NotFound));
                return;
            }
            callback(drogon::HttpResponse::newFileResponse(target.string(),
                                                           SanitizeFileName(target.filename().string()),
                                                           drogon::CT_NONE,
                                                           "",
                                                           req));
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/tools/file-share/item",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback))
            {
                return;
            }
            std::filesystem::path root;
            std::filesystem::path target;
            if (!ResolveSharedPath(config_store_.LoadFileShares(),
                                   body.value("shareId", ""),
                                   body.value("path", ""),
                                   root,
                                   target))
            {
                callback(JsonResponse({{"code", "invalid_share_or_path"}}, drogon::k400BadRequest));
                return;
            }
            if (target == root)
            {
                callback(JsonResponse({{"code", "cannot_delete_share_root"}}, drogon::k400BadRequest));
                return;
            }
            std::error_code ec;
            if (std::filesystem::is_directory(target, ec))
            {
                std::filesystem::remove_all(target, ec);
            }
            else
            {
                std::filesystem::remove(target, ec);
            }
            callback(JsonResponse({{"ok", !ec}}, ec ? drogon::k400BadRequest : drogon::k200OK));
        },
        {drogon::Delete});

    drogon::app().registerHandlerViaRegex(
        "^/web(?:/.*)?$",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            HandleStaticAsset(StripPrefix(req->path(), "/web"), std::move(callback));
        },
        {drogon::Get});
}

void HttpServer::HandleStaticAsset(const std::string& request_path,
                                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) const
{
    std::string path = request_path;
    if (path == "/")
    {
        path = "/index.html";
    }

    if (const auto it = kEmbeddedAssets.find(path); it != kEmbeddedAssets.end())
    {
        auto response = drogon::HttpResponse::newHttpResponse();
        response->setBody(std::string(reinterpret_cast<const char*>(it->second.data), it->second.size));
        response->setContentTypeString(std::string(it->second.content_type));
        callback(response);
        return;
    }

    if (const auto index_it = kEmbeddedAssets.find("/index.html"); index_it != kEmbeddedAssets.end())
    {
        auto response = drogon::HttpResponse::newHttpResponse();
        response->setBody(std::string(reinterpret_cast<const char*>(index_it->second.data), index_it->second.size));
        response->setContentTypeString(std::string(index_it->second.content_type));
        callback(response);
        return;
    }

    callback(drogon::HttpResponse::newNotFoundResponse());
}
} // namespace spacestation
