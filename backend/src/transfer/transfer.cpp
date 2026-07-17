#include "transfer/transfer.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <climits>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
using SocketLength = int;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
using SocketLength = socklen_t;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace spacestation::transfer
{
namespace
{
constexpr std::uint32_t kMaxJsonFrame = 64U * 1024U * 1024U;
constexpr std::uint64_t kMaxChunkBody = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxStreamFrameBody = 1024ULL * 1024ULL;
constexpr std::size_t kStreamBufferSize = 64ULL * 1024ULL;
constexpr std::uint64_t kMaxFileSize = 4ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxChunkCount = 1'000'000;
constexpr auto kZlibCompression = "zlib";

void CloseSocket(SocketHandle socket)
{
    if (socket == kInvalidSocket)
    {
        return;
    }
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

void ShutdownSocket(SocketHandle socket)
{
#ifdef _WIN32
    shutdown(socket, SD_BOTH);
#else
    shutdown(socket, SHUT_RDWR);
#endif
}

std::filesystem::path ResolvePath(const std::filesystem::path& path, const std::filesystem::path& base)
{
    if (path.empty() || path.is_absolute() || base.empty())
    {
        return path.lexically_normal();
    }
    return (base / path).lexically_normal();
}

std::string RandomId()
{
    std::array<unsigned char, 16> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1)
    {
        throw std::runtime_error("无法生成传输会话 ID");
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : bytes)
    {
        output << std::setw(2) << static_cast<int>(byte);
    }
    return output.str();
}

std::string Hex(const unsigned char* data, std::size_t size)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string result(size * 2, '0');
    for (std::size_t index = 0; index < size; ++index)
    {
        result[index * 2] = digits[data[index] >> 4];
        result[index * 2 + 1] = digits[data[index] & 0x0f];
    }
    return result;
}

std::string Sha256(const unsigned char* data, std::size_t size)
{
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0;
    auto* context = EVP_MD_CTX_new();
    if (!context || EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(context, data, size) != 1 || EVP_DigestFinal_ex(context, digest.data(), &digest_size) != 1)
    {
        EVP_MD_CTX_free(context);
        throw std::runtime_error("SHA-256 计算失败");
    }
    EVP_MD_CTX_free(context);
    return Hex(digest.data(), digest_size);
}

std::string Sha256File(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("无法读取文件进行 SHA-256 校验: " + path.string());
    }
    auto* context = EVP_MD_CTX_new();
    if (!context || EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1)
    {
        EVP_MD_CTX_free(context);
        throw std::runtime_error("SHA-256 初始化失败");
    }
    std::vector<char> buffer(1024 * 1024);
    while (input)
    {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0 && EVP_DigestUpdate(context, buffer.data(), static_cast<std::size_t>(count)) != 1)
        {
            EVP_MD_CTX_free(context);
            throw std::runtime_error("SHA-256 计算失败");
        }
    }
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0;
    if (EVP_DigestFinal_ex(context, digest.data(), &digest_size) != 1)
    {
        EVP_MD_CTX_free(context);
        throw std::runtime_error("SHA-256 计算失败");
    }
    EVP_MD_CTX_free(context);
    return Hex(digest.data(), digest_size);
}

std::vector<unsigned char> TryCompressZlib(const unsigned char* data, std::size_t size)
{
    if (size == 0 || size > static_cast<std::size_t>(std::numeric_limits<uLong>::max()))
    {
        return {};
    }
    uLongf compressed_size = compressBound(static_cast<uLong>(size));
    std::vector<unsigned char> compressed(static_cast<std::size_t>(compressed_size));
    const auto result = compress2(compressed.data(),
                                  &compressed_size,
                                  data,
                                  static_cast<uLong>(size),
                                  Z_BEST_SPEED);
    if (result != Z_OK || compressed_size >= size)
    {
        return {};
    }
    compressed.resize(static_cast<std::size_t>(compressed_size));
    return compressed;
}

std::vector<unsigned char> DecompressZlib(const unsigned char* data,
                                          std::size_t size,
                                          std::size_t expected_size)
{
    if (expected_size == 0 || expected_size > static_cast<std::size_t>(std::numeric_limits<uLongf>::max()))
    {
        throw std::runtime_error("压缩块的原始长度无效");
    }
    std::vector<unsigned char> output(expected_size);
    uLongf output_size = static_cast<uLongf>(expected_size);
    const auto result = uncompress(output.data(),
                                   &output_size,
                                   data,
                                   static_cast<uLong>(size));
    if (result != Z_OK || output_size != expected_size)
    {
        throw std::runtime_error("zlib 块解压失败或长度不匹配");
    }
    return output;
}

class DeflateStream
{
  public:
    DeflateStream()
    {
        if (deflateInit(&stream_, Z_BEST_SPEED) != Z_OK)
        {
            throw std::runtime_error("无法初始化 zlib 连续压缩流");
        }
        initialized_ = true;
    }

    ~DeflateStream()
    {
        if (initialized_)
        {
            deflateEnd(&stream_);
        }
    }

    z_stream& Get()
    {
        return stream_;
    }

  private:
    z_stream stream_{};
    bool initialized_ = false;
};

class InflateStream
{
  public:
    InflateStream()
    {
        if (inflateInit(&stream_) != Z_OK)
        {
            throw std::runtime_error("无法初始化 zlib 连续解压流");
        }
        initialized_ = true;
    }

    ~InflateStream()
    {
        if (initialized_)
        {
            inflateEnd(&stream_);
        }
    }

    z_stream& Get()
    {
        return stream_;
    }

  private:
    z_stream stream_{};
    bool initialized_ = false;
};

class Sha256Accumulator
{
  public:
    Sha256Accumulator() : context_(EVP_MD_CTX_new(), EVP_MD_CTX_free)
    {
        if (!context_)
        {
            throw std::runtime_error("SHA-256 初始化失败");
        }
        Reset();
    }

    void Reset()
    {
        if (EVP_DigestInit_ex(context_.get(), EVP_sha256(), nullptr) != 1)
        {
            throw std::runtime_error("SHA-256 初始化失败");
        }
    }

    void Update(const unsigned char* data, std::size_t size)
    {
        if (EVP_DigestUpdate(context_.get(), data, size) != 1)
        {
            throw std::runtime_error("SHA-256 计算失败");
        }
    }

    std::string Finish()
    {
        std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
        unsigned int size = 0;
        if (EVP_DigestFinal_ex(context_.get(), digest.data(), &size) != 1)
        {
            throw std::runtime_error("SHA-256 计算失败");
        }
        return Hex(digest.data(), size);
    }

  private:
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context_;
};

std::uint32_t WeakChecksum(const unsigned char* data, std::size_t size)
{
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    for (std::size_t index = 0; index < size; ++index)
    {
        a = (a + data[index]) & 0xffffU;
        b = (b + static_cast<std::uint32_t>((size - index) * data[index])) & 0xffffU;
    }
    return (b << 16U) | a;
}

std::string SafeFileName(const std::string& input)
{
    if (input.empty() || input == "." || input == ".." ||
        std::any_of(input.begin(), input.end(), [](unsigned char character) {
            return character < 0x20 || character == '/' || character == '\\' || character == ':';
        }))
    {
        throw std::runtime_error("目标文件名无效");
    }
    return input;
}

