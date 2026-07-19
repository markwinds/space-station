#include "ssh/sftp_service.hpp"

#include <libssh2.h>
#include <libssh2_sftp.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace spacestation::ssh
{
struct SftpService::Forward
{
    std::string id;
    std::string host_id;
    std::string remote_host;
    int local_port = 0;
    int remote_port = 0;
    std::atomic<bool> running{false};
    std::mutex message_mutex;
    std::string message = "正在启动";
    std::jthread worker;
};

namespace
{
constexpr std::size_t kMaximumSftpFileSize = 256ULL * 1024ULL * 1024ULL;

struct SocketHandle
{
    libssh2_socket_t value = LIBSSH2_INVALID_SOCKET;
    SocketHandle() = default;
    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;
    SocketHandle(SocketHandle&& other) noexcept : value(std::exchange(other.value, LIBSSH2_INVALID_SOCKET))
    {
    }
    SocketHandle& operator=(SocketHandle&& other) noexcept
    {
        if (this == &other) return *this;
        Close();
        value = std::exchange(other.value, LIBSSH2_INVALID_SOCKET);
        return *this;
    }
    ~SocketHandle()
    {
        Close();
    }

  private:
    void Close()
    {
        if (value == LIBSSH2_INVALID_SOCKET) return;
#ifdef _WIN32
        closesocket(value);
#else
        close(value);
#endif
        value = LIBSSH2_INVALID_SOCKET;
    }
};

struct SessionDeleter
{
    void operator()(LIBSSH2_SESSION* session) const
    {
        if (session) libssh2_session_free(session);
    }
};

struct SftpDeleter
{
    void operator()(LIBSSH2_SFTP* sftp) const
    {
        if (sftp) libssh2_sftp_shutdown(sftp);
    }
};

struct ChannelDeleter
{
    void operator()(LIBSSH2_CHANNEL* channel) const
    {
        if (channel) libssh2_channel_free(channel);
    }
};

struct SftpHandleDeleter
{
    void operator()(LIBSSH2_SFTP_HANDLE* handle) const
    {
        if (handle) libssh2_sftp_close_handle(handle);
    }
};

struct AgentDeleter
{
    void operator()(LIBSSH2_AGENT* agent) const
    {
        if (!agent) return;
        libssh2_agent_disconnect(agent);
        libssh2_agent_free(agent);
    }
};

using SessionPtr = std::unique_ptr<LIBSSH2_SESSION, SessionDeleter>;
using SftpPtr = std::unique_ptr<LIBSSH2_SFTP, SftpDeleter>;
using ChannelPtr = std::unique_ptr<LIBSSH2_CHANNEL, ChannelDeleter>;
using SftpHandlePtr = std::unique_ptr<LIBSSH2_SFTP_HANDLE, SftpHandleDeleter>;
using AgentPtr = std::unique_ptr<LIBSSH2_AGENT, AgentDeleter>;

struct Connection
{
    SocketHandle socket;
    SessionPtr jump_session;
    ChannelPtr jump_channel;
    SessionPtr session;
    SftpPtr sftp;
};

LIBSSH2_RECV_FUNC(ChannelReceive)
{
    const auto result = libssh2_channel_read(static_cast<LIBSSH2_CHANNEL*>(*abstract), static_cast<char*>(buffer), length);
    if (result == LIBSSH2_ERROR_EAGAIN) { errno = EAGAIN; return -1; }
    return result;
}

LIBSSH2_SEND_FUNC(ChannelSend)
{
    const auto result = libssh2_channel_write(static_cast<LIBSSH2_CHANNEL*>(*abstract), static_cast<const char*>(buffer), length);
    if (result == LIBSSH2_ERROR_EAGAIN) { errno = EAGAIN; return -1; }
    return result;
}

void EnsureLibssh2Initialized()
{
    static const int initialized = [] {
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return -1;
#endif
        return libssh2_init(0);
    }();
    if (initialized != 0) throw std::runtime_error("SSH 库初始化失败。");
}

SocketHandle ConnectSocket(const std::string& host, int port)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* raw = nullptr;
    const auto service = std::to_string(port);
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &raw) != 0)
    {
        throw std::runtime_error("无法解析 SFTP 主机地址。");
    }
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addresses(raw, freeaddrinfo);
    for (auto* address = addresses.get(); address; address = address->ai_next)
    {
        SocketHandle socket_handle;
        socket_handle.value = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket_handle.value == LIBSSH2_INVALID_SOCKET) continue;
