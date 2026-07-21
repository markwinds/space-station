#include "ssh/sftp_service.hpp"
#include "logging/logger.hpp"
#include "ssh/network_utils.hpp"

#include <libssh2.h>
#include <libssh2_sftp.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
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

struct SftpService::DownloadWorker
{
    std::string id;
    std::string host_id;
    std::string path;
    std::atomic<std::uint64_t> downloaded{0};
    std::atomic<std::uint64_t> total{0};
    std::atomic<bool> finished{false};
    mutable std::mutex state_mutex;
    std::string status = "queued";
    std::string error;
    std::jthread thread;
};

struct SftpService::UploadWorker
{
    struct Chunk
    {
        std::uint64_t offset = 0;
        std::string content;
        std::function<void(const std::string&)> on_complete;
    };

    std::string id;
    std::string host_id;
    std::string path;
    std::uint64_t total_size = 0;
    std::uint64_t next_offset = 0;
    std::atomic<bool> finished{false};
    std::mutex mutex;
    std::condition_variable condition;
    std::deque<Chunk> chunks;
    std::jthread thread;
};

namespace
{
constexpr std::uint64_t kMaximumSftpDownloadSize = 2ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumSftpUploadSize = 2ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaximumSftpUploadChunkSize = 8ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaximumConcurrentUploadsPerHost = 2;
constexpr auto kUploadInactivityTimeout = std::chrono::minutes(2);
using namespace std::chrono_literals;
using Deadline = std::chrono::steady_clock::time_point;
constexpr auto kConnectionTimeout = 15s;

Deadline NewDeadline()
{
    return std::chrono::steady_clock::now() + kConnectionTimeout;
}

void CheckConnectionDeadline(std::stop_token stop_token, Deadline deadline, const char* timeout_message)
{
    if (stop_token.stop_requested()) throw std::runtime_error("SSH 连接已取消。");
    if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error(timeout_message);
}

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

struct JumpTransport
{
    LIBSSH2_CHANNEL* channel = nullptr;
    std::vector<char> pending_input;
    std::size_t pending_offset = 0;
};

struct Connection
{
    SocketHandle socket;
    SessionPtr jump_session;
    ChannelPtr jump_channel;
    std::unique_ptr<JumpTransport> jump_transport;
    SessionPtr session;
    SftpPtr sftp;
};

LIBSSH2_RECV_FUNC(ChannelReceive)
{
    const auto transport = static_cast<JumpTransport*>(*abstract);
    if (transport->pending_offset < transport->pending_input.size())
    {
        const auto available = transport->pending_input.size() - transport->pending_offset;
        const auto copied = std::min(length, available);
        std::memcpy(buffer, transport->pending_input.data() + transport->pending_offset, copied);
        transport->pending_offset += copied;
        if (transport->pending_offset == transport->pending_input.size())
        {
            transport->pending_input.clear();
            transport->pending_offset = 0;
        }
        return static_cast<ssize_t>(copied);
    }
    const auto result = libssh2_channel_read(transport->channel, static_cast<char*>(buffer), length);
    if (result == LIBSSH2_ERROR_EAGAIN) return -EAGAIN;
    return result;
}

LIBSSH2_SEND_FUNC(ChannelSend)
{
    const auto transport = static_cast<JumpTransport*>(*abstract);
    auto result = libssh2_channel_write(transport->channel, static_cast<const char*>(buffer), length);
    if (result != LIBSSH2_ERROR_EAGAIN) return result;

    // A direct-tcpip channel can be write-blocked until the outer SSH session
    // receives a window-adjust/control packet. Pump it here and retain any
    // tunneled target payload for the inner receive callback.
    std::array<char, 32768> incoming{};
    const auto received = libssh2_channel_read(transport->channel, incoming.data(), incoming.size());
    if (received > 0)
    {
        transport->pending_input.insert(transport->pending_input.end(), incoming.data(), incoming.data() + received);
        result = libssh2_channel_write(transport->channel, static_cast<const char*>(buffer), length);
        if (result != LIBSSH2_ERROR_EAGAIN) return result;
    }
    return -EAGAIN;
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

void SetSocketBlocking(libssh2_socket_t socket_value, bool blocking)
{
#ifdef _WIN32
    u_long enabled = blocking ? 0 : 1;
    ioctlsocket(socket_value, FIONBIO, &enabled);
#else
    const auto flags = fcntl(socket_value, F_GETFL, 0);
    if (flags >= 0)
    {
        const auto updated = blocking ? flags & ~O_NONBLOCK : flags | O_NONBLOCK;
        fcntl(socket_value, F_SETFL, updated);
    }
#endif
}

void SetSocketIoTimeout(libssh2_socket_t socket_value)
{
#ifdef _WIN32
    DWORD timeout = static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(kConnectionTimeout).count());
    setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(socket_value, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(kConnectionTimeout).count();
    timeval timeout{static_cast<time_t>(seconds), 0};
    setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket_value, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

bool ConnectInProgress()
{
#ifdef _WIN32
    const auto error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEALREADY;
#else
    return errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EALREADY;
#endif
}

bool WaitForSocketConnection(libssh2_socket_t socket_value, std::stop_token stop_token,
                             Deadline deadline, const char* timeout_message)
{
    while (true)
    {
        CheckConnectionDeadline(stop_token, deadline, timeout_message);
        fd_set writes;
        fd_set errors;
        FD_ZERO(&writes);
        FD_ZERO(&errors);
        FD_SET(socket_value, &writes);
        FD_SET(socket_value, &errors);
        timeval timeout{0, 100000};
        const auto selected = select(static_cast<int>(socket_value + 1), nullptr, &writes, &errors, &timeout);
        if (selected == 0) continue;
        if (selected < 0) return false;
        int socket_error = 0;
#ifdef _WIN32
        int error_length = sizeof(socket_error);
#else
        socklen_t error_length = sizeof(socket_error);
#endif
        if (getsockopt(socket_value, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socket_error), &error_length) != 0)
            return false;
        return socket_error == 0;
    }
}

SocketHandle ConnectSocket(const std::string& host, int port, std::stop_token stop_token, Deadline deadline)
{
    const auto addresses = network::Resolve(host, port, stop_token, deadline, "SSH 连接已取消。",
                                            "SSH DNS 解析超时。", "无法解析 SFTP 主机地址。");
    for (const auto& address : addresses)
    {
        SocketHandle socket_handle;
        socket_handle.value = socket(address.family, address.socket_type, address.protocol);
        if (socket_handle.value == LIBSSH2_INVALID_SOCKET) continue;
        SetSocketIoTimeout(socket_handle.value);
        SetSocketBlocking(socket_handle.value, false);
        if (connect(socket_handle.value, reinterpret_cast<const sockaddr*>(&address.address),
                    static_cast<int>(address.length)) == 0)
        {
            return socket_handle;
        }
        if (ConnectInProgress() && WaitForSocketConnection(socket_handle.value, stop_token, deadline, "SSH TCP 连接超时。"))
            return socket_handle;
        CheckConnectionDeadline(stop_token, deadline, "SSH TCP 连接超时。");
    }
    throw std::runtime_error("无法连接 SFTP 主机。");
}

void WaitSocket(libssh2_socket_t socket_value, LIBSSH2_SESSION* session)
{
    fd_set reads;
    fd_set writes;
    FD_ZERO(&reads);
    FD_ZERO(&writes);
    const auto directions = libssh2_session_block_directions(session);
    if ((directions & LIBSSH2_SESSION_BLOCK_INBOUND) != 0) FD_SET(socket_value, &reads);
    if ((directions & LIBSSH2_SESSION_BLOCK_OUTBOUND) != 0) FD_SET(socket_value, &writes);
    timeval timeout{0, 100000};
    select(static_cast<int>(socket_value + 1), &reads, &writes, nullptr, &timeout);
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

Connection OpenConnection(ConfigStore& store, const std::string& host_id, bool initialize_sftp = true,
                          std::stop_token stop_token = {})
{
    EnsureLibssh2Initialized();
    const auto deadline = NewDeadline();
    const auto host = FindHost(store, host_id);
    const auto credential = store.LoadSshCredential(host_id);
    const auto use_agent = host.value("useAgent", false);
    if (!credential && !use_agent) throw std::runtime_error("使用 SFTP 前请先加密保存该主机的凭据。");
    Connection connection;
    const auto jump_host_id = host.value("jumpHostId", "");
    if (!jump_host_id.empty())
    {
        const auto jump_host = FindHost(store, jump_host_id);
        const auto route_message = "SFTP route: target=" + host_id + " via=" + jump_host_id;
        logI(route_message.c_str());
        const auto jump_fingerprint = jump_host.value("hostKeySha256", "");
        if (jump_fingerprint.empty()) throw std::runtime_error("请先直接连接并信任跳板机主机指纹。");
        const auto jump_credential = store.LoadSshCredential(jump_host_id);
        const auto jump_use_agent = jump_host.value("useAgent", false);
        if (!jump_credential && !jump_use_agent) throw std::runtime_error("请先保存跳板机凭据或启用 SSH Agent。");
        connection.socket = ConnectSocket(jump_host.value("host", ""), std::clamp(jump_host.value("port", 22), 1, 65535),
                                          stop_token, deadline);
        SetSocketBlocking(connection.socket.value, true);
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
        connection.jump_transport = std::make_unique<JumpTransport>();
        connection.jump_transport->channel = connection.jump_channel.get();
    }
    else
    {
        const auto route_message = "SFTP route: target=" + host_id + " direct";
        logI(route_message.c_str());
        connection.socket = ConnectSocket(host.value("host", ""), std::clamp(host.value("port", 22), 1, 65535),
                                          stop_token, deadline);
        SetSocketBlocking(connection.socket.value, true);
    }
    connection.session.reset(jump_host_id.empty()
                                 ? libssh2_session_init()
                                 : libssh2_session_init_ex(nullptr, nullptr, nullptr, connection.jump_transport.get()));
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
        if (connection.jump_channel)
        {
            SetSocketBlocking(connection.socket.value, false);
            libssh2_session_set_blocking(connection.jump_session.get(), 0);
        }
        connection.sftp.reset(libssh2_sftp_init(connection.session.get()));
        if (!connection.sftp)
            throw std::runtime_error(SessionError(connection.session.get(), "SFTP 子系统启动失败。"));
    }
    return connection;
}

std::runtime_error SftpError(LIBSSH2_SFTP* sftp, const char* action)
{
    return std::runtime_error(std::string(action) + "，SFTP 错误码 " + std::to_string(libssh2_sftp_last_error(sftp)) + "。");
}

void SetNonBlocking(libssh2_socket_t socket_value)
{
    SetSocketBlocking(socket_value, false);
}

void RelayForward(ConfigStore& store, const std::string& host_id, const std::string& remote_host,
                  int remote_port, libssh2_socket_t client, std::stop_token token)
{
    auto connection = OpenConnection(store, host_id, false, token);
    SetNonBlocking(connection.socket.value);
    if (connection.jump_session) libssh2_session_set_blocking(connection.jump_session.get(), 0);
    libssh2_session_set_blocking(connection.session.get(), 0);
    LIBSSH2_CHANNEL* raw_channel = nullptr;
    const auto channel_deadline = NewDeadline();
    while (!raw_channel)
    {
        CheckConnectionDeadline(token, channel_deadline, "端口转发通道创建超时。");
        raw_channel = libssh2_channel_direct_tcpip(connection.session.get(), remote_host.c_str(), remote_port);
        if (!raw_channel && libssh2_session_last_errno(connection.session.get()) == LIBSSH2_ERROR_EAGAIN)
            WaitSocket(connection.socket.value, connection.session.get());
        else
            break;
    }
    ChannelPtr channel(raw_channel);
    if (!channel) throw std::runtime_error(SessionError(connection.session.get(), "端口转发通道创建失败。"));
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

    std::unordered_map<std::string, std::shared_ptr<DownloadWorker>> downloads;
    {
        std::lock_guard lock(downloads_mutex_);
        downloads.swap(downloads_);
    }
    for (auto& [_, download] : downloads) download->thread.request_stop();
    for (auto& [_, download] : downloads)
        if (download->thread.joinable()) download->thread.join();

    std::unordered_map<std::string, std::shared_ptr<UploadWorker>> uploads;
    {
        std::lock_guard lock(uploads_mutex_);
        uploads.swap(uploads_);
    }
    for (auto& [_, upload] : uploads)
    {
        upload->thread.request_stop();
        upload->condition.notify_all();
    }
    for (auto& [_, upload] : uploads)
        if (upload->thread.joinable()) upload->thread.join();
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

void SftpService::StartUploadChunk(const std::string& upload_id,
                                   const std::string& host_id,
                                   const std::string& path,
                                   std::uint64_t offset,
                                   std::uint64_t total_size,
                                   std::string content,
                                   std::function<void(const std::string&)> on_complete)
{
    if (upload_id.empty() || upload_id.size() > 128) throw std::runtime_error("SFTP 上传会话标识无效。");
    if (total_size > kMaximumSftpUploadSize) throw std::runtime_error("SFTP 上传文件不能超过 2 GiB。");
    if (content.size() > kMaximumSftpUploadChunkSize) throw std::runtime_error("SFTP 上传分片不能超过 8 MiB。");
    if (offset > total_size || content.size() > total_size - offset)
        throw std::runtime_error("SFTP 上传分片范围无效。");

    std::shared_ptr<UploadWorker> worker;
    bool start_worker = false;
    {
        std::lock_guard lock(uploads_mutex_);
        std::erase_if(uploads_, [](const auto& item) { return item.second->finished.load(); });
        const auto existing = uploads_.find(upload_id);
        if (existing != uploads_.end())
        {
            worker = existing->second;
        }
        else
        {
            if (offset != 0) throw std::runtime_error("SFTP 上传会话已失效，请重新上传文件。");
            const auto active_for_host = std::count_if(uploads_.begin(), uploads_.end(), [&](const auto& item) {
                return item.second->host_id == host_id && !item.second->finished.load();
            });
            if (active_for_host >= kMaximumConcurrentUploadsPerHost)
                throw std::runtime_error("同一主机最多同时上传 2 个文件，请稍后重试。");
            worker = std::make_shared<UploadWorker>();
            worker->id = upload_id;
            worker->host_id = host_id;
            worker->path = path;
            worker->total_size = total_size;
            uploads_.emplace(upload_id, worker);
            start_worker = true;
        }
    }

    {
        std::lock_guard lock(worker->mutex);
        if (worker->finished.load()) throw std::runtime_error("SFTP 上传会话已结束，请重新上传文件。");
        if (worker->host_id != host_id || worker->path != path || worker->total_size != total_size)
            throw std::runtime_error("SFTP 上传会话参数不一致。");
        if (offset != worker->next_offset) throw std::runtime_error("SFTP 上传分片顺序无效。");
        worker->next_offset += content.size();
        worker->chunks.push_back({offset, std::move(content), std::move(on_complete)});
    }

    if (start_worker)
    {
        auto* const worker_ptr = worker.get();
        worker->thread = std::jthread([this, worker_ptr](std::stop_token stop_token) {
            std::string failure;
            try
            {
                const auto started_message = "SFTP upload session started: host=" + worker_ptr->host_id +
                                             " upload=" + worker_ptr->id;
                logI(started_message.c_str());
                auto connection = OpenConnection(config_store_, worker_ptr->host_id, true, stop_token);
                const auto normalized = NormalizePath(worker_ptr->path);
                SftpHandlePtr file(libssh2_sftp_open(connection.sftp.get(), normalized.c_str(),
                                                    LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC, 0600));
                if (!file) throw SftpError(connection.sftp.get(), "远程文件创建失败");

                while (!stop_token.stop_requested())
                {
                    UploadWorker::Chunk chunk;
                    {
                        std::unique_lock lock(worker_ptr->mutex);
                        if (!worker_ptr->condition.wait_for(lock, kUploadInactivityTimeout, [&] {
                                return !worker_ptr->chunks.empty() || stop_token.stop_requested();
                            }))
                            throw std::runtime_error("SFTP 上传会话等待分片超时。");
                        if (stop_token.stop_requested()) throw std::runtime_error("上传已取消。");
                        chunk = std::move(worker_ptr->chunks.front());
                        worker_ptr->chunks.pop_front();
                    }

                    try
                    {
                        libssh2_sftp_seek64(file.get(), chunk.offset);
                        std::size_t chunk_offset = 0;
                        while (chunk_offset < chunk.content.size() && !stop_token.stop_requested())
                        {
                            const auto written = libssh2_sftp_write(file.get(), chunk.content.data() + chunk_offset,
                                                                    chunk.content.size() - chunk_offset);
                            if (written < 0) throw SftpError(connection.sftp.get(), "远程文件写入失败");
                            chunk_offset += static_cast<std::size_t>(written);
                        }
                        if (stop_token.stop_requested()) throw std::runtime_error("上传已取消。");
                        const auto completed = chunk.offset + chunk.content.size() == worker_ptr->total_size;
                        if (completed)
                        {
                            LIBSSH2_SFTP_ATTRIBUTES attributes{};
                            if (libssh2_sftp_fstat(file.get(), &attributes) != 0 ||
                                (attributes.flags & LIBSSH2_SFTP_ATTR_SIZE) == 0 ||
                                attributes.filesize != worker_ptr->total_size)
                                throw std::runtime_error("上传完成后远端文件大小校验失败。");
                        }
                        chunk.on_complete("");
                        if (completed) break;
                    }
                    catch (const std::exception& error)
                    {
                        chunk.on_complete(error.what());
                        throw;
                    }
                }
            }
            catch (const std::exception& error)
            {
                failure = error.what();
            }

            std::deque<UploadWorker::Chunk> pending;
            {
                std::lock_guard lock(worker_ptr->mutex);
                pending.swap(worker_ptr->chunks);
            }
            for (auto& chunk : pending) chunk.on_complete(failure.empty() ? "上传已取消。" : failure);
            const auto finished_message = "SFTP upload session finished: host=" + worker_ptr->host_id +
                                          " upload=" + worker_ptr->id +
                                          (failure.empty() ? " status=ok" : " error=" + failure);
            if (failure.empty()) logI(finished_message.c_str());
            else logE(finished_message.c_str());
            worker_ptr->finished = true;
        });
    }
    worker->condition.notify_one();
}

void SftpService::StartDownload(const std::string& download_id,
                                const std::string& host_id,
                                const std::string& path,
                                std::function<bool(std::string_view)> on_chunk,
                                std::function<void(const std::string&)> on_complete)
{
    if (download_id.empty() || download_id.size() > 128) throw std::runtime_error("SFTP 下载任务标识无效。");
    auto worker = std::make_shared<DownloadWorker>();
    worker->id = download_id;
    worker->host_id = host_id;
    worker->path = path;
    {
        std::lock_guard lock(downloads_mutex_);
        if (downloads_.contains(download_id)) throw std::runtime_error("SFTP 下载任务已经存在。");
        downloads_.emplace(download_id, worker);
    }
    auto* const worker_ptr = worker.get();
    worker->thread = std::jthread(
        [this, worker_ptr, host_id, path, on_chunk = std::move(on_chunk),
         on_complete = std::move(on_complete)](std::stop_token stop_token) mutable {
            std::string error_message;
            try
            {
                {
                    std::lock_guard lock(worker_ptr->state_mutex);
                    worker_ptr->status = "downloading";
                }
                auto connection = OpenConnection(config_store_, host_id, true, stop_token);
                const auto normalized = NormalizePath(path);
                SftpHandlePtr file(libssh2_sftp_open(connection.sftp.get(), normalized.c_str(), LIBSSH2_FXF_READ, 0));
                if (!file) throw SftpError(connection.sftp.get(), "远程文件打开失败");
                LIBSSH2_SFTP_ATTRIBUTES attributes{};
                if (libssh2_sftp_fstat(file.get(), &attributes) != 0 ||
                    (attributes.flags & LIBSSH2_SFTP_ATTR_SIZE) == 0)
                    throw std::runtime_error("无法读取远程文件大小。");
                worker_ptr->total = attributes.filesize;
                if (attributes.filesize > kMaximumSftpDownloadSize)
                    throw std::runtime_error("SFTP 下载文件不能超过 2 GiB。");
                std::size_t downloaded = 0;
                std::array<char, 65536> buffer{};
                while (!stop_token.stop_requested())
                {
                    const auto count = libssh2_sftp_read(file.get(), buffer.data(), buffer.size());
                    if (count == 0) break;
                    if (count < 0) throw SftpError(connection.sftp.get(), "远程文件读取失败");
                    downloaded += static_cast<std::size_t>(count);
                    if (!on_chunk({buffer.data(), static_cast<std::size_t>(count)}))
                        throw std::runtime_error("浏览器已关闭下载连接。");
                    worker_ptr->downloaded = downloaded;
                }
                if (!stop_token.stop_requested() && downloaded != attributes.filesize)
                    throw std::runtime_error("下载不完整：远端文件大小与已发送字节数不一致。");
                if (stop_token.stop_requested()) error_message = "下载已取消。";
            }
            catch (const std::exception& error)
            {
                error_message = error.what();
            }
            on_complete(error_message);
            {
                std::lock_guard lock(worker_ptr->state_mutex);
                worker_ptr->error = error_message;
                worker_ptr->status = error_message.empty() ? "success" : "error";
            }
            worker_ptr->finished = true;
        });
}

nlohmann::json SftpService::DownloadState(const std::string& download_id) const
{
    std::shared_ptr<DownloadWorker> worker;
    {
        std::lock_guard lock(downloads_mutex_);
        const auto found = downloads_.find(download_id);
        if (found == downloads_.end()) return {{"found", false}};
        worker = found->second;
    }
    std::lock_guard lock(worker->state_mutex);
    return {{"found", true},
            {"id", worker->id},
            {"status", worker->status},
            {"downloadedBytes", worker->downloaded.load()},
            {"totalBytes", worker->total.load()},
            {"message", worker->error}};
}

void SftpService::RemoveDownload(const std::string& download_id)
{
    std::shared_ptr<DownloadWorker> worker;
    {
        std::lock_guard lock(downloads_mutex_);
        const auto found = downloads_.find(download_id);
        if (found == downloads_.end()) return;
        {
            std::lock_guard state_lock(found->second->state_mutex);
            if (found->second->status != "success" && found->second->status != "error")
                throw std::runtime_error("下载任务仍在进行中。");
        }
        worker = std::move(found->second);
        downloads_.erase(found);
    }
    if (worker->thread.joinable()) worker->thread.join();
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
