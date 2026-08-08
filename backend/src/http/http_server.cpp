#include "http/http_server.hpp"

#include "cert/certificate_service.hpp"
#include "embedded_assets.hpp"
#include "logging/logger.hpp"
#include "serial/serial_websocket.hpp"
#include "serial/browser_serial_share_websocket.hpp"
#include "ssh/ssh_websocket.hpp"

#include <nlohmann/json.hpp>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

namespace spacestation
{
namespace
{
constexpr std::size_t kMaximumHttpRequestBodySize = 12ULL * 1024ULL * 1024ULL;

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

bool IsStaticAssetRequest(const std::string& path)
{
    return path.starts_with("/assets/");
}

bool ShouldRevalidateStaticAsset(const std::string& path)
{
    return path == "/index.html" || path == "/sw.js" || path == "/manifest.webmanifest";
}

drogon::ContentType StaticAssetContentType(const std::string& path)
{
    const auto extension = std::filesystem::path(path).extension().string();
    if (extension == ".html")
    {
        return drogon::CT_TEXT_HTML;
    }
    if (extension == ".js")
    {
        return drogon::CT_TEXT_JAVASCRIPT;
    }
    if (extension == ".css")
    {
        return drogon::CT_TEXT_CSS;
    }
    if (extension == ".json" || extension == ".webmanifest")
    {
        return drogon::CT_APPLICATION_JSON;
    }
    if (extension == ".svg")
    {
        return drogon::CT_IMAGE_SVG_XML;
    }
    if (extension == ".png")
    {
        return drogon::CT_IMAGE_PNG;
    }
    if (extension == ".ico")
    {
        return drogon::CT_IMAGE_XICON;
    }
    return drogon::CT_APPLICATION_OCTET_STREAM;
}

const EmbeddedAsset* FindCurrentHashedAsset(const std::string& path)
{
    const std::array<std::string_view, 17> hashed_asset_prefixes{
        "/assets/index-",
        "/assets/TimeManagerTool-",
        "/assets/FileManagerTool-",
        "/assets/HabitTool-",
        "/assets/DatePicker-",
        "/assets/TransferTool-",
        "/assets/TextCompareTool-",
        "/assets/SshTool-",
        "/assets/SerialTool-",
        "/assets/TerminalPluginTool-",
        "/assets/AuthenticatorTool-",
        "/assets/WebTerminal-",
        "/assets/editor.worker-",
        "/assets/binaryCompare.worker-",
        "/assets/folderCompare.worker-",
        "/assets/codicon-",
        "/assets/_plugin-vue_export-helper-",
    };

    for (const auto prefix : hashed_asset_prefixes)
    {
        if (!path.starts_with(prefix))
        {
            continue;
        }
        const auto extension = std::filesystem::path(path).extension().string();
        for (const auto& [asset_path, asset] : kEmbeddedAssets)
        {
            if (asset_path.starts_with(prefix) && std::filesystem::path(asset_path).extension() == extension)
            {
                return &asset;
            }
        }
    }
    return nullptr;
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

bool RequireSecureRequest(const drogon::HttpRequestPtr& request,
                          std::function<void(const drogon::HttpResponsePtr&)>& callback)
{
    if (request->isOnSecureConnection() || request->peerAddr().isLoopbackIp())
    {
        return true;
    }
    callback(JsonResponse({{"code", "https_required"}, {"message", "SSH 管理接口只允许通过 HTTPS 访问。"}},
                          drogon::k403Forbidden));
    return false;
}

bool RequireLoopbackRequest(const drogon::HttpRequestPtr& request,
                            std::function<void(const drogon::HttpResponsePtr&)>& callback)
{
    if (request->peerAddr().isLoopbackIp())
    {
        return true;
    }
    callback(JsonResponse({{"code", "local_access_required"},
                           {"message", "文件管理器只允许从运行 Space Station 的本机访问。"}},
                          drogon::k403Forbidden));
    return false;
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

template <typename AddListener>
void AddAllAddressListeners(AddListener&& add_listener)
{
    add_listener("0.0.0.0");
    add_listener("::");
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

} // namespace

HttpServer::HttpServer(ConfigStore& config_store, AppConfig config)
    : port_(static_cast<std::uint16_t>(config.port)),
      http_enabled_(config.http_enabled),
      http_port_(static_cast<std::uint16_t>(config.http_port)),
      certificate_path_(std::move(config.certificate_path)),
      private_key_path_(std::move(config.private_key_path)),
      trusted_root_certificate_path_(std::move(config.trusted_root_certificate_path)),
      config_store_(config_store),
      authenticator_service_(config_store),
      transfer_manager_(config_store),
      terminal_plugin_service_(std::filesystem::path(config.data_path) / "plugins"),
      serial_service_(&terminal_plugin_service_),
      browser_serial_share_service_(&terminal_plugin_service_),
      sftp_service_(config_store)
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
    drogon::app().setClientMaxBodySize(kMaximumHttpRequestBodySize);
    drogon::app().enableGzip(true);
    drogon::app().enableBrotli(true);
    drogon::app().disableSigtermHandling();
    EnsureTlsCertificate(certificate_path_, private_key_path_);
    terminal_plugin_service_.Start();
    if (http_enabled_)
    {
        AddAllAddressListeners([this](const std::string& ip) {
            drogon::app().addListener(ip, http_port_, false);
        });
    }
    const auto mutual_tls_config = BuildMutualTlsConfig(trusted_root_certificate_path_);
    if (!mutual_tls_config.empty())
    {
        logI("HTTPS mutual TLS enabled");
        drogon::app().setSSLFiles(certificate_path_, private_key_path_);
        drogon::app().setSSLConfigCommands(mutual_tls_config);
        AddAllAddressListeners([this](const std::string& ip) {
            drogon::app().addListener(ip, port_, true);
        });
        server_thread_ = std::thread([] { drogon::app().run(); });
        return;
    }
    AddAllAddressListeners([this](const std::string& ip) {
        drogon::app().addListener(ip, port_, true, certificate_path_, private_key_path_);
    });
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
    terminal_plugin_service_.Stop();
}

std::string HttpServer::UiUrl() const
{
    return "https://127.0.0.1:" + std::to_string(port_) + "/web/";
}

void HttpServer::RegisterRoutes()
{
    serial_websocket_controller_ = std::make_shared<serial::SerialWebSocketController>(serial_service_);
    drogon::app().registerController(serial_websocket_controller_);
    browser_serial_share_websocket_controller_ =
        std::make_shared<serial::BrowserSerialShareWebSocketController>(browser_serial_share_service_);
    drogon::app().registerController(browser_serial_share_websocket_controller_);
    ssh_websocket_controller_ =
        std::make_shared<ssh::SshWebSocketController>(config_store_, terminal_plugin_service_);
    drogon::app().registerController(ssh_websocket_controller_);
    drogon::app().registerHandler(
        "/api/tools/ssh/session/terminate",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback)) return;
            const auto terminated = ssh_websocket_controller_->TerminateSession(body.value("resumeToken", ""));
            callback(JsonResponse({{"ok", true}, {"terminated", terminated}}));
        },
        {drogon::Post});

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
        "/api/tools/serial/ports",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            try
            {
                callback(JsonResponse(serial_service_.ListPorts()));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/tools/serial/browser-shares",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            auto response = JsonResponse(browser_serial_share_service_.ListShares());
            response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
            response->addHeader("Pragma", "no-cache");
            callback(response);
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/tools/terminal/plugins",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            callback(JsonResponse(terminal_plugin_service_.ListPlugins()));
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/tools/terminal/plugins/reload",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            try
            {
                terminal_plugin_service_.Reload();
                callback(JsonResponse({{"ok", true}}));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k500InternalServerError));
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/tools/terminal/plugins/effective",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            const auto transport = req->getParameter("transport");
            const auto target = req->getParameter("target");
            if (transport.empty() || target.empty())
            {
                callback(JsonResponse({{"ok", false}, {"message", "transport 和 target 不能为空。"}},
                                      drogon::k400BadRequest));
                return;
            }
            auto response = JsonResponse(terminal_plugin_service_.ListEffectivePlugins(transport, target));
            response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
            callback(response);
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/tools/terminal/plugins/{1}",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& plugin_id) {
            if (!RequireSecureRequest(req, callback)) return;
            try
            {
                callback(JsonResponse(terminal_plugin_service_.GetPlugin(plugin_id)));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/tools/terminal/plugins/{1}",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& plugin_id) {
            if (!RequireSecureRequest(req, callback)) return;
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback)) return;
            try
            {
                terminal_plugin_service_.SavePlugin(plugin_id, body.value("manifest", nlohmann::json::object()),
                                                     body.value("source", ""));
                callback(JsonResponse({{"ok", true}, {"plugin", terminal_plugin_service_.GetPlugin(plugin_id)}}));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Put});

    drogon::app().registerHandler(
        "/api/tools/terminal/plugins/{1}",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& plugin_id) {
            if (!RequireSecureRequest(req, callback)) return;
            try
            {
                terminal_plugin_service_.DeletePlugin(plugin_id);
                callback(JsonResponse({{"ok", true}}));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Delete});

    drogon::app().registerHandler(
        "/api/tools/authenticator/entries",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            try
            {
                callback(JsonResponse(authenticator_service_.ListCodes()));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/tools/authenticator/entries",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback)) return;
            try
            {
                callback(JsonResponse({{"ok", true}, {"entry", authenticator_service_.SaveEntry(body)}}));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/tools/authenticator/entries",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            try
            {
                authenticator_service_.DeleteEntry(req->getParameter("id"));
                callback(JsonResponse({{"ok", true}}));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Delete});

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
        "/api/tools/time-manager/state",
        [this](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            callback(JsonResponse(config_store_.LoadTimeManagerState()));
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/tools/time-manager/state",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            const auto body = nlohmann::json::parse(req->body(), nullptr, false);
            if (body.is_discarded() || !body.is_object())
            {
                callback(JsonResponse({{"code", "invalid_json"}, {"message", "时间管理器状态不是合法 JSON。"}},
                                      drogon::k400BadRequest));
                return;
            }
            config_store_.SaveTimeManagerState(body);
            callback(JsonResponse({{"ok", true}}));
        },
        {drogon::Put});

    drogon::app().registerHandler(
        "/api/tools/habits/state",
        [this](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            callback(JsonResponse(config_store_.LoadHabitState()));
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/tools/habits/state",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback))
            {
                return;
            }
            config_store_.SaveHabitState(body);
            callback(JsonResponse({{"ok", true}}));
        },
        {drogon::Put});

    drogon::app().registerHandler(
        "/api/tools/terminal/snippets",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            auto response = JsonResponse({{"snippets", config_store_.LoadCommandSnippets()}});
            response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
            callback(response);
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/tools/terminal/snippets",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback)) return;
            try
            {
                callback(JsonResponse({{"ok", true}, {"snippet", config_store_.SaveCommandSnippet(body)}}));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Put});

    drogon::app().registerHandler(
        "/api/tools/terminal/snippets",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            const auto id = req->getParameter("id");
            if (id.empty())
            {
                callback(JsonResponse({{"ok", false}, {"message", "片段 id 不能为空。"}}, drogon::k400BadRequest));
                return;
            }
            if (!config_store_.DeleteCommandSnippet(id))
            {
                callback(JsonResponse({{"ok", false}, {"message", "共享片段不存在。"}}, drogon::k404NotFound));
                return;
            }
            callback(JsonResponse({{"ok", true}}));
        },
        {drogon::Delete});

    drogon::app().registerHandler(
        "/api/tools/transfer/state",
        [this](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            callback(JsonResponse(transfer_manager_.State()));
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/tools/transfer/server/config",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback))
            {
                return;
            }
            try
            {
                callback(JsonResponse(transfer_manager_.UpdateServerConfig(body)));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Put});

    drogon::app().registerHandler(
        "/api/tools/transfer/server/start",
        [this](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            try
            {
                callback(JsonResponse(transfer_manager_.StartServer()));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/tools/transfer/server/stop",
        [this](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            callback(JsonResponse(transfer_manager_.StopServer()));
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/tools/transfer/client/jobs",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback))
            {
                return;
            }
            try
            {
                callback(JsonResponse(transfer_manager_.StartClientJob(body), drogon::k202Accepted));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/tools/ssh/hosts",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback))
            {
                return;
            }
            auto hosts = config_store_.LoadSshHosts();
            for (auto& host : hosts)
            {
                if (host.is_object())
                {
                    host["hasCredential"] = config_store_.HasSshCredential(host.value("id", ""));
                }
            }
            callback(JsonResponse({{"hosts", hosts}}));
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/tools/ssh/hosts",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback))
            {
                return;
            }
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback))
            {
                return;
            }
            const auto hosts = body.value("hosts", nlohmann::json::array());
            if (!hosts.is_array())
            {
                callback(JsonResponse({{"code", "invalid_hosts"}, {"message", "SSH 主机必须是数组。"}},
                                      drogon::k400BadRequest));
                return;
            }
            try
            {
                config_store_.SaveSshHosts(hosts);
                callback(JsonResponse({{"ok", true}}));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Put});

    drogon::app().registerHandler(
        "/api/tools/ssh/credential",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback))
            {
                return;
            }
            const auto host_id = req->getParameter("hostId");
            if (host_id.empty())
            {
                callback(JsonResponse({{"code", "missing_host_id"}}, drogon::k400BadRequest));
                return;
            }
            config_store_.DeleteSshCredential(host_id);
            callback(JsonResponse({{"ok", true}}));
        },
        {drogon::Delete});

    drogon::app().registerHandler(
        "/api/tools/ssh/sftp/list",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            try
            {
                callback(JsonResponse(sftp_service_.List(req->getParameter("hostId"), req->getParameter("path"))));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/tools/ssh/sftp/folder",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback)) return;
            try
            {
                sftp_service_.CreateDirectory(body.value("hostId", ""), body.value("path", ""));
                callback(JsonResponse({{"ok", true}}));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/tools/ssh/sftp/item",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback)) return;
            try
            {
                sftp_service_.Remove(body.value("hostId", ""), body.value("path", ""),
                                     body.value("directory", false));
                callback(JsonResponse({{"ok", true}}));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Delete});

    drogon::app().registerHandler(
        "/api/tools/ssh/sftp/rename",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback)) return;
            try
            {
                sftp_service_.Rename(body.value("hostId", ""), body.value("from", ""), body.value("to", ""));
                callback(JsonResponse({{"ok", true}}));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Put});

    drogon::app().registerHandler(
        "/api/tools/ssh/sftp/upload",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            drogon::MultiPartParser parser;
            if (parser.parse(req) != 0)
            {
                callback(JsonResponse({{"code", "invalid_multipart"}}, drogon::k400BadRequest));
                return;
            }
            const auto host_id = parser.getParameter<std::string>("hostId");
            const auto upload_id = parser.getParameter<std::string>("uploadId");
            const auto path = parser.getParameter<std::string>("path");
            const auto& files = parser.getFiles();
            if (upload_id.empty() || host_id.empty() || files.size() != 1)
            {
                callback(JsonResponse({{"code", "missing_host_or_file"}}, drogon::k400BadRequest));
                return;
            }
            std::shared_ptr<std::function<void(const drogon::HttpResponsePtr&)>> response_callback;
            try
            {
                const auto& file = files.front();
                const auto filename = SanitizeFileName(file.getFileName());
                const auto destination = path.empty() || path == "/" ? "/" + filename : path + "/" + filename;
                const auto offset_text = parser.getParameter<std::string>("offset");
                const auto total_size_text = parser.getParameter<std::string>("totalSize");
                const auto offset = offset_text.empty() ? 0ULL : std::stoull(offset_text);
                const auto total_size = total_size_text.empty()
                                            ? static_cast<std::uint64_t>(file.fileLength())
                                            : std::stoull(total_size_text);
                auto content = std::string(file.fileData(), file.fileLength());
                const auto uploaded_size = content.size();
                response_callback = std::make_shared<std::function<void(const drogon::HttpResponsePtr&)>>(std::move(callback));
                sftp_service_.StartUploadChunk(
                    upload_id,
                    host_id,
                    destination,
                    offset,
                    total_size,
                    std::move(content),
                    [response_callback, offset, total_size, uploaded_size](const std::string& error) {
                        if (!error.empty())
                        {
                            (*response_callback)(JsonResponse({{"ok", false}, {"message", error}}, drogon::k400BadRequest));
                            return;
                        }
                        (*response_callback)(JsonResponse({{"ok", true},
                                                          {"uploadedBytes", offset + uploaded_size},
                                                          {"totalBytes", total_size}}));
                    });
            }
            catch (const std::exception& error)
            {
                auto response = JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest);
                if (response_callback) (*response_callback)(response);
                else callback(response);
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/tools/ssh/sftp/download",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            const auto host_id = req->getParameter("hostId");
            const auto path = req->getParameter("path");
            const auto download_id = req->getParameter("downloadId");
            if (download_id.empty() || host_id.empty() || path.empty())
            {
                callback(JsonResponse({{"ok", false}, {"message", "缺少 SFTP 主机或文件路径。"}},
                                      drogon::k400BadRequest));
                return;
            }
            auto response = drogon::HttpResponse::newAsyncStreamResponse(
                [this, download_id, host_id, path](drogon::ResponseStreamPtr stream) {
                    auto shared_stream = std::shared_ptr<drogon::ResponseStream>(stream.release());
                    try
                    {
                        sftp_service_.StartDownload(
                            download_id,
                            host_id,
                            path,
                            [shared_stream](std::string_view chunk) {
                                return shared_stream->send(std::string(chunk));
                            },
                            [shared_stream, path](const std::string& error) {
                                if (!error.empty())
                                {
                                    const auto log_message = "SFTP download failed for " + path + ": " + error;
                                    logE(log_message.c_str());
                                }
                                shared_stream->close();
                            });
                    }
                    catch (const std::exception& error)
                    {
                        const auto log_message = "SFTP download could not start for " + path + ": " + error.what();
                        logE(log_message.c_str());
                        shared_stream->close();
                    }
                },
                true);
            response->setContentTypeCode(drogon::CT_APPLICATION_OCTET_STREAM);
            response->addHeader("Content-Disposition",
                                "attachment; filename=\"" +
                                    SanitizeFileName(std::filesystem::path(path).filename().string()) + "\"");
            callback(response);
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/tools/ssh/sftp/download/status",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            callback(JsonResponse(sftp_service_.DownloadState(req->getParameter("downloadId"))));
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/tools/ssh/sftp/download/status",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            try
            {
                sftp_service_.RemoveDownload(req->getParameter("downloadId"));
                callback(JsonResponse({{"ok", true}}));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Delete});

    drogon::app().registerHandler(
        "/api/tools/ssh/forwards",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            callback(JsonResponse(sftp_service_.ForwardState()));
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/tools/ssh/forwards",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireSecureRequest(req, callback)) return;
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback)) return;
            try
            {
                callback(JsonResponse(sftp_service_.StartForward(body.value("hostId", ""), body.value("localPort", 0),
                                                                  body.value("remoteHost", ""), body.value("remotePort", 0))));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/tools/ssh/forwards/{1}",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
            if (!RequireSecureRequest(req, callback)) return;
            sftp_service_.StopForward(id);
            callback(JsonResponse({{"ok", true}}));
        },
        {drogon::Delete});

    drogon::app().registerHandler(
        "/api/tools/files/list",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireLoopbackRequest(req, callback)) return;
            try
            {
                auto response = JsonResponse(file_manager_service_.List(req->getParameter("path")));
                response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
                callback(response);
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/api/tools/files/folder",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireLoopbackRequest(req, callback)) return;
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback)) return;
            try
            {
                file_manager_service_.CreateDirectory(body.value("parent", ""), body.value("name", ""));
                callback(JsonResponse({{"ok", true}}));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/tools/files/rename",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireLoopbackRequest(req, callback)) return;
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback)) return;
            try
            {
                file_manager_service_.Rename(body.value("path", ""), body.value("name", ""));
                callback(JsonResponse({{"ok", true}}));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Put});

    drogon::app().registerHandler(
        "/api/tools/files/item",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireLoopbackRequest(req, callback)) return;
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback)) return;
            try
            {
                callback(JsonResponse({{"ok", true}, {"removed", file_manager_service_.Remove(body.value("path", ""))}}));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Delete});

    drogon::app().registerHandler(
        "/api/tools/files/transfer",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireLoopbackRequest(req, callback)) return;
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback)) return;
            try
            {
                const auto sources = body.value("sources", std::vector<std::string>{});
                callback(JsonResponse(file_manager_service_.Transfer(
                    sources,
                    body.value("destination", ""),
                    body.value("operation", "copy") == "move",
                    body.value("conflictPolicy", "error"))));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/tools/files/trash",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireLoopbackRequest(req, callback)) return;
            nlohmann::json body;
            if (!ParseJsonBody(req, body, callback)) return;
            try
            {
                callback(JsonResponse(file_manager_service_.MoveToTrash(
                    body.value("paths", std::vector<std::string>{}))));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/tools/files/upload",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireLoopbackRequest(req, callback)) return;
            drogon::MultiPartParser parser;
            if (parser.parse(req) != 0)
            {
                callback(JsonResponse({{"ok", false}, {"message", "上传请求格式无效。"}}, drogon::k400BadRequest));
                return;
            }
            const auto& files = parser.getFiles();
            if (files.size() != 1)
            {
                callback(JsonResponse({{"ok", false}, {"message", "每个上传分块必须包含一个文件。"}}, drogon::k400BadRequest));
                return;
            }
            try
            {
                const auto offset_text = parser.getParameter<std::string>("offset");
                const auto total_size_text = parser.getParameter<std::string>("totalSize");
                if (offset_text.empty() || total_size_text.empty())
                {
                    throw std::invalid_argument("上传请求缺少文件大小或分块偏移。");
                }
                const auto& file = files.front();
                callback(JsonResponse(file_manager_service_.WriteUploadChunk(
                    parser.getParameter<std::string>("uploadId"),
                    parser.getParameter<std::string>("directory"),
                    parser.getParameter<std::string>("fileName"),
                    std::stoull(offset_text),
                    std::stoull(total_size_text),
                    std::string_view(file.fileData(), file.fileLength()))));
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/api/tools/files/download",
        [this](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            if (!RequireLoopbackRequest(req, callback)) return;
            try
            {
                const auto path = file_manager_service_.DownloadPath(req->getParameter("path"));
                auto response = drogon::HttpResponse::newFileResponse(
                    path.string(), SanitizeFileName(path.filename().string()), drogon::CT_APPLICATION_OCTET_STREAM, "", req);
                response->addHeader("Cache-Control", "no-store");
                callback(response);
            }
            catch (const std::exception& error)
            {
                callback(JsonResponse({{"ok", false}, {"message", error.what()}}, drogon::k400BadRequest));
            }
        },
        {drogon::Get});

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
        response->setContentTypeCode(StaticAssetContentType(path));
        if (ShouldRevalidateStaticAsset(path))
        {
            response->addHeader("Cache-Control", "no-cache");
        }
        callback(response);
        return;
    }

    if (IsStaticAssetRequest(path))
    {
        if (const auto* asset = FindCurrentHashedAsset(path))
        {
            auto response = drogon::HttpResponse::newHttpResponse();
            response->setBody(std::string(reinterpret_cast<const char*>(asset->data), asset->size));
            response->setContentTypeCode(StaticAssetContentType(path));
            response->addHeader("Cache-Control", "no-cache");
            callback(response);
            return;
        }
        callback(drogon::HttpResponse::newNotFoundResponse());
        return;
    }

    if (const auto index_it = kEmbeddedAssets.find("/index.html"); index_it != kEmbeddedAssets.end())
    {
        auto response = drogon::HttpResponse::newHttpResponse();
        response->setBody(std::string(reinterpret_cast<const char*>(index_it->second.data), index_it->second.size));
        response->setContentTypeCode(drogon::CT_TEXT_HTML);
        response->addHeader("Cache-Control", "no-cache");
        callback(response);
        return;
    }

    callback(drogon::HttpResponse::newNotFoundResponse());
}
} // namespace spacestation
