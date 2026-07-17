#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace spacestation::transfer
{
inline constexpr int kProtocolVersion = 1;
inline constexpr std::size_t kDefaultChunkSize = 1024 * 1024;

struct ServerConfig
{
    std::string listen_address = "0.0.0.0";
    int tls_port = 9443;
    bool tls_enabled = true;
    bool plain_enabled = false;
    int plain_port = 9080;
    std::string certificate_path;
    std::string private_key_path;
    std::string client_ca_path;
    std::filesystem::path destination_root = "data/transfer/incoming";
    std::vector<std::filesystem::path> basis_roots;
    double same_name_match_threshold = 0.5;
    bool overwrite = true;
};

struct ClientConfig
{
    std::string host = "127.0.0.1";
    int port = 9443;
    bool tls_enabled = true;
    std::string certificate_path;
    std::string private_key_path;
    std::string server_ca_path;
    std::string server_name;
    std::size_t chunk_size = kDefaultChunkSize;
    std::string compression_mode = "chunk";
    std::vector<std::filesystem::path> files;
};

struct FileProgress
{
    std::string file_id;
    std::string path;
    std::string stage;
    std::uint64_t file_size = 0;
    std::uint64_t matched_bytes = 0;
    std::uint64_t uploaded_bytes = 0;
    std::uint64_t wire_bytes = 0;
    std::uint64_t compressed_chunks = 0;
    std::string compression_mode = "none";
    std::string error;
};

using ProgressCallback = std::function<void(const FileProgress&)>;

nlohmann::json ToJson(const ServerConfig& config);
nlohmann::json ToJson(const ClientConfig& config);
ServerConfig ServerConfigFromJson(const nlohmann::json& json, const std::filesystem::path& base = {});
ClientConfig ClientConfigFromJson(const nlohmann::json& json, const std::filesystem::path& base = {});

class Server
{
  public:
    explicit Server(ServerConfig config);
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    void Start();
    void Stop();
    bool Running() const;
    const ServerConfig& Config() const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class Client
{
  public:
    explicit Client(ClientConfig config);
    std::vector<FileProgress> Send(const ProgressCallback& callback = {});

  private:
    ClientConfig config_;
};
} // namespace spacestation::transfer
