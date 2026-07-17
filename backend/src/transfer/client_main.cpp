#include "transfer/transfer.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>

namespace
{
nlohmann::json LoadJson(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("无法读取配置文件: " + path.string());
    nlohmann::json json;
    input >> json;
    return json;
}

void Usage(const char* program)
{
    std::cout << "用法: " << program << " [--config client.json] [选项] <文件...>\n"
              << "  --host <地址> --port <端口> --tls / --plain --chunk-size <字节>\n"
              << "  --compression <chunk|stream>\n"
              << "  --cert <文件> --key <文件> --server-ca <文件> --server-name <名称>\n";
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
                config_path = argv[++index];
        }
        spacestation::transfer::ClientConfig config;
        if (!config_path.empty())
            config = spacestation::transfer::ClientConfigFromJson(LoadJson(config_path), config_path.parent_path());
        std::vector<std::filesystem::path> command_line_files;
        for (int index = 1; index < argc; ++index)
        {
            const std::string option = argv[index];
            auto value = [&]() -> std::string {
                if (++index >= argc)
                    throw std::runtime_error(option + " 缺少参数");
                return argv[index];
            };
            if (option == "--config")
                ++index;
            else if (option == "--host")
                config.host = value();
            else if (option == "--port")
                config.port = std::stoi(value());
            else if (option == "--plain")
                config.tls_enabled = false;
            else if (option == "--tls")
                config.tls_enabled = true;
            else if (option == "--chunk-size")
                config.chunk_size = std::stoull(value());
            else if (option == "--compression")
                config.compression_mode = value();
            else if (option == "--cert")
                config.certificate_path = value();
            else if (option == "--key")
                config.private_key_path = value();
            else if (option == "--server-ca")
                config.server_ca_path = value();
            else if (option == "--server-name")
                config.server_name = value();
            else if (option == "-h" || option == "--help")
            {
                Usage(argv[0]);
                return 0;
            }
            else if (option.starts_with("-"))
                throw std::runtime_error("未知参数: " + option);
            else
                command_line_files.emplace_back(option);
        }
        if (!command_line_files.empty())
            config.files = std::move(command_line_files);
        spacestation::transfer::Client client(std::move(config));
        const auto results = client.Send([](const auto& progress) {
            std::cout << nlohmann::json{{"fileId", progress.file_id},
                                       {"path", progress.path},
                                       {"stage", progress.stage},
                                       {"fileSize", progress.file_size},
                                       {"matchedBytes", progress.matched_bytes},
                                       {"uploadedBytes", progress.uploaded_bytes},
                                       {"wireBytes", progress.wire_bytes},
                                       {"compressedChunks", progress.compressed_chunks},
                                       {"compressionMode", progress.compression_mode},
                                       {"error", progress.error}}
                             .dump()
                      << '\n';
        });
        const auto failed = std::any_of(results.begin(), results.end(), [](const auto& item) {
            return !item.error.empty();
        });
        return failed ? 1 : 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "错误: " << error.what() << '\n';
        Usage(argv[0]);
        return 2;
    }
}