bool IsSafeIdentifier(const std::string& value)
{
    return value.size() >= 8 && value.size() <= 64 &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isalnum(character) || character == '-' || character == '_';
           });
}

class TemporaryFileGuard
{
  public:
    explicit TemporaryFileGuard(std::filesystem::path path) : path_(std::move(path))
    {
    }

    ~TemporaryFileGuard()
    {
        if (!released_)
        {
            std::error_code error;
            std::filesystem::remove(path_, error);
        }
    }

    void Release()
    {
        released_ = true;
    }

  private:
    std::filesystem::path path_;
    bool released_ = false;
};

class Connection
{
  public:
    Connection(SocketHandle socket, SSL* ssl) : socket_(socket), ssl_(ssl)
    {
    }

    ~Connection()
    {
        if (ssl_)
        {
            SSL_shutdown(ssl_);
            SSL_free(ssl_);
        }
        CloseSocket(socket_);
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    SocketHandle Socket() const
    {
        return socket_;
    }

    void WriteAll(const void* source, std::size_t size)
    {
        const auto* data = static_cast<const unsigned char*>(source);
        while (size > 0)
        {
            const auto count = ssl_ ? SSL_write(ssl_, data, static_cast<int>(std::min<std::size_t>(size, INT_MAX)))
#ifdef _WIN32
                                    : send(socket_, reinterpret_cast<const char*>(data), static_cast<int>(size), 0);
#else
                                    : send(socket_, data, size, 0);
#endif
            if (count <= 0)
            {
                throw std::runtime_error("传输连接写入失败");
            }
            data += count;
            size -= static_cast<std::size_t>(count);
        }
    }

    void ReadAll(void* destination, std::size_t size)
    {
        auto* data = static_cast<unsigned char*>(destination);
        while (size > 0)
        {
            const auto count = ssl_ ? SSL_read(ssl_, data, static_cast<int>(std::min<std::size_t>(size, INT_MAX)))
#ifdef _WIN32
                                    : recv(socket_, reinterpret_cast<char*>(data), static_cast<int>(size), 0);
#else
                                    : recv(socket_, data, size, 0);
#endif
            if (count <= 0)
            {
                throw std::runtime_error("传输连接已关闭");
            }
            data += count;
            size -= static_cast<std::size_t>(count);
        }
    }

    void SendJson(const nlohmann::json& json)
    {
        const auto payload = json.dump();
        if (payload.size() > kMaxJsonFrame)
        {
            throw std::runtime_error("协议 JSON 帧过大");
        }
        const auto size = htonl(static_cast<std::uint32_t>(payload.size()));
        WriteAll(&size, sizeof(size));
        WriteAll(payload.data(), payload.size());
    }

    nlohmann::json ReceiveJson()
    {
        std::uint32_t encoded_size = 0;
        ReadAll(&encoded_size, sizeof(encoded_size));
        const auto size = ntohl(encoded_size);
        if (size == 0 || size > kMaxJsonFrame)
        {
            throw std::runtime_error("协议 JSON 帧长度无效");
        }
        std::string payload(size, '\0');
        ReadAll(payload.data(), payload.size());
        auto result = nlohmann::json::parse(payload, nullptr, false);
        if (result.is_discarded() || !result.is_object())
        {
            throw std::runtime_error("协议 JSON 帧无效");
        }
        return result;
    }

  private:
    SocketHandle socket_;
    SSL* ssl_;
};

SocketHandle ConnectSocket(const std::string& host, int port)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    const auto service = std::to_string(port);
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses) != 0)
    {
        throw std::runtime_error("无法解析服务端地址: " + host);
    }
    SocketHandle result = kInvalidSocket;
    for (auto* address = addresses; address; address = address->ai_next)
    {
        auto socket = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket == kInvalidSocket)
        {
            continue;
        }
        if (::connect(socket, address->ai_addr, static_cast<SocketLength>(address->ai_addrlen)) == 0)
        {
            result = socket;
            break;
        }
        CloseSocket(socket);
    }
    freeaddrinfo(addresses);
    if (result == kInvalidSocket)
    {
        throw std::runtime_error("无法连接服务端 " + host + ":" + service);
    }
    return result;
}

SocketHandle CreateListener(const std::string& host, int port)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    addrinfo* addresses = nullptr;
    const auto service = std::to_string(port);
    const char* node = host.empty() || host == "*" ? nullptr : host.c_str();
    if (getaddrinfo(node, service.c_str(), &hints, &addresses) != 0)
    {
        throw std::runtime_error("无法解析监听地址: " + host);
    }
    SocketHandle result = kInvalidSocket;
    for (auto* address = addresses; address; address = address->ai_next)
    {
        auto socket = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket == kInvalidSocket)
        {
            continue;
        }
        int enabled = 1;
        setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
        if (bind(socket, address->ai_addr, static_cast<SocketLength>(address->ai_addrlen)) == 0 &&
            listen(socket, 64) == 0)
        {
            result = socket;
            break;
        }
        CloseSocket(socket);
    }
    freeaddrinfo(addresses);
    if (result == kInvalidSocket)
    {
        throw std::runtime_error("无法监听 " + host + ":" + service);
    }
    return result;
}