#ifdef _WIN32
        DWORD timeout = 15000;
        setsockopt(socket_handle.value, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        setsockopt(socket_handle.value, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
        timeval timeout{15, 0};
        setsockopt(socket_handle.value, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(socket_handle.value, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
        if (connect(socket_handle.value, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0)
        {
            return socket_handle;
        }
    }
    throw std::runtime_error("无法连接 SFTP 主机。");
}

std::string SessionError(LIBSSH2_SESSION* session, const char* fallback)
{
    char* message = nullptr;
    int length = 0;
    libssh2_session_last_error(session, &message, &length, 0);
    return message && length > 0 ? std::string(message, static_cast<std::size_t>(length)) : fallback;
}

std::string Fingerprint(LIBSSH2_SESSION* session)
{
    std::size_t length = 0;
    int type = 0;
    const auto* key = libssh2_session_hostkey(session, &length, &type);
    if (!key || length == 0) throw std::runtime_error("无法读取 SFTP 主机公钥。");
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    SHA256(reinterpret_cast<const unsigned char*>(key), length, digest.data());
    std::array<unsigned char, 64> encoded{};
    const auto encoded_length = EVP_EncodeBlock(encoded.data(), digest.data(), digest.size());
    auto value = std::string(reinterpret_cast<const char*>(encoded.data()), encoded_length);
    while (!value.empty() && value.back() == '=') value.pop_back();
    return "SHA256:" + value;
}

std::string NormalizePath(const std::string& value)
{
    auto path = std::filesystem::path(value.empty() ? "/" : value).lexically_normal().generic_string();
    if (path.empty() || path.front() != '/') path.insert(path.begin(), '/');
    return path;
}

nlohmann::json FindHost(ConfigStore& store, const std::string& host_id)
{
    for (const auto& host : store.LoadSshHosts())
    {
        if (host.is_object() && host.value("id", "") == host_id) return host;
    }
    throw std::runtime_error("找不到 SFTP 主机。");
}

void AuthenticateBlocking(LIBSSH2_SESSION* session,
                          const std::string& username,
                          const std::optional<nlohmann::json>& credential,
                          bool use_agent,
                          const char* fallback)
{
    int result = LIBSSH2_ERROR_AUTHENTICATION_FAILED;
    if (use_agent)
    {
        AgentPtr agent(libssh2_agent_init(session));
        if (!agent || libssh2_agent_connect(agent.get()) != 0 || libssh2_agent_list_identities(agent.get()) != 0)
            throw std::runtime_error("无法连接 SSH Agent，请检查 SSH_AUTH_SOCK。");
        libssh2_agent_publickey* identity = nullptr;
        libssh2_agent_publickey* previous = nullptr;
        while (libssh2_agent_get_identity(agent.get(), &identity, previous) == 0)
        {
            result = libssh2_agent_userauth(agent.get(), username.c_str(), identity);
            if (result == 0) break;
            previous = identity;
        }
    }
    else if (credential && credential->value("method", "") == "privateKey")
    {
        const auto private_key = credential->value("privateKey", "");
        const auto passphrase = credential->value("passphrase", "");
        result = libssh2_userauth_publickey_frommemory(session, username.c_str(), username.size(), nullptr, 0,
                                                       private_key.data(), private_key.size(),
                                                       passphrase.empty() ? nullptr : passphrase.c_str());
    }
    else if (credential)
    {
        const auto password = credential->value("password", "");
        result = libssh2_userauth_password_ex(session, username.c_str(), username.size(), password.c_str(),
                                              password.size(), nullptr);
    }
    if (result != 0) throw std::runtime_error(SessionError(session, fallback));
}

Connection OpenConnection(ConfigStore& store, const std::string& host_id, bool initialize_sftp = true)
{
    EnsureLibssh2Initialized();
    const auto host = FindHost(store, host_id);
    const auto credential = store.LoadSshCredential(host_id);
    const auto use_agent = host.value("useAgent", false);
    if (!credential && !use_agent) throw std::runtime_error("使用 SFTP 前请先加密保存该主机的凭据。");
    Connection connection;
    const auto jump_host_id = host.value("jumpHostId", "");
    if (!jump_host_id.empty())
    {
        const auto jump_host = FindHost(store, jump_host_id);
        const auto jump_fingerprint = jump_host.value("hostKeySha256", "");
        if (jump_fingerprint.empty()) throw std::runtime_error("请先直接连接并信任跳板机主机指纹。");
        const auto jump_credential = store.LoadSshCredential(jump_host_id);
        const auto jump_use_agent = jump_host.value("useAgent", false);
        if (!jump_credential && !jump_use_agent) throw std::runtime_error("请先保存跳板机凭据或启用 SSH Agent。");
        connection.socket = ConnectSocket(jump_host.value("host", ""), std::clamp(jump_host.value("port", 22), 1, 65535));
        connection.jump_session.reset(libssh2_session_init());
        if (!connection.jump_session) throw std::runtime_error("无法创建跳板机 SSH 会话。");
        libssh2_session_set_blocking(connection.jump_session.get(), 1);
        libssh2_session_set_timeout(connection.jump_session.get(), 15000);
        if (libssh2_session_handshake(connection.jump_session.get(), connection.socket.value) != 0)
            throw std::runtime_error(SessionError(connection.jump_session.get(), "跳板机 SSH 握手失败。"));
        if (Fingerprint(connection.jump_session.get()) != jump_fingerprint)
            throw std::runtime_error("跳板机主机指纹已经变化，连接已中止。");
        AuthenticateBlocking(connection.jump_session.get(), jump_host.value("username", ""), jump_credential,
                             jump_use_agent, "跳板机认证失败。");
        connection.jump_channel.reset(libssh2_channel_direct_tcpip(connection.jump_session.get(),
                                                                    host.value("host", "").c_str(),
                                                                    std::clamp(host.value("port", 22), 1, 65535)));
        if (!connection.jump_channel) throw std::runtime_error(SessionError(connection.jump_session.get(), "跳板机无法打开目标连接。"));
    }
    else
    {
        connection.socket = ConnectSocket(host.value("host", ""), std::clamp(host.value("port", 22), 1, 65535));
    }
    connection.session.reset(jump_host_id.empty()
                                 ? libssh2_session_init()
                                 : libssh2_session_init_ex(nullptr, nullptr, nullptr, connection.jump_channel.get()));
    if (!connection.session) throw std::runtime_error("无法创建 SFTP SSH 会话。");
    libssh2_session_set_blocking(connection.session.get(), 1);
    libssh2_session_set_timeout(connection.session.get(), 15000);
    if (connection.jump_channel)
    {
        libssh2_session_callback_set2(connection.session.get(), LIBSSH2_CALLBACK_RECV,
                                      reinterpret_cast<libssh2_cb_generic*>(ChannelReceive));
        libssh2_session_callback_set2(connection.session.get(), LIBSSH2_CALLBACK_SEND,
                                      reinterpret_cast<libssh2_cb_generic*>(ChannelSend));
    }
    if (libssh2_session_handshake(connection.session.get(), connection.socket.value) != 0)
    {
        throw std::runtime_error(SessionError(connection.session.get(), "SFTP SSH 握手失败。"));
    }
    const auto expected = host.value("hostKeySha256", "");
    if (expected.empty() || expected != Fingerprint(connection.session.get()))
    {
        throw std::runtime_error("SFTP 主机指纹未确认或已经变化。");
    }
    AuthenticateBlocking(connection.session.get(), host.value("username", ""), credential, use_agent,
                         "SFTP SSH 认证失败。");
    if (initialize_sftp)
    {
        connection.sftp.reset(libssh2_sftp_init(connection.session.get()));
        if (!connection.sftp) throw std::runtime_error(SessionError(connection.session.get(), "SFTP 子系统启动失败。"));
    }
    return connection;
}

std::runtime_error SftpError(LIBSSH2_SFTP* sftp, const char* action)
{
    return std::runtime_error(std::string(action) + "，SFTP 错误码 " + std::to_string(libssh2_sftp_last_error(sftp)) + "。");
}

void SetNonBlocking(libssh2_socket_t socket_value)
{
#ifdef _WIN32
    u_long enabled = 1;
    ioctlsocket(socket_value, FIONBIO, &enabled);
#else
    const auto flags = fcntl(socket_value, F_GETFL, 0);
    if (flags >= 0) fcntl(socket_value, F_SETFL, flags | O_NONBLOCK);
#endif
}

void RelayForward(ConfigStore& store, const std::string& host_id, const std::string& remote_host,
                  int remote_port, libssh2_socket_t client, std::stop_token token)
{
    auto connection = OpenConnection(store, host_id, false);
    LIBSSH2_CHANNEL* raw_channel = libssh2_channel_direct_tcpip(connection.session.get(), remote_host.c_str(), remote_port);
    ChannelPtr channel(raw_channel);
    if (!channel) throw std::runtime_error(SessionError(connection.session.get(), "端口转发通道创建失败。"));
    libssh2_session_set_blocking(connection.session.get(), 0);
    SetNonBlocking(client);
    std::array<char, 32768> local_buffer{};
    std::array<char, 32768> remote_buffer{};
    std::string to_remote;
    std::size_t remote_offset = 0;
    std::string to_local;
    std::size_t local_offset = 0;
    while (!token.stop_requested() && !libssh2_channel_eof(channel.get()))
    {
        fd_set reads;
        fd_set writes;
        FD_ZERO(&reads);
        FD_ZERO(&writes);
        FD_SET(client, &reads);
        FD_SET(connection.socket.value, &reads);
        if (local_offset < to_local.size()) FD_SET(client, &writes);
        timeval timeout{0, 100000};
        const auto maximum = static_cast<int>(std::max(client, connection.socket.value) + 1);
        select(maximum, &reads, &writes, nullptr, &timeout);
        if (FD_ISSET(client, &reads) && remote_offset == to_remote.size())
        {
#ifdef _WIN32
            const auto count = recv(client, local_buffer.data(), static_cast<int>(local_buffer.size()), 0);
#else
            const auto count = recv(client, local_buffer.data(), local_buffer.size(), 0);
#endif
            if (count <= 0) break;
            to_remote.assign(local_buffer.data(), static_cast<std::size_t>(count));
            remote_offset = 0;
        }
        while (remote_offset < to_remote.size())
        {
            const auto count = libssh2_channel_write(channel.get(), to_remote.data() + remote_offset,
                                                      to_remote.size() - remote_offset);
            if (count == LIBSSH2_ERROR_EAGAIN) break;
            if (count < 0) throw std::runtime_error("端口转发写入远端失败。");
            remote_offset += static_cast<std::size_t>(count);
        }
        if (remote_offset == to_remote.size()) { to_remote.clear(); remote_offset = 0; }
        if (local_offset == to_local.size())
        {
            const auto count = libssh2_channel_read(channel.get(), remote_buffer.data(), remote_buffer.size());
            if (count > 0) { to_local.assign(remote_buffer.data(), static_cast<std::size_t>(count)); local_offset = 0; }
            else if (count < 0 && count != LIBSSH2_ERROR_EAGAIN) throw std::runtime_error("端口转发读取远端失败。");
        }
        if (local_offset < to_local.size() && FD_ISSET(client, &writes))
        {
#ifdef _WIN32
            const auto count = send(client, to_local.data() + local_offset, static_cast<int>(to_local.size() - local_offset), 0);
#else
            const auto count = send(client, to_local.data() + local_offset, to_local.size() - local_offset, 0);
#endif
            if (count > 0) local_offset += static_cast<std::size_t>(count);
        }
        if (local_offset == to_local.size()) { to_local.clear(); local_offset = 0; }
    }
}
} // namespace

SftpService::SftpService(ConfigStore& config_store) : config_store_(config_store)
{
}

SftpService::~SftpService()
{
    std::unordered_map<std::string, std::shared_ptr<Forward>> forwards;
    {
        std::lock_guard lock(forwards_mutex_);
        forwards.swap(forwards_);
    }
    for (auto& [_, forward] : forwards) forward->worker.request_stop();
    for (auto& [_, forward] : forwards)
        if (forward->worker.joinable()) forward->worker.join();
}

nlohmann::json SftpService::List(const std::string& host_id, const std::string& path) const
{
    auto connection = OpenConnection(config_store_, host_id);
    const auto normalized = NormalizePath(path);
    SftpHandlePtr directory(libssh2_sftp_opendir(connection.sftp.get(), normalized.c_str()));
    if (!directory) throw SftpError(connection.sftp.get(), "远程目录打开失败");
    auto items = nlohmann::json::array();
    std::array<char, 4096> name{};
    std::array<char, 4096> long_entry{};
    LIBSSH2_SFTP_ATTRIBUTES attributes{};
    while (true)
    {
        const auto length = libssh2_sftp_readdir_ex(directory.get(), name.data(), name.size(),
                                                    long_entry.data(), long_entry.size(), &attributes);
        if (length == 0) break;
        if (length < 0) throw SftpError(connection.sftp.get(), "远程目录读取失败");
        const auto filename = std::string(name.data(), static_cast<std::size_t>(length));
        if (filename == "." || filename == "..") continue;
        const auto permissions = (attributes.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) != 0 ? attributes.permissions : 0;
        const auto type = LIBSSH2_SFTP_S_ISDIR(permissions) ? "directory" :
                          LIBSSH2_SFTP_S_ISLNK(permissions) ? "symlink" : "file";
        items.push_back({
            {"name", filename},
            {"path", normalized == "/" ? "/" + filename : normalized + "/" + filename},
            {"type", type},
            {"size", (attributes.flags & LIBSSH2_SFTP_ATTR_SIZE) != 0 ? attributes.filesize : 0},
            {"modifiedAt", (attributes.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) != 0 ? attributes.mtime : 0},
        });
    }
    std::sort(items.begin(), items.end(), [](const auto& left, const auto& right) {
        const auto left_directory = left.value("type", "") == "directory";
        const auto right_directory = right.value("type", "") == "directory";
        return left_directory != right_directory ? left_directory : left.value("name", "") < right.value("name", "");
    });
    const auto parent = normalized == "/" ? "/" : std::filesystem::path(normalized).parent_path().generic_string();
    return {{"path", normalized}, {"parentPath", parent.empty() ? "/" : parent}, {"items", items}};
}

void SftpService::CreateDirectory(const std::string& host_id, const std::string& path) const
{
    auto connection = OpenConnection(config_store_, host_id);
    const auto normalized = NormalizePath(path);
    if (libssh2_sftp_mkdir(connection.sftp.get(), normalized.c_str(), 0755) != 0)
        throw SftpError(connection.sftp.get(), "远程目录创建失败");
}

void SftpService::Remove(const std::string& host_id, const std::string& path, bool directory) const
{
    auto connection = OpenConnection(config_store_, host_id);
    const auto normalized = NormalizePath(path);
    if (normalized == "/") throw std::runtime_error("不能删除远程根目录。");
    const auto result = directory ? libssh2_sftp_rmdir(connection.sftp.get(), normalized.c_str())
                                  : libssh2_sftp_unlink(connection.sftp.get(), normalized.c_str());
    if (result != 0) throw SftpError(connection.sftp.get(), "远程项目删除失败");
}

void SftpService::Rename(const std::string& host_id, const std::string& from, const std::string& to) const
{
    auto connection = OpenConnection(config_store_, host_id);
    const auto source = NormalizePath(from);
    const auto destination = NormalizePath(to);
    if (source == "/" || destination == "/") throw std::runtime_error("不能重命名远程根目录。");
    auto result = libssh2_sftp_rename_ex(connection.sftp.get(), source.c_str(), source.size(), destination.c_str(),
                                         destination.size(), LIBSSH2_SFTP_RENAME_OVERWRITE | LIBSSH2_SFTP_RENAME_ATOMIC);
    if (result != 0)
    {
        result = libssh2_sftp_rename_ex(connection.sftp.get(), source.c_str(), source.size(), destination.c_str(),
                                        destination.size(), LIBSSH2_SFTP_RENAME_OVERWRITE);
    }
    if (result != 0) throw SftpError(connection.sftp.get(), "远程项目重命名失败");
}

void SftpService::Upload(const std::string& host_id, const std::string& path, std::string_view content) const
{
    if (content.size() > kMaximumSftpFileSize) throw std::runtime_error("SFTP 上传文件不能超过 256 MiB。");
    auto connection = OpenConnection(config_store_, host_id);
    const auto normalized = NormalizePath(path);
    SftpHandlePtr file(libssh2_sftp_open(connection.sftp.get(), normalized.c_str(),
                                        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC, 0600));
    if (!file) throw SftpError(connection.sftp.get(), "远程文件创建失败");
    std::size_t offset = 0;
    while (offset < content.size())
    {
        const auto written = libssh2_sftp_write(file.get(), content.data() + offset, content.size() - offset);
        if (written < 0) throw SftpError(connection.sftp.get(), "远程文件写入失败");
        offset += static_cast<std::size_t>(written);
    }
}

SftpDownload SftpService::Download(const std::string& host_id, const std::string& path) const
{
    auto connection = OpenConnection(config_store_, host_id);
    const auto normalized = NormalizePath(path);
    SftpHandlePtr file(libssh2_sftp_open(connection.sftp.get(), normalized.c_str(), LIBSSH2_FXF_READ, 0));
    if (!file) throw SftpError(connection.sftp.get(), "远程文件打开失败");
    std::string content;
    std::array<char, 65536> buffer{};
    while (true)
    {
        const auto count = libssh2_sftp_read(file.get(), buffer.data(), buffer.size());
        if (count == 0) break;
        if (count < 0) throw SftpError(connection.sftp.get(), "远程文件读取失败");
        if (content.size() + static_cast<std::size_t>(count) > kMaximumSftpFileSize)
            throw std::runtime_error("SFTP 下载文件不能超过 256 MiB。");
        content.append(buffer.data(), static_cast<std::size_t>(count));
    }
    return {std::filesystem::path(normalized).filename().string(), std::move(content)};
}

nlohmann::json SftpService::StartForward(const std::string& host_id, int local_port,
                                         const std::string& remote_host, int remote_port)
{
    if (local_port < 1 || local_port > 65535 || remote_port < 1 || remote_port > 65535 || remote_host.empty())
        throw std::runtime_error("端口转发参数无效。");
    FindHost(config_store_, host_id);
    auto forward = std::make_shared<Forward>();
    forward->id = host_id + "-" + std::to_string(local_port) + "-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    forward->host_id = host_id;
    forward->local_port = local_port;
    forward->remote_host = remote_host;
    forward->remote_port = remote_port;
    forward->worker = std::jthread([this, forward = forward.get()](std::stop_token token) {
        try
        {
            SocketHandle listener;
            listener.value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (listener.value == LIBSSH2_INVALID_SOCKET) throw std::runtime_error("无法创建本地监听端口。");
            int reuse = 1;
            setsockopt(listener.value, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(static_cast<std::uint16_t>(forward->local_port));
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (bind(listener.value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(listener.value, 8) != 0)
                throw std::runtime_error("本地端口无法监听，可能已被占用。");
            SetNonBlocking(listener.value);
            forward->running = true;
            { std::lock_guard lock(forward->message_mutex); forward->message = "运行中"; }
            while (!token.stop_requested())
            {
                fd_set reads;
                FD_ZERO(&reads);
                FD_SET(listener.value, &reads);
                timeval timeout{0, 200000};
                if (select(static_cast<int>(listener.value + 1), &reads, nullptr, nullptr, &timeout) <= 0) continue;
                SocketHandle client;
                client.value = accept(listener.value, nullptr, nullptr);
                if (client.value == LIBSSH2_INVALID_SOCKET) continue;
                RelayForward(config_store_, forward->host_id, forward->remote_host, forward->remote_port,
                             client.value, token);
            }
        }
        catch (const std::exception& error)
        {
            std::lock_guard lock(forward->message_mutex);
            forward->message = error.what();
        }
        forward->running = false;
    });
    {
        std::lock_guard lock(forwards_mutex_);
        forwards_[forward->id] = forward;
    }
    return {{"id", forward->id}};
}

void SftpService::StopForward(const std::string& id)
{
    std::shared_ptr<Forward> forward;
    {
        std::lock_guard lock(forwards_mutex_);
        const auto found = forwards_.find(id);
        if (found == forwards_.end()) return;
        forward = found->second;
        forwards_.erase(found);
    }
    forward->worker.request_stop();
    if (forward->worker.joinable()) forward->worker.join();
}

nlohmann::json SftpService::ForwardState() const
{
    auto items = nlohmann::json::array();
    std::lock_guard lock(forwards_mutex_);
    for (const auto& [id, forward] : forwards_)
    {
        std::string status;
        { std::lock_guard message_lock(forward->message_mutex); status = forward->message; }
        items.push_back({{"id", id}, {"hostId", forward->host_id}, {"localPort", forward->local_port},
                         {"remoteHost", forward->remote_host}, {"remotePort", forward->remote_port},
                         {"running", forward->running.load()}, {"message", status}});
    }
    return {{"forwards", items}};
}
} // namespace spacestation::ssh
