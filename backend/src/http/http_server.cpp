#include "http/http_server.hpp"

#include "embedded_assets.hpp"
#include "logging/logger.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>

namespace spacestation
{
namespace
{
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
} // namespace

HttpServer::HttpServer(ConfigStore& config_store, std::uint16_t port) : port_(port), config_store_(config_store)
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
    drogon::app().addListener("127.0.0.1", port_);
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
    return "http://127.0.0.1:" + std::to_string(port_) + "/web/";
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