std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> CreateServerTlsContext(const ServerConfig& config)
{
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context(SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
    if (!context || SSL_CTX_set_min_proto_version(context.get(), TLS1_3_VERSION) != 1 ||
        SSL_CTX_set_max_proto_version(context.get(), TLS1_3_VERSION) != 1 ||
        SSL_CTX_use_certificate_chain_file(context.get(), config.certificate_path.c_str()) != 1 ||
        SSL_CTX_use_PrivateKey_file(context.get(), config.private_key_path.c_str(), SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(context.get()) != 1 ||
        SSL_CTX_load_verify_locations(context.get(), config.client_ca_path.c_str(), nullptr) != 1)
    {
        throw std::runtime_error("传输服务端 TLS 1.3/mTLS 配置无效");
    }
    SSL_CTX_set_verify(context.get(), SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
    SSL_CTX_set_verify_depth(context.get(), 8);
    return context;
}

std::unique_ptr<Connection> Connect(const ClientConfig& config,
                                    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>& tls_context)
{
    auto socket = ConnectSocket(config.host, config.port);
    if (!config.tls_enabled)
    {
        return std::make_unique<Connection>(socket, nullptr);
    }
    tls_context.reset(SSL_CTX_new(TLS_client_method()));
    if (!tls_context || SSL_CTX_set_min_proto_version(tls_context.get(), TLS1_3_VERSION) != 1 ||
        SSL_CTX_set_max_proto_version(tls_context.get(), TLS1_3_VERSION) != 1 ||
        SSL_CTX_use_certificate_chain_file(tls_context.get(), config.certificate_path.c_str()) != 1 ||
        SSL_CTX_use_PrivateKey_file(tls_context.get(), config.private_key_path.c_str(), SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(tls_context.get()) != 1 ||
        SSL_CTX_load_verify_locations(tls_context.get(), config.server_ca_path.c_str(), nullptr) != 1)
    {
        CloseSocket(socket);
        throw std::runtime_error("传输客户端 TLS 1.3/mTLS 配置无效");
    }
    SSL_CTX_set_verify(tls_context.get(), SSL_VERIFY_PEER, nullptr);
    auto* ssl = SSL_new(tls_context.get());
    if (!ssl)
    {
        CloseSocket(socket);
        throw std::runtime_error("无法创建 TLS 连接");
    }
    SSL_set_fd(ssl, static_cast<int>(socket));
    const auto verify_name = config.server_name.empty() ? config.host : config.server_name;
    in_addr address4{};
    in6_addr address6{};
    auto* parameters = SSL_get0_param(ssl);
    if (inet_pton(AF_INET, verify_name.c_str(), &address4) == 1 || inet_pton(AF_INET6, verify_name.c_str(), &address6) == 1)
    {
        X509_VERIFY_PARAM_set1_ip_asc(parameters, verify_name.c_str());
    }
    else
    {
        SSL_set_tlsext_host_name(ssl, verify_name.c_str());
        X509_VERIFY_PARAM_set1_host(parameters, verify_name.c_str(), 0);
    }
    if (SSL_connect(ssl) != 1)
    {
        SSL_free(ssl);
        CloseSocket(socket);
        throw std::runtime_error("TLS 握手或服务端证书校验失败");
    }
    return std::make_unique<Connection>(socket, ssl);
}

struct Signature
{
    std::uint32_t weak = 0;
    std::string strong;
    std::size_t length = 0;
};

std::vector<Signature> BuildSignatures(const std::filesystem::path& path, std::size_t chunk_size)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("无法读取待传输文件: " + path.string());
    }
    std::vector<Signature> result;
    std::vector<unsigned char> buffer(chunk_size);
    while (input)
    {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const auto count = static_cast<std::size_t>(input.gcount());
        if (count == 0)
        {
            break;
        }
        result.push_back({WeakChecksum(buffer.data(), count), Sha256(buffer.data(), count), count});
    }
    return result;
}

bool AllMatched(const std::vector<bool>& matched)
{
    return std::all_of(matched.begin(), matched.end(), [](bool value) { return value; });
}

void ScanCandidate(const std::filesystem::path& candidate,
                   const std::vector<Signature>& signatures,
                   std::size_t chunk_size,
                   std::vector<bool>& matched,
                   std::fstream& output,
                   std::uint64_t& matched_bytes)
{
    std::map<std::size_t, std::unordered_map<std::uint32_t, std::vector<std::size_t>>> lookups;
    for (std::size_t index = 0; index < signatures.size(); ++index)
    {
        if (!matched[index])
        {
            lookups[signatures[index].length][signatures[index].weak].push_back(index);
        }
    }
    for (const auto& [window_size, lookup] : lookups)
    {
        if (window_size == 0 || AllMatched(matched))
        {
            continue;
        }
        std::ifstream input(candidate, std::ios::binary);
        if (!input)
        {
            continue;
        }
        std::vector<unsigned char> window(window_size);
        input.read(reinterpret_cast<char*>(window.data()), static_cast<std::streamsize>(window.size()));
        if (static_cast<std::size_t>(input.gcount()) != window.size())
        {
            continue;
        }
        auto weak = WeakChecksum(window.data(), window.size());
        std::size_t ring_start = 0;
        while (true)
        {
            const auto found = lookup.find(weak);
            if (found != lookup.end())
            {
                std::vector<unsigned char> ordered(window_size);
                for (std::size_t index = 0; index < window_size; ++index)
                {
                    ordered[index] = window[(ring_start + index) % window_size];
                }
                const auto strong = Sha256(ordered.data(), ordered.size());
                for (const auto target_index : found->second)
                {
                    if (!matched[target_index] && signatures[target_index].strong == strong)
                    {
                        output.seekp(static_cast<std::streamoff>(target_index * chunk_size));
                        output.write(reinterpret_cast<const char*>(ordered.data()),
                                     static_cast<std::streamsize>(ordered.size()));
                        if (!output)
                        {
                            throw std::runtime_error("写入差分匹配数据失败");
                        }
                        matched[target_index] = true;
                        matched_bytes += ordered.size();
                    }
                }
            }
            char next_char = 0;
            if (!input.get(next_char))
            {
                break;
            }
            const auto old = window[ring_start];
            const auto next = static_cast<unsigned char>(next_char);
            auto a = weak & 0xffffU;
            auto b = weak >> 16U;
            a = (a + 0x10000U - old + next) & 0xffffU;
            b = (b + 0x10000U - static_cast<std::uint32_t>((window_size * old) & 0xffffU) + a) & 0xffffU;
            weak = (b << 16U) | a;
            window[ring_start] = next;
            ring_start = (ring_start + 1) % window_size;
        }
    }
}

std::vector<std::filesystem::path> CandidateFiles(const ServerConfig& config, const std::string& name)
{
    std::vector<std::filesystem::path> same_name;
    std::vector<std::filesystem::path> other;
    std::set<std::filesystem::path> seen;
    const auto destination = (config.destination_root / name).lexically_normal();
    std::error_code ec;
    if (std::filesystem::is_regular_file(destination, ec))
    {
        same_name.push_back(destination);
        seen.insert(std::filesystem::weakly_canonical(destination, ec));
    }
    for (const auto& root : config.basis_roots)
    {
        ec.clear();
        if (!std::filesystem::is_directory(root, ec))
        {
            continue;
        }
        for (std::filesystem::recursive_directory_iterator iterator(
                 root, std::filesystem::directory_options::skip_permission_denied, ec),
             end;
             iterator != end; iterator.increment(ec))
        {
            if (ec)
            {
                ec.clear();
                continue;
            }
            if (!iterator->is_regular_file(ec) || iterator->is_symlink(ec))
            {
                continue;
            }
            const auto canonical = std::filesystem::weakly_canonical(iterator->path(), ec);
            if (!seen.insert(canonical).second)
            {
                continue;
            }
            (iterator->path().filename() == name ? same_name : other).push_back(iterator->path());
        }
    }
    same_name.insert(same_name.end(), other.begin(), other.end());
    return same_name;
}

std::size_t CountSameName(const std::vector<std::filesystem::path>& candidates, const std::string& name)
{
    return static_cast<std::size_t>(std::count_if(candidates.begin(), candidates.end(), [&](const auto& path) {
        return path.filename() == name;
    }));
}

void ValidateServerConfig(const ServerConfig& config)
{
    if (!config.tls_enabled && !config.plain_enabled)
    {
        throw std::runtime_error("TLS 和普通端口不能同时关闭");
    }
    if (config.tls_enabled &&
        (config.certificate_path.empty() || config.private_key_path.empty() || config.client_ca_path.empty()))
    {
        throw std::runtime_error("TLS 服务端必须配置证书、私钥和客户端 CA");
    }
    if (config.same_name_match_threshold < 0.0 || config.same_name_match_threshold > 1.0)
    {
        throw std::runtime_error("同名文件匹配阈值必须在 0 到 1 之间");
    }
    if (config.tls_port < 1 || config.tls_port > 65535 || config.plain_port < 1 || config.plain_port > 65535 ||
        (config.tls_enabled && config.plain_enabled && config.tls_port == config.plain_port))
    {
        throw std::runtime_error("服务端监听端口无效或重复");
    }
}

void ValidateClientConfig(const ClientConfig& config)
{
    if (config.host.empty() || config.port < 1 || config.port > 65535)
    {
        throw std::runtime_error("客户端目标地址或端口无效");
    }
    if (config.chunk_size < 64 * 1024 || config.chunk_size > 16 * 1024 * 1024)
    {
        throw std::runtime_error("块大小必须在 64 KiB 到 16 MiB 之间");
    }
    if (config.compression_mode != "chunk" && config.compression_mode != "stream")
    {
        throw std::runtime_error("压缩方式必须是 chunk 或 stream");
    }
    if (config.tls_enabled &&
        (config.certificate_path.empty() || config.private_key_path.empty() || config.server_ca_path.empty()))
    {
        throw std::runtime_error("TLS 客户端必须配置证书、私钥和服务端 CA");
    }
    if (config.files.empty())
    {
        throw std::runtime_error("至少需要指定一个待传输文件");
    }
}
} // namespace

nlohmann::json ToJson(const ServerConfig& config)
{
    auto roots = nlohmann::json::array();
    for (const auto& root : config.basis_roots)
    {
        roots.push_back(root.string());
    }
    return {{"configVersion", 1},
            {"listenAddress", config.listen_address},
            {"tlsEnabled", config.tls_enabled},
            {"tlsPort", config.tls_port},
            {"plainEnabled", config.plain_enabled},
            {"plainPort", config.plain_port},
            {"certificatePath", config.certificate_path},
            {"privateKeyPath", config.private_key_path},
            {"clientCaPath", config.client_ca_path},
            {"destinationRoot", config.destination_root.string()},
            {"basisRoots", roots},
            {"sameNameMatchThreshold", config.same_name_match_threshold},
            {"overwrite", config.overwrite}};
}

nlohmann::json ToJson(const ClientConfig& config)
{
    auto files = nlohmann::json::array();
    for (const auto& file : config.files)
    {
        files.push_back(file.string());
    }
    return {{"configVersion", 1},
            {"host", config.host},
            {"port", config.port},
            {"tlsEnabled", config.tls_enabled},
            {"certificatePath", config.certificate_path},
            {"privateKeyPath", config.private_key_path},
            {"serverCaPath", config.server_ca_path},
            {"serverName", config.server_name},
            {"chunkSize", config.chunk_size},
            {"compressionMode", config.compression_mode},
            {"files", files}};
}

ServerConfig ServerConfigFromJson(const nlohmann::json& json, const std::filesystem::path& base)
{
    if (json.contains("configVersion") && json.value("configVersion", 0) != 1)
    {
        throw std::runtime_error("不支持的服务端配置版本");
    }
    ServerConfig result;
    result.listen_address = json.value("listenAddress", result.listen_address);
    result.tls_enabled = json.value("tlsEnabled", result.tls_enabled);
    result.tls_port = json.value("tlsPort", result.tls_port);
    result.plain_enabled = json.value("plainEnabled", result.plain_enabled);
    result.plain_port = json.value("plainPort", result.plain_port);
    result.certificate_path = ResolvePath(json.value("certificatePath", ""), base).string();
    result.private_key_path = ResolvePath(json.value("privateKeyPath", ""), base).string();
    result.client_ca_path = ResolvePath(json.value("clientCaPath", ""), base).string();
    result.destination_root = ResolvePath(json.value("destinationRoot", result.destination_root.string()), base);
    result.same_name_match_threshold = json.value("sameNameMatchThreshold", result.same_name_match_threshold);
    result.overwrite = json.value("overwrite", result.overwrite);
    if (const auto roots = json.find("basisRoots"); roots != json.end() && roots->is_array())
    {
        for (const auto& root : *roots)
        {
            if (root.is_string() && !root.get<std::string>().empty())
            {
                result.basis_roots.push_back(ResolvePath(root.get<std::string>(), base));
            }
        }
    }
    return result;
}

ClientConfig ClientConfigFromJson(const nlohmann::json& json, const std::filesystem::path& base)
{
    if (json.contains("configVersion") && json.value("configVersion", 0) != 1)
    {
        throw std::runtime_error("不支持的客户端配置版本");
    }
    ClientConfig result;
    result.host = json.value("host", result.host);
    result.port = json.value("port", result.port);
    result.tls_enabled = json.value("tlsEnabled", result.tls_enabled);
    result.certificate_path = ResolvePath(json.value("certificatePath", ""), base).string();
    result.private_key_path = ResolvePath(json.value("privateKeyPath", ""), base).string();
    result.server_ca_path = ResolvePath(json.value("serverCaPath", ""), base).string();
    result.server_name = json.value("serverName", "");
    result.chunk_size = json.value("chunkSize", result.chunk_size);
    result.compression_mode = json.value("compressionMode", result.compression_mode);
    if (const auto files = json.find("files"); files != json.end() && files->is_array())
    {
        for (const auto& file : *files)
        {
            if (file.is_string() && !file.get<std::string>().empty())
            {
                result.files.push_back(ResolvePath(file.get<std::string>(), base));
            }
        }
    }
    return result;
}

class Server::Impl
{
  public:
    explicit Impl(ServerConfig config) : config_(std::move(config))
    {
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            throw std::runtime_error("Winsock 初始化失败");
        }
#endif
#ifndef _WIN32
        std::signal(SIGPIPE, SIG_IGN);
#endif
    }

    ~Impl()
    {
        Stop();
#ifdef _WIN32
        WSACleanup();
#endif
    }

    void Start()
    {
        if (running_.exchange(true))
        {
            return;
        }
        try
        {
            ValidateServerConfig(config_);
            std::filesystem::create_directories(config_.destination_root);
            if (config_.tls_enabled)
            {
                tls_context_ = CreateServerTlsContext(config_);
                AddListener(config_.tls_port, true);
            }
            if (config_.plain_enabled)
            {
                AddListener(config_.plain_port, false);
            }
        }
        catch (...)
        {
            running_ = false;
            Stop();
            throw;
        }
    }

    void Stop()
    {
        running_ = false;
        for (const auto socket : listeners_)
        {
            ShutdownSocket(socket);
            CloseSocket(socket);
        }
        listeners_.clear();
        {
            std::lock_guard lock(active_mutex_);
            for (const auto socket : active_sockets_)
            {
                ShutdownSocket(socket);
            }
        }
        for (auto& thread : accept_threads_)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
        accept_threads_.clear();
        for (auto& thread : worker_threads_)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
        worker_threads_.clear();
        tls_context_.reset();
    }

    bool Running() const
    {
        return running_;
    }

    const ServerConfig& Config() const
    {
        return config_;
    }

  private:
    void AddListener(int port, bool tls)
    {
        const auto listener = CreateListener(config_.listen_address, port);
        listeners_.push_back(listener);
        accept_threads_.emplace_back([this, listener, tls] { AcceptLoop(listener, tls); });
    }

    void AcceptLoop(SocketHandle listener, bool tls)
    {
        while (running_)
        {
            sockaddr_storage address{};
            SocketLength size = sizeof(address);
            const auto socket = accept(listener, reinterpret_cast<sockaddr*>(&address), &size);
            if (socket == kInvalidSocket)
            {
                if (!running_)
                {
                    break;
                }
                continue;
            }
            {
                std::lock_guard lock(active_mutex_);
                active_sockets_.insert(socket);
                worker_threads_.emplace_back([this, socket, tls] {
                    bool connection_owned = false;
                    try
                    {
                        SSL* ssl = nullptr;
                        if (tls)
                        {
                            ssl = SSL_new(tls_context_.get());
                            if (!ssl)
                            {
                                throw std::runtime_error("无法创建服务端 TLS 会话");
                            }
                            SSL_set_fd(ssl, static_cast<int>(socket));
                            if (SSL_accept(ssl) != 1)
                            {
                                SSL_free(ssl);
                                ssl = nullptr;
                                throw std::runtime_error("客户端 mTLS 校验失败");
                            }
                        }
                        Connection connection(socket, ssl);
                        connection_owned = true;
                        HandleConnection(connection);
                    }
                    catch (const std::exception& error)
                    {
                        std::cerr << "transfer server: " << error.what() << '\n';
                        // Connection owns successfully negotiated sockets. Handshake failures are closed below.
                        std::lock_guard lock(active_mutex_);
                        if (!connection_owned && active_sockets_.contains(socket))
                        {
                            CloseSocket(socket);
                        }
                    }
                    std::lock_guard lock(active_mutex_);
                    active_sockets_.erase(socket);
                });
            }
        }
    }

    void HandleConnection(Connection& connection)
    {
        const auto hello = connection.ReceiveJson();
        if (hello.value("type", "") != "hello" || hello.value("protocolVersion", 0) != kProtocolVersion)
        {
            throw std::runtime_error("客户端协议版本不受支持");
        }
        connection.SendJson({{"type", "hello"}, {"ok", true}, {"protocolVersion", kProtocolVersion}});
        while (running_)
        {
            const auto request = connection.ReceiveJson();
            const auto type = request.value("type", "");
            if (type == "goodbye")
            {
                return;
            }
            if (type != "startFile")
            {
                throw std::runtime_error("不支持的传输请求");
            }
            try
            {
                HandleFile(connection, request);
            }
            catch (const std::exception& error)
            {
                try
                {
                    connection.SendJson({{"type", "error"},
                                         {"sessionId", request.value("sessionId", "")},
                                         {"fileId", request.value("fileId", "")},
                                         {"message", error.what()}});
                }
                catch (...)
                {
                }
                return;
            }
        }
    }

    void HandleFile(Connection& connection, const nlohmann::json& request)
    {
        const auto session_id = request.value("sessionId", "");
        const auto file_id = request.value("fileId", "");
        const auto name = SafeFileName(request.value("name", ""));
        const auto file_size = request.value("fileSize", std::uint64_t{0});
        const auto chunk_size = request.value("chunkSize", std::size_t{0});
        const auto full_hash = request.value("fullHash", "");
        const auto signature_json = request.value("signatures", nlohmann::json::array());
        if (!IsSafeIdentifier(session_id) || !IsSafeIdentifier(file_id) || chunk_size < 64 * 1024 ||
            chunk_size > 16 * 1024 * 1024 ||
            full_hash.size() != 64 || !signature_json.is_array())
        {
            throw std::runtime_error("文件清单无效");
        }
        if (file_size > kMaxFileSize)
        {
            throw std::runtime_error("文件超过服务端允许的最大大小");
        }
        const auto expected_chunks = file_size == 0 ? 0 : ((file_size - 1) / chunk_size) + 1;
        if (expected_chunks > kMaxChunkCount)
        {
            throw std::runtime_error("文件块数量超过服务端限制");
        }
        if (signature_json.size() != expected_chunks)
        {
            throw std::runtime_error("文件块签名数量无效");
        }
        std::vector<Signature> signatures;
        signatures.reserve(signature_json.size());
        for (const auto& item : signature_json)
        {
            signatures.push_back({item.value("weak", 0U), item.value("strong", ""), item.value("length", 0U)});
            if (signatures.back().strong.size() != 64 || signatures.back().length == 0 ||
                signatures.back().length > chunk_size)
            {
                throw std::runtime_error("文件块签名无效");
            }
        }

        const auto destination = (config_.destination_root / name).lexically_normal();
        std::shared_ptr<std::mutex> destination_mutex;
        {
            std::lock_guard lock(destination_locks_mutex_);
            auto& weak = destination_locks_[destination.string()];
            destination_mutex = weak.lock();
            if (!destination_mutex)
            {
                destination_mutex = std::make_shared<std::mutex>();
                weak = destination_mutex;
            }
        }
        std::unique_lock destination_lock(*destination_mutex);
        auto destination_permissions = std::filesystem::perms::unknown;
        std::error_code permissions_error;
        if (std::filesystem::exists(destination, permissions_error))
        {
            destination_permissions = std::filesystem::status(destination, permissions_error).permissions();
            if (permissions_error)
            {
                throw std::runtime_error("无法读取目标文件权限");
            }
        }
        if (!config_.overwrite && std::filesystem::exists(destination))
        {
            connection.SendJson({{"type", "error"}, {"sessionId", session_id}, {"message", "目标文件已存在"}});
            return;
        }
        const auto temporary = config_.destination_root / ("." + name + "." + session_id + ".part");
        TemporaryFileGuard temporary_guard(temporary);
        {
            std::ofstream create(temporary, std::ios::binary | std::ios::trunc);
            if (!create)
            {
                throw std::runtime_error("无法创建临时文件");
            }
        }
        std::filesystem::resize_file(temporary, file_size);
        std::fstream output(temporary, std::ios::binary | std::ios::in | std::ios::out);
        if (!output)
        {
            throw std::runtime_error("无法打开临时文件");
        }

        std::vector<bool> matched(signatures.size(), false);
        std::uint64_t matched_bytes = 0;
        const auto candidates = CandidateFiles(config_, name);
        const auto same_name_count = CountSameName(candidates, name);
        for (std::size_t index = 0; index < same_name_count && !AllMatched(matched); ++index)
        {
            ScanCandidate(candidates[index], signatures, chunk_size, matched, output, matched_bytes);
        }
        const auto ratio = file_size == 0 ? 1.0 : static_cast<double>(matched_bytes) / static_cast<double>(file_size);
        if (ratio <= config_.same_name_match_threshold)
        {
            for (std::size_t index = same_name_count; index < candidates.size() && !AllMatched(matched); ++index)
            {
                ScanCandidate(candidates[index], signatures, chunk_size, matched, output, matched_bytes);
            }
        }
        output.flush();
        auto missing = nlohmann::json::array();
        std::vector<std::size_t> missing_indices;
        for (std::size_t index = 0; index < matched.size(); ++index)
        {
            if (!matched[index])
            {
                missing.push_back(index);
                missing_indices.push_back(index);
            }
        }
        connection.SendJson({{"type", "plan"},
                             {"sessionId", session_id},
                             {"fileId", file_id},
                             {"matchedBytes", matched_bytes},
                             {"compressionAlgorithms", nlohmann::json::array({kZlibCompression})},
                             {"streamCompressionAlgorithms", nlohmann::json::array({kZlibCompression})},
                             {"missing", missing}});

        std::size_t remaining = missing_indices.size();
        auto receive_chunk = [&](const nlohmann::json& chunk) {
            if (chunk.value("type", "") != "chunk" || chunk.value("sessionId", "") != session_id ||
                chunk.value("fileId", "") != file_id)
            {
                throw std::runtime_error("块描述与当前会话不匹配");
            }
            const auto index = chunk.value("chunkIndex", signatures.size());
            const auto body_length = chunk.value("bodyLength", std::uint64_t{0});
            const auto uncompressed_length = chunk.value("uncompressedLength", body_length);
            const auto compression_algorithm = chunk.value("compressionAlgorithm", std::string("none"));
            const auto target_offset = chunk.value("targetOffset", std::uint64_t{0});
            const auto compression_valid = compression_algorithm == "none" || compression_algorithm == kZlibCompression;
            const auto lengths_valid = compression_algorithm == "none"
                                           ? body_length == uncompressed_length
                                           : body_length < uncompressed_length;
            if (index >= signatures.size() || matched[index] || !compression_valid || !lengths_valid ||
                uncompressed_length != signatures[index].length || target_offset != index * chunk_size ||
                body_length == 0 || body_length > kMaxChunkBody || uncompressed_length > kMaxChunkBody ||
                chunk.value("strongHash", "") != signatures[index].strong)
            {
                throw std::runtime_error("块描述无效或重复");
            }
            std::vector<unsigned char> encoded_body(static_cast<std::size_t>(body_length));
            connection.ReadAll(encoded_body.data(), encoded_body.size());
            auto body = compression_algorithm == kZlibCompression
                            ? DecompressZlib(encoded_body.data(), encoded_body.size(),
                                             static_cast<std::size_t>(uncompressed_length))
                            : std::move(encoded_body);
            if (Sha256(body.data(), body.size()) != signatures[index].strong)
            {
                throw std::runtime_error("块 SHA-256 校验失败");
            }
            output.seekp(static_cast<std::streamoff>(index * chunk_size));
            output.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
            if (!output)
            {
                throw std::runtime_error("写入上传块失败");
            }
            matched[index] = true;
            --remaining;
            connection.SendJson({{"type", "chunkAck"},
                                 {"sessionId", session_id},
                                 {"fileId", file_id},
                                 {"chunkIndex", index},
                                 {"compressionAlgorithm", compression_algorithm}});
        };

        if (remaining > 0)
        {
            const auto first = connection.ReceiveJson();
            if (first.value("type", "") == "streamStart")
            {
                std::uint64_t expected_uncompressed = 0;
                for (const auto index : missing_indices)
                {
                    if (expected_uncompressed > kMaxFileSize - signatures[index].length)
                    {
                        throw std::runtime_error("连续流原始长度溢出");
                    }
                    expected_uncompressed += signatures[index].length;
                }
                if (first.value("sessionId", "") != session_id || first.value("fileId", "") != file_id ||
                    first.value("compressionAlgorithm", "") != kZlibCompression ||
                    first.value("uncompressedLength", std::uint64_t{0}) != expected_uncompressed)
                {
                    throw std::runtime_error("连续流描述与当前会话不匹配");
                }
                connection.SendJson({{"type", "streamReady"}, {"sessionId", session_id}, {"fileId", file_id}});

                InflateStream inflater;
                auto& stream = inflater.Get();
                std::vector<unsigned char> encoded(kStreamBufferSize);
                std::vector<unsigned char> decoded(kStreamBufferSize);
                std::size_t missing_position = 0;
                std::size_t block_written = 0;
                std::uint64_t total_written = 0;
                std::uint64_t total_wire = 0;
                std::uint64_t expected_sequence = 0;
                bool stream_finished = false;
                Sha256Accumulator block_hash;
                const auto max_stream_wire = expected_uncompressed + expected_uncompressed / 16 + kStreamBufferSize;

                auto write_decoded = [&](const unsigned char* data, std::size_t size) {
                    while (size > 0)
                    {
                        if (missing_position >= missing_indices.size())
                        {
                            throw std::runtime_error("连续流解压数据超过预期长度");
                        }
                        const auto index = missing_indices[missing_position];
                        const auto block_remaining = signatures[index].length - block_written;
                        const auto count = std::min(size, block_remaining);
                        output.seekp(static_cast<std::streamoff>(index * chunk_size + block_written));
                        output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(count));
                        if (!output)
                        {
                            throw std::runtime_error("写入连续流解压数据失败");
                        }
                        block_hash.Update(data, count);
                        data += count;
                        size -= count;
                        block_written += count;
                        total_written += count;
                        if (block_written == signatures[index].length)
                        {
                            if (block_hash.Finish() != signatures[index].strong)
                            {
                                throw std::runtime_error("连续流块 SHA-256 校验失败");
                            }
                            matched[index] = true;
                            --remaining;
                            ++missing_position;
                            block_written = 0;
                            if (missing_position < missing_indices.size())
                            {
                                block_hash.Reset();
                            }
                        }
                    }
                };

                while (true)
                {
                    const auto frame = connection.ReceiveJson();
                    const auto type = frame.value("type", "");
                    if (frame.value("sessionId", "") != session_id || frame.value("fileId", "") != file_id)
                    {
                        throw std::runtime_error("连续流帧与当前会话不匹配");
                    }
                    if (type == "streamEnd")
                    {
                        if (!stream_finished || frame.value("sequence", std::uint64_t{0}) != expected_sequence ||
                            missing_position != missing_indices.size() || block_written != 0 ||
                            total_written != expected_uncompressed || remaining != 0 ||
                            frame.value("wireBytes", std::numeric_limits<std::uint64_t>::max()) != total_wire)
                        {
                            throw std::runtime_error("连续流未完整结束");
                        }
                        connection.SendJson({{"type", "streamAck"},
                                             {"sessionId", session_id},
                                             {"fileId", file_id},
                                             {"wireBytes", frame.value("wireBytes", std::uint64_t{0})}});
                        break;
                    }
                    const auto body_length = frame.value("bodyLength", std::uint64_t{0});
                    if (type != "streamData" || stream_finished ||
                        frame.value("sequence", std::numeric_limits<std::uint64_t>::max()) != expected_sequence ||
                        body_length == 0 || body_length > kMaxStreamFrameBody ||
                        body_length > max_stream_wire - total_wire)
                    {
                        throw std::runtime_error("连续流数据帧无效");
                    }
                    encoded.resize(static_cast<std::size_t>(body_length));
                    connection.ReadAll(encoded.data(), encoded.size());
                    total_wire += body_length;
                    stream.next_in = encoded.data();
                    stream.avail_in = static_cast<uInt>(encoded.size());
                    while (true)
                    {
                        stream.next_out = decoded.data();
                        stream.avail_out = static_cast<uInt>(decoded.size());
                        const auto result = inflate(&stream, Z_NO_FLUSH);
                        const auto produced = decoded.size() - stream.avail_out;
                        if (produced > 0)
                        {
                            write_decoded(decoded.data(), produced);
                        }
                        if (result == Z_STREAM_END)
                        {
                            stream_finished = true;
                            if (stream.avail_in != 0)
                            {
                                throw std::runtime_error("连续流尾部包含多余压缩数据");
                            }
                            break;
                        }
                        if (result != Z_OK && !(result == Z_BUF_ERROR && stream.avail_in == 0 && produced == 0))
                        {
                            throw std::runtime_error("zlib 连续流解压失败");
                        }
                        if (stream.avail_in == 0 && produced < decoded.size())
                        {
                            break;
                        }
                    }
                    connection.SendJson({{"type", "streamDataAck"},
                                         {"sessionId", session_id},
                                         {"fileId", file_id},
                                         {"sequence", expected_sequence}});
                    ++expected_sequence;
                }
            }
            else
            {
                receive_chunk(first);
                while (remaining > 0)
                {
                    receive_chunk(connection.ReceiveJson());
                }
            }
        }
        const auto commit = connection.ReceiveJson();
        if (commit.value("type", "") != "commit" || commit.value("sessionId", "") != session_id ||
            commit.value("fileId", "") != file_id)
        {
            throw std::runtime_error("缺少文件提交请求");
        }
        output.flush();
        output.close();
        if (Sha256File(temporary) != full_hash)
        {
            std::filesystem::remove(temporary);
            connection.SendJson({{"type", "commitResult"},
                                 {"sessionId", session_id},
                                 {"fileId", file_id},
                                 {"ok", false},
                                 {"message", "最终文件 SHA-256 校验失败"}});
            return;
        }
        if (destination_permissions != std::filesystem::perms::unknown)
        {
            std::filesystem::permissions(
                temporary, destination_permissions, std::filesystem::perm_options::replace, permissions_error);
            if (permissions_error)
            {
                throw std::runtime_error("无法继承目标文件权限");
            }
        }
#ifdef _WIN32
        if (config_.overwrite)
        {
            std::filesystem::remove(destination);
        }
#endif
        std::filesystem::rename(temporary, destination);
        temporary_guard.Release();
        connection.SendJson({{"type", "commitResult"},
                             {"sessionId", session_id},
                             {"fileId", file_id},
                             {"ok", true},
                             {"matchedBytes", matched_bytes}});
    }

    ServerConfig config_;
    std::atomic<bool> running_{false};
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> tls_context_{nullptr, SSL_CTX_free};
    std::vector<SocketHandle> listeners_;
    std::vector<std::thread> accept_threads_;
    std::vector<std::thread> worker_threads_;
    std::mutex active_mutex_;
    std::set<SocketHandle> active_sockets_;
    std::mutex destination_locks_mutex_;
    std::unordered_map<std::string, std::weak_ptr<std::mutex>> destination_locks_;
};

