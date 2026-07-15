#include "transfer/transfer.hpp"

#include <csignal>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

namespace
{
volatile std::sig_atomic_t stop_requested = 0;
void HandleSignal(int)
{
    stop_requested = 1;
}

nlohmann::json LoadJson(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input)
    {
        throw std::runtime_error("无法读取配置文件: " + path.string());
    }
    nlohmann::json json;
    input >> json;
    return json;
}

void Usage(const char* program)
{
    std::cout << "用法: " << program << " --config <server.json> [选项]\n"
              << "  --listen <地址> --tls-port <端口> --plain-port <端口>\n"
              << "  --tls / --no-tls --plain / --no-plain --destination <目录> --basis <目录>\n"
              << "  --cert <文件> --key <文件> --client-ca <文件>\n";
}
} // namespace

int main(int argc, char* argv[])
{
    try
    {
        std::filesystem::path config_path;
        for (int index = 1; index < argc; ++index)
        {
            if (std::string_view(argv[index]) == "--config" && index + 1 < argc)
            {
                config_path = argv[++index];
            }
        }
        spacestation::transfer::ServerConfig config;
        if (!config_path.empty())
        {
            config = spacestation::transfer::ServerConfigFromJson(LoadJson(config_path), config_path.parent_path());
        }
        bool basis_overridden = false;
        for (int index = 1; index < argc; ++index)
        {
            const std::string option = argv[index];
            auto value = [&]() -> std::string {
                if (++index >= argc)
                {
                    throw std::runtime_error(option + " 缺少参数");
                }
                return argv[index];
            };
            if (option == "--config")
                ++index;
            else if (option == "--listen")
                config.listen_address = value();
            else if (option == "--tls-port")
                config.tls_port = std::stoi(value());
            else if (option == "--plain-port")
                config.plain_port = std::stoi(value());
            else if (option == "--plain")
                config.plain_enabled = true;
            else if (option == "--no-plain")
                config.plain_enabled = false;
            else if (option == "--tls")
                config.tls_enabled = true;
            else if (option == "--no-tls")
                config.tls_enabled = false;
            else if (option == "--destination")
                config.destination_root = value();
            else if (option == "--basis")
            {
                if (!basis_overridden)
                {
                    config.basis_roots.clear();
                    basis_overridden = true;
                }
                config.basis_roots.push_back(value());
            }
            else if (option == "--cert")
                config.certificate_path = value();
            else if (option == "--key")
                config.private_key_path = value();
            else if (option == "--client-ca")
                config.client_ca_path = value();
            else if (option == "--match-threshold")
                config.same_name_match_threshold = std::stod(value());
            else if (option == "--no-overwrite")
                config.overwrite = false;
            else if (option == "-h" || option == "--help")
            {
                Usage(argv[0]);
                return 0;
            }
            else
                throw std::runtime_error("未知参数: " + option);
        }
        std::signal(SIGINT, HandleSignal);
        std::signal(SIGTERM, HandleSignal);
        spacestation::transfer::Server server(std::move(config));
        server.Start();
        std::cout << "差分传输服务端已启动\n";
        while (!stop_requested)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        server.Stop();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "错误: " << error.what() << '\n';
        Usage(argv[0]);
        return 2;
    }
}
