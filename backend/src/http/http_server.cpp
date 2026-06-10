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
        {"ClientCAFile", trusted_root_certificate_path},
        {"VerifyMode", "Require"},
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
    }
    drogon::app().addListener("0.0.0.0", port_, true, certificate_path_, private_key_path_, false, mutual_tls_config);
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