Server::Server(ServerConfig config) : impl_(std::make_unique<Impl>(std::move(config)))
{
}

Server::~Server() = default;

void Server::Start()
{
    impl_->Start();
}

void Server::Stop()
{
    impl_->Stop();
}

bool Server::Running() const
{
    return impl_->Running();
}

const ServerConfig& Server::Config() const
{
    return impl_->Config();
}

Client::Client(ClientConfig config) : config_(std::move(config))
{
}

std::vector<FileProgress> Client::Send(const ProgressCallback& callback)
{
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
        throw std::runtime_error("Winsock 初始化失败");
    }
#endif
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif
    ValidateClientConfig(config_);
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> tls_context(nullptr, SSL_CTX_free);
    auto connection = Connect(config_, tls_context);
    connection->SendJson({{"type", "hello"}, {"protocolVersion", kProtocolVersion}});
    const auto hello = connection->ReceiveJson();
    if (!hello.value("ok", false) || hello.value("protocolVersion", 0) != kProtocolVersion)
    {
        throw std::runtime_error("服务端协议版本不兼容");
    }

    std::vector<FileProgress> results;
    for (const auto& path : config_.files)
    {
        FileProgress progress;
        progress.file_id = RandomId();
        progress.path = path.string();
        try
        {
            if (!std::filesystem::is_regular_file(path))
            {
                throw std::runtime_error("待传输路径不是普通文件");
            }
            progress.file_size = std::filesystem::file_size(path);
            progress.stage = "hashing";
            if (callback)
            {
                callback(progress);
            }
            const auto signatures = BuildSignatures(path, config_.chunk_size);
            const auto full_hash = Sha256File(path);
            auto signature_json = nlohmann::json::array();
            for (const auto& signature : signatures)
            {
                signature_json.push_back({{"weak", signature.weak},
                                          {"strong", signature.strong},
                                          {"length", signature.length}});
            }
            const auto session_id = RandomId();
            progress.stage = "scanning";
            if (callback)
            {
                callback(progress);
            }
            connection->SendJson({{"type", "startFile"},
                                  {"protocolVersion", kProtocolVersion},
                                  {"sessionId", session_id},
                                  {"fileId", progress.file_id},
                                  {"name", path.filename().string()},
                                  {"fileSize", progress.file_size},
                                  {"chunkSize", config_.chunk_size},
                                  {"hashAlgorithm", "sha256"},
                                  {"fullHash", full_hash},
                                  {"signatures", signature_json}});
            const auto plan = connection->ReceiveJson();
            if (plan.value("type", "") == "error")
            {
                throw std::runtime_error(plan.value("message", "服务端拒绝文件"));
            }
            if (plan.value("type", "") != "plan" || plan.value("sessionId", "") != session_id)
            {
                throw std::runtime_error("服务端返回的差分计划无效");
            }
            progress.matched_bytes = plan.value("matchedBytes", std::uint64_t{0});
            const auto compression_algorithms = plan.value("compressionAlgorithms", nlohmann::json::array());
            const auto supports_zlib = compression_algorithms.is_array() &&
                                       std::find(compression_algorithms.begin(),
                                                 compression_algorithms.end(),
                                                 kZlibCompression) != compression_algorithms.end();
            const auto stream_compression_algorithms =
                plan.value("streamCompressionAlgorithms", nlohmann::json::array());
            const auto supports_zlib_stream = stream_compression_algorithms.is_array() &&
                                              std::find(stream_compression_algorithms.begin(),
                                                        stream_compression_algorithms.end(),
                                                        kZlibCompression) != stream_compression_algorithms.end();
            std::vector<std::size_t> missing_indices;
            std::uint64_t missing_bytes = 0;
            for (const auto& item : plan.value("missing", nlohmann::json::array()))
            {
                const auto index = item.get<std::size_t>();
                if (index >= signatures.size())
                {
                    throw std::runtime_error("服务端返回了无效的缺失块编号");
                }
                if (!missing_indices.empty() && index <= missing_indices.back())
                {
                    throw std::runtime_error("服务端返回的缺失块顺序无效");
                }
                if (missing_bytes > kMaxFileSize - signatures[index].length)
                {
                    throw std::runtime_error("缺失块总长度溢出");
                }
                missing_indices.push_back(index);
                missing_bytes += signatures[index].length;
            }
            progress.stage = "uploading";
            if (callback)
            {
                callback(progress);
            }
            std::ifstream input(path, std::ios::binary);
            if (config_.compression_mode == "stream" && supports_zlib_stream && !missing_indices.empty())
            {
                progress.compression_mode = "stream";
                connection->SendJson({{"type", "streamStart"},
                                      {"sessionId", session_id},
                                      {"fileId", progress.file_id},
                                      {"compressionAlgorithm", kZlibCompression},
                                      {"uncompressedLength", missing_bytes}});
                const auto ready = connection->ReceiveJson();
                if (ready.value("type", "") != "streamReady" || ready.value("sessionId", "") != session_id ||
                    ready.value("fileId", "") != progress.file_id)
                {
                    throw std::runtime_error("服务端未确认连续压缩流");
                }

                DeflateStream deflater;
                auto& stream = deflater.Get();
                std::vector<unsigned char> stream_input(kStreamBufferSize);
                std::vector<unsigned char> encoded(kStreamBufferSize);
                std::uint64_t sequence = 0;
                auto send_encoded = [&](std::size_t size) {
                    connection->SendJson({{"type", "streamData"},
                                          {"sessionId", session_id},
                                          {"fileId", progress.file_id},
                                          {"sequence", sequence},
                                          {"bodyLength", size}});
                    connection->WriteAll(encoded.data(), size);
                    const auto acknowledgement = connection->ReceiveJson();
                    if (acknowledgement.value("type", "") != "streamDataAck" ||
                        acknowledgement.value("sessionId", "") != session_id ||
                        acknowledgement.value("fileId", "") != progress.file_id ||
                        acknowledgement.value("sequence", std::numeric_limits<std::uint64_t>::max()) != sequence)
                    {
                        throw std::runtime_error("服务端连续流数据确认无效");
                    }
                    progress.wire_bytes += size;
                    ++sequence;
                };

                for (const auto index : missing_indices)
                {
                    input.seekg(static_cast<std::streamoff>(index * config_.chunk_size));
                    std::size_t block_remaining = signatures[index].length;
                    while (block_remaining > 0)
                    {
                        const auto input_size = std::min(block_remaining, stream_input.size());
                        input.read(reinterpret_cast<char*>(stream_input.data()),
                                   static_cast<std::streamsize>(input_size));
                        if (static_cast<std::size_t>(input.gcount()) != input_size)
                        {
                            throw std::runtime_error("读取待上传连续流失败");
                        }
                        block_remaining -= input_size;
                        stream.next_in = stream_input.data();
                        stream.avail_in = static_cast<uInt>(input_size);
                        while (stream.avail_in > 0)
                        {
                            stream.next_out = encoded.data();
                            stream.avail_out = static_cast<uInt>(encoded.size());
                            if (deflate(&stream, Z_NO_FLUSH) != Z_OK)
                            {
                                throw std::runtime_error("zlib 连续流压缩失败");
                            }
                            const auto produced = encoded.size() - stream.avail_out;
                            if (produced > 0)
                            {
                                send_encoded(produced);
                            }
                        }
                    }
                    progress.uploaded_bytes += signatures[index].length;
                    ++progress.compressed_chunks;
                    if (callback)
                    {
                        callback(progress);
                    }
                }
                while (true)
                {
                    stream.next_out = encoded.data();
                    stream.avail_out = static_cast<uInt>(encoded.size());
                    const auto result = deflate(&stream, Z_FINISH);
                    if (result != Z_OK && result != Z_STREAM_END)
                    {
                        throw std::runtime_error("zlib 连续流结束失败");
                    }
                    const auto produced = encoded.size() - stream.avail_out;
                    if (produced > 0)
                    {
                        send_encoded(produced);
                    }
                    if (result == Z_STREAM_END)
                    {
                        break;
                    }
                }
                connection->SendJson({{"type", "streamEnd"},
                                      {"sessionId", session_id},
                                      {"fileId", progress.file_id},
                                      {"sequence", sequence},
                                      {"wireBytes", progress.wire_bytes}});
                const auto acknowledgement = connection->ReceiveJson();
                if (acknowledgement.value("type", "") != "streamAck" ||
                    acknowledgement.value("sessionId", "") != session_id ||
                    acknowledgement.value("fileId", "") != progress.file_id)
                {
                    throw std::runtime_error("服务端连续流结束确认无效");
                }
            }
            else
            {
                std::vector<unsigned char> buffer(config_.chunk_size);
                for (const auto index : missing_indices)
                {
                    input.seekg(static_cast<std::streamoff>(index * config_.chunk_size));
                    input.read(reinterpret_cast<char*>(buffer.data()),
                               static_cast<std::streamsize>(signatures[index].length));
                    if (static_cast<std::size_t>(input.gcount()) != signatures[index].length)
                    {
                        throw std::runtime_error("读取待上传块失败");
                    }
                    auto compressed = supports_zlib
                                          ? TryCompressZlib(buffer.data(), signatures[index].length)
                                          : std::vector<unsigned char>{};
                    const auto use_compression = !compressed.empty();
                    const auto* body = use_compression ? compressed.data() : buffer.data();
                    const auto body_length = use_compression ? compressed.size() : signatures[index].length;
                    const auto compression_algorithm = use_compression ? kZlibCompression : std::string_view("none");
                    connection->SendJson({{"type", "chunk"},
                                          {"sessionId", session_id},
                                          {"fileId", progress.file_id},
                                          {"chunkIndex", index},
                                          {"targetOffset", index * config_.chunk_size},
                                          {"bodyLength", body_length},
                                          {"uncompressedLength", signatures[index].length},
                                          {"compressionAlgorithm", compression_algorithm},
                                          {"strongHash", signatures[index].strong}});
                    connection->WriteAll(body, body_length);
                    const auto acknowledgement = connection->ReceiveJson();
                    if (acknowledgement.value("type", "") == "error")
                    {
                        throw std::runtime_error(acknowledgement.value("message", "服务端拒绝文件块"));
                    }
                    if (acknowledgement.value("type", "") != "chunkAck" ||
                        acknowledgement.value("chunkIndex", signatures.size()) != index)
                    {
                        throw std::runtime_error("服务端块确认无效");
                    }
                    progress.uploaded_bytes += signatures[index].length;
                    progress.wire_bytes += body_length;
                    progress.compressed_chunks += use_compression ? 1 : 0;
                    if (use_compression)
                    {
                        progress.compression_mode = "chunk";
                    }
                    if (callback)
                    {
                        callback(progress);
                    }
                }
            }
            progress.stage = "verifying";
            if (callback)
            {
                callback(progress);
            }
            connection->SendJson({{"type", "commit"}, {"sessionId", session_id}, {"fileId", progress.file_id}});
            const auto result = connection->ReceiveJson();
            if (result.value("type", "") != "commitResult" || !result.value("ok", false))
            {
                throw std::runtime_error(result.value("message", "服务端最终校验失败"));
            }
            progress.stage = "completed";
        }
        catch (const std::exception& error)
        {
            progress.stage = "failed";
            progress.error = error.what();
        }
        if (callback)
        {
            callback(progress);
        }
        results.push_back(progress);
        if (!progress.error.empty())
        {
            break;
        }
    }
    try
    {
        connection->SendJson({{"type", "goodbye"}});
    }
    catch (...)
    {
    }
#ifdef _WIN32
    WSACleanup();
#endif
    return results;
}
} // namespace spacestation::transfer
