#include "config/config_store.hpp"
#include "http/http_server.hpp"
#include "logging/logger.hpp"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <thread>

namespace
{
volatile std::sig_atomic_t g_stop_requested = 0;

void HandleSignal(int)
{
    g_stop_requested = 1;
}
} // namespace

int main()
{
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    spacestation::ConfigStore config_store;
    const auto config = config_store.Load();
    const auto log_path = std::filesystem::path(config.data_path) / "logs" / "space-station.log";
    spacestation::Logger::Instance().Init(spacestation::ParseLogLevel(config.log_level), log_path);
    logI("Starting Space Station");

    spacestation::HttpServer server(config_store, config);
    server.Start();
    logI("Backend listening");
    std::cout << "Space Station backend listening at " << server.UiUrl() << '\n';

    while (!g_stop_requested)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    server.Stop();
    logI("Stopping Space Station");
    spacestation::Logger::Instance().Shutdown();
    return 0;
}
