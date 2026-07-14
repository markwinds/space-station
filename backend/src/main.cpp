#include "config/config_store.hpp"
#include "http/http_server.hpp"
#include "logging/logger.hpp"

#include <chrono>
#include <charconv>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace
{
volatile std::sig_atomic_t g_stop_requested = 0;

struct CommandLineOptions
{
    bool show_help = false;
    bool enable_http = false;
    std::optional<int> http_port;
};

void HandleSignal(int)
{
    g_stop_requested = 1;
}

void PrintUsage(std::ostream& output, std::string_view program)
{
    output << "用法: " << program << " [选项]\n"
           << "\n"
           << "选项:\n"
           << "  --http              仅本次运行启用 HTTP，使用配置中的 HTTP 端口\n"
           << "  --http-port <端口>  仅本次运行启用 HTTP，并指定监听端口（1-65535）\n"
           << "  -h, --help          显示此帮助信息\n";
}

int ParsePort(std::string_view value)
{
    int port = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
    if (error != std::errc{} || end != value.data() + value.size() || port < 1 || port > 65535)
    {
        throw std::invalid_argument("HTTP 端口必须是 1 到 65535 之间的整数: " + std::string(value));
    }
    return port;
}

CommandLineOptions ParseCommandLine(int argc, char* argv[])
{
    CommandLineOptions options;
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument(argv[index]);
        if (argument == "-h" || argument == "--help")
        {
            options.show_help = true;
        }
        else if (argument == "--http")
        {
            options.enable_http = true;
        }
        else if (argument == "--http-port")
        {
            if (++index >= argc)
            {
                throw std::invalid_argument("--http-port 后需要提供端口号");
            }
            options.http_port = ParsePort(argv[index]);
            options.enable_http = true;
        }
        else if (argument.starts_with("--http-port="))
        {
            options.http_port = ParsePort(argument.substr(std::string_view("--http-port=").size()));
            options.enable_http = true;
        }
        else
        {
            throw std::invalid_argument("未知命令行参数: " + std::string(argument));
        }
    }
    return options;
}
} // namespace

int main(int argc, char* argv[])
{
    CommandLineOptions options;
    try
    {
        options = ParseCommandLine(argc, argv);
    }
    catch (const std::invalid_argument& error)
    {
        std::cerr << "错误: " << error.what() << "\n\n";
        PrintUsage(std::cerr, argv[0]);
        return 2;
    }

    if (options.show_help)
    {
        PrintUsage(std::cout, argv[0]);
        return 0;
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    spacestation::ConfigStore config_store;
    auto config = config_store.Load();
    if (options.enable_http)
    {
        config.http_enabled = true;
        if (options.http_port)
        {
            config.http_port = *options.http_port;
        }
        if (config.http_port == config.port)
        {
            std::cerr << "错误: HTTP 端口不能与 HTTPS 端口相同: " << config.http_port << '\n';
            return 2;
        }
    }
    const auto log_path = std::filesystem::path(config.data_path) / "logs" / "space-station.log";
    spacestation::Logger::Instance().Init(spacestation::ParseLogLevel(config.log_level), log_path);
    logI("Starting Space Station");
    if (options.enable_http)
    {
        const auto http_log_message = "HTTP temporarily enabled on port " + std::to_string(config.http_port);
        logI(http_log_message.c_str());
    }

    spacestation::HttpServer server(config_store, config);
    server.Start();
    logI("Backend listening");
    std::cout << "Space Station backend listening at " << server.UiUrl() << '\n';
    if (options.enable_http)
    {
        std::cout << "Temporary HTTP access: http://127.0.0.1:" << config.http_port << "/web/\n";
    }

    while (!g_stop_requested)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    server.Stop();
    logI("Stopping Space Station");
    spacestation::Logger::Instance().Shutdown();
    return 0;
}
