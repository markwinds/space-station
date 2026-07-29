#include "ssh/ssh_session.hpp"
#include "ssh/network_utils.hpp"

#include "logging/logger.hpp"
#include "plugins/terminal_plugin_service.hpp"

#include <libssh2.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace spacestation::ssh
{
namespace
{
using namespace std::chrono_literals;
using Deadline = std::chrono::steady_clock::time_point;
constexpr auto kConnectionTimeout = 15s;
constexpr std::size_t kDetachedOutputBufferBytes = 4ULL * 1024ULL * 1024ULL;
constexpr std::array<char, 4> kOutputFrameMagic{'S', 'S', 'O', '1'};

std::string NextPluginSessionId(const std::string& host_id)
{
    static std::atomic<std::uint64_t> counter{0};
    return "ssh:" + host_id + ":" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
           std::to_string(++counter);
}

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
        if (this != &other)
        {
            SocketHandle old;
            old.value = std::exchange(value, LIBSSH2_INVALID_SOCKET);
            value = std::exchange(other.value, LIBSSH2_INVALID_SOCKET);
        }
        return *this;
    }

    ~SocketHandle()
    {
        if (value == LIBSSH2_INVALID_SOCKET)
        {
            return;
        }
#ifdef _WIN32
        closesocket(value);
#else
        close(value);
#endif
    }
};

struct SessionDeleter
{
    void operator()(LIBSSH2_SESSION* session) const
    {
        if (session)
        {
            libssh2_session_free(session);
        }
    }
};

struct ChannelDeleter
{
    void operator()(LIBSSH2_CHANNEL* channel) const
    {
        if (channel)
        {
            libssh2_channel_free(channel);
        }
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
using ChannelPtr = std::unique_ptr<LIBSSH2_CHANNEL, ChannelDeleter>;
using AgentPtr = std::unique_ptr<LIBSSH2_AGENT, AgentDeleter>;

void EnsureLibssh2Initialized()
{
    static const int initialized = [] {
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            return -1;
        }
#endif
        return libssh2_init(0);
    }();
    if (initialized != 0)
    {
        throw std::runtime_error("SSH 库初始化失败。");
    }
}

void SetSocketNonBlocking(libssh2_socket_t socket_value)
{
#ifdef _WIN32
    u_long enabled = 1;
    ioctlsocket(socket_value, FIONBIO, &enabled);
#else
    const auto flags = fcntl(socket_value, F_GETFL, 0);
    if (flags >= 0) fcntl(socket_value, F_SETFL, flags | O_NONBLOCK);
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
                                            "SSH DNS 解析超时。", "无法解析 SSH 主机地址。");
    for (const auto& address : addresses)
    {
        SocketHandle socket_handle;
        socket_handle.value = socket(address.family, address.socket_type, address.protocol);
        if (socket_handle.value == LIBSSH2_INVALID_SOCKET)
        {
            continue;
        }
        SetSocketNonBlocking(socket_handle.value);
        if (connect(socket_handle.value, reinterpret_cast<const sockaddr*>(&address.address),
                    static_cast<int>(address.length)) == 0)
        {
            return socket_handle;
        }
        if (ConnectInProgress() && WaitForSocketConnection(socket_handle.value, stop_token, deadline, "SSH TCP 连接超时。"))
            return socket_handle;
        CheckConnectionDeadline(stop_token, deadline, "SSH TCP 连接超时。");
    }
    throw std::runtime_error("无法连接 SSH 主机。");
}

void WaitSocket(libssh2_socket_t socket, LIBSSH2_SESSION* session)
{
    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;
    fd_set read_set;
    fd_set write_set;
    FD_ZERO(&read_set);
    FD_ZERO(&write_set);
    const auto directions = libssh2_session_block_directions(session);
    if ((directions & LIBSSH2_SESSION_BLOCK_INBOUND) != 0)
    {
        FD_SET(socket, &read_set);
    }
    if ((directions & LIBSSH2_SESSION_BLOCK_OUTBOUND) != 0)
    {
        FD_SET(socket, &write_set);
    }
    select(static_cast<int>(socket + 1), &read_set, &write_set, nullptr, &timeout);
}

template <typename Operation>
int Retry(libssh2_socket_t socket, LIBSSH2_SESSION* session, std::stop_token stop_token,
          Deadline deadline, const char* timeout_message, Operation&& operation)
{
    int result = LIBSSH2_ERROR_EAGAIN;
    while (!stop_token.stop_requested() && result == LIBSSH2_ERROR_EAGAIN)
    {
        CheckConnectionDeadline(stop_token, deadline, timeout_message);
        result = operation();
        if (result == LIBSSH2_ERROR_EAGAIN)
        {
            WaitSocket(socket, session);
        }
    }
    CheckConnectionDeadline(stop_token, deadline, timeout_message);
    return result;
}

std::string LastSessionError(LIBSSH2_SESSION* session, std::string fallback)
{
    char* message = nullptr;
    int length = 0;
    libssh2_session_last_error(session, &message, &length, 0);
    if (message && length > 0)
    {
        return std::string(message, static_cast<std::size_t>(length));
    }
    return fallback;
}

std::string HostFingerprint(LIBSSH2_SESSION* session)
{
    std::size_t key_length = 0;
    int key_type = 0;
    const auto* key = libssh2_session_hostkey(session, &key_length, &key_type);
    if (!key || key_length == 0)
    {
        throw std::runtime_error("无法读取 SSH 主机公钥。");
    }
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    SHA256(reinterpret_cast<const unsigned char*>(key), key_length, digest.data());
    std::array<unsigned char, 4 * ((SHA256_DIGEST_LENGTH + 2) / 3) + 1> encoded{};
    const auto encoded_length = EVP_EncodeBlock(encoded.data(), digest.data(), digest.size());
    auto value = std::string(reinterpret_cast<const char*>(encoded.data()), static_cast<std::size_t>(encoded_length));
    while (!value.empty() && value.back() == '=')
    {
        value.pop_back();
    }
    return "SHA256:" + value;
}

int ClampTerminalSize(int value, int fallback)
{
    return std::clamp(value > 0 ? value : fallback, 2, 1000);
}

LIBSSH2_RECV_FUNC(ChannelReceive)
{
    const auto channel = static_cast<LIBSSH2_CHANNEL*>(*abstract);
    const auto result = libssh2_channel_read(channel, static_cast<char*>(buffer), length);
    if (result == LIBSSH2_ERROR_EAGAIN || result == LIBSSH2_ERROR_TIMEOUT)
    {
        return -EAGAIN;
    }
    return result;
}

LIBSSH2_SEND_FUNC(ChannelSend)
{
    const auto channel = static_cast<LIBSSH2_CHANNEL*>(*abstract);
    const auto result = libssh2_channel_write(channel, static_cast<const char*>(buffer), length);
    if (result == LIBSSH2_ERROR_EAGAIN || result == LIBSSH2_ERROR_TIMEOUT)
    {
        return -EAGAIN;
    }
    return result;
}

int Authenticate(LIBSSH2_SESSION* session,
                 libssh2_socket_t socket,
                 std::stop_token stop_token,
                 const std::string& username,
                 const std::string& password,
                 const std::string& private_key,
                 const std::string& passphrase,
                 bool use_agent,
                 Deadline deadline)
{
    if (use_agent)
    {
        AgentPtr agent(libssh2_agent_init(session));
        if (!agent ||
            Retry(socket, session, stop_token, deadline, "SSH Agent 连接超时。",
                  [&] { return libssh2_agent_connect(agent.get()); }) != 0 ||
            Retry(socket, session, stop_token, deadline, "SSH Agent 身份读取超时。",
                  [&] { return libssh2_agent_list_identities(agent.get()); }) != 0)
            throw std::runtime_error("无法连接 SSH Agent，请检查 SSH_AUTH_SOCK。");
        libssh2_agent_publickey* identity = nullptr;
        libssh2_agent_publickey* previous = nullptr;
        int result = LIBSSH2_ERROR_AUTHENTICATION_FAILED;
        while (libssh2_agent_get_identity(agent.get(), &identity, previous) == 0)
        {
            CheckConnectionDeadline(stop_token, deadline, "SSH Agent 认证超时。");
            result = Retry(socket, session, stop_token, deadline, "SSH Agent 认证超时。",
                           [&] { return libssh2_agent_userauth(agent.get(), username.c_str(), identity); });
            if (result == 0) break;
            previous = identity;
        }
        return result;
    }
    if (!private_key.empty())
    {
        return Retry(socket, session, stop_token, deadline, "SSH 私钥认证超时。", [&] {
            return libssh2_userauth_publickey_frommemory(
                session, username.c_str(), username.size(), nullptr, 0, private_key.data(), private_key.size(),
                passphrase.empty() ? nullptr : passphrase.c_str());
        });
    }
    return Retry(socket, session, stop_token, deadline, "SSH 密码认证超时。", [&] {
        return libssh2_userauth_password_ex(session, username.c_str(), static_cast<unsigned int>(username.size()),
                                            password.c_str(), static_cast<unsigned int>(password.size()), nullptr);
    });
}
} // namespace

SshSession::SshSession(ConfigStore& config_store,
                       drogon::WebSocketConnectionPtr connection,
                       std::string connection_id,
                       plugins::TerminalPluginService* plugin_service)
    : config_store_(config_store),
      plugin_service_(plugin_service),
      connection_(std::move(connection)),
      connection_id_(std::move(connection_id))
{
}

SshSession::~SshSession()
{
    Stop();
}

void SshSession::Start(SshConnectOptions options)
{
    if (worker_.joinable())
    {
        SendEvent({{"type", "error"}, {"message", "当前终端已经开始连接。"}});
        return;
    }
    if (plugin_service_)
    {
        plugin_session_id_ = NextPluginSessionId(options.host_id);
        plugin_service_->RegisterSession(
            plugin_session_id_, "ssh", options.host_id,
            [weak = weak_from_this()](std::string data) {
                const auto session = weak.lock();
                if (!session) throw std::runtime_error("SSH 会话已经关闭。");
                session->Write(std::move(data));
            });
    }
    worker_ = std::jthread([this, options = std::move(options)](std::stop_token token) mutable {
        Run(token, std::move(options));
    });
}

void SshSession::Write(std::string data)
{
    if (data.empty())
    {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        commands_.push_back({CommandType::Input, std::move(data)});
    }
    condition_.notify_all();
}

void SshSession::Resize(int columns, int rows)
{
    {
        std::lock_guard lock(mutex_);
        commands_.push_back({CommandType::Resize, {}, ClampTerminalSize(columns, 100), ClampTerminalSize(rows, 30)});
    }
    condition_.notify_all();
}

void SshSession::ConfirmHostKey(bool trusted)
{
    {
        std::lock_guard lock(mutex_);
        trust_answered_ = true;
        host_trusted_ = trusted;
    }
    condition_.notify_all();
}

SshSession::ResumeResult SshSession::Attach(drogon::WebSocketConnectionPtr connection,
                                            std::string connection_id,
                                            std::uint64_t last_sequence)
{
    drogon::WebSocketConnectionPtr previous;
    ResumeResult result;
    {
        std::lock_guard lock(mutex_);
        previous = connection_.lock();
        const auto latest_sequence = next_output_sequence_ - 1;
        const auto earliest_sequence = output_buffer_.empty() ? next_output_sequence_ : output_buffer_.front().sequence;
        result.latest_sequence = latest_sequence;
        result.earliest_sequence = earliest_sequence;
        result.gap = last_sequence < latest_sequence && last_sequence + 1 < earliest_sequence;

        while (!output_buffer_.empty() && output_buffer_.front().sequence <= last_sequence)
        {
            output_buffer_bytes_ -= output_buffer_.front().data.size();
            output_buffer_.pop_front();
        }

        connection_ = connection;
        connection_id_ = std::move(connection_id);
        detached_at_.reset();
        for (const auto& buffered : output_buffer_)
        {
            connection->send(buffered.data, buffered.type);
            ++result.replayed_messages;
        }
    }
    if (previous && previous != connection && previous->connected())
    {
        previous->shutdown(drogon::CloseCode::kEndpointGone, "SSH session resumed elsewhere");
    }
    return result;
}

bool SshSession::Detach(const std::string& connection_id)
{
    std::lock_guard lock(mutex_);
    if (connection_id_ != connection_id)
    {
        return false;
    }
    connection_.reset();
    connection_id_.clear();
    detached_at_ = std::chrono::steady_clock::now();
    return true;
}

bool SshSession::IsAttachedTo(const std::string& connection_id) const
{
    std::lock_guard lock(mutex_);
    return !connection_id.empty() && connection_id_ == connection_id && !connection_.expired();
}

bool SshSession::DetachedFor(std::chrono::steady_clock::duration duration) const
{
    std::lock_guard lock(mutex_);
    return detached_at_.has_value() && std::chrono::steady_clock::now() - *detached_at_ >= duration;
}

void SshSession::Stop()
{
    if (worker_.joinable())
    {
        worker_.request_stop();
        condition_.notify_all();
        worker_.join();
    }
    if (plugin_service_ && !plugin_session_id_.empty())
    {
        plugin_service_->UnregisterSession(plugin_session_id_);
        plugin_session_id_.clear();
    }
}

void SshSession::Run(std::stop_token stop_token, SshConnectOptions options)
{
    try
    {
        EnsureLibssh2Initialized();
        SendEvent({{"type", "status"}, {"status", "connecting"}, {"message", "正在连接…"}});
        SocketHandle socket;
        SessionPtr jump_session;
        ChannelPtr jump_channel;
        std::string jump_host_name;
        if (!options.jump_host_id.empty())
        {
            nlohmann::json jump_host;
            for (const auto& candidate : config_store_.LoadSshHosts())
                if (candidate.value("id", "") == options.jump_host_id) { jump_host = candidate; break; }
            if (!jump_host.is_object()) throw std::runtime_error("找不到配置的跳板机。");
            jump_host_name = jump_host.value("name", jump_host.value("host", options.jump_host_id));
            SendEvent({{"type", "status"}, {"status", "connecting"},
                       {"message", "正在连接跳板机 " + jump_host_name + "…"}});
            const auto route_message = "SSH route: target=" + options.host_id + " via=" + options.jump_host_id;
            logI(route_message.c_str());
            const auto jump_fingerprint = jump_host.value("hostKeySha256", "");
            if (jump_fingerprint.empty()) throw std::runtime_error("请先直接连接并信任跳板机主机指纹。");
            const auto jump_credential = config_store_.LoadSshCredential(options.jump_host_id);
            const auto jump_use_agent = jump_host.value("useAgent", false);
            if (!jump_credential && !jump_use_agent) throw std::runtime_error("请先保存跳板机凭据或启用 SSH Agent。");
            socket = ConnectSocket(jump_host.value("host", ""), std::clamp(jump_host.value("port", 22), 1, 65535),
                                   stop_token, NewDeadline());
            jump_session.reset(libssh2_session_init());
            if (!jump_session) throw std::runtime_error("无法创建跳板机 SSH 会话。");
            libssh2_session_set_blocking(jump_session.get(), 0);
            if (Retry(socket.value, jump_session.get(), stop_token, NewDeadline(), "跳板机 SSH 握手超时。",
                      [&] { return libssh2_session_handshake(jump_session.get(), socket.value); }) != 0)
                throw std::runtime_error(LastSessionError(jump_session.get(), "跳板机 SSH 握手失败。"));
            if (HostFingerprint(jump_session.get()) != jump_fingerprint)
                throw std::runtime_error("跳板机主机指纹已经变化，连接已中止。");
            const auto jump_username = jump_host.value("username", "");
            if (Authenticate(jump_session.get(), socket.value, stop_token, jump_username,
                             jump_credential ? jump_credential->value("password", "") : "",
                             jump_credential ? jump_credential->value("privateKey", "") : "",
                             jump_credential ? jump_credential->value("passphrase", "") : "",
                             jump_use_agent, NewDeadline()) != 0)
                throw std::runtime_error(LastSessionError(jump_session.get(), "跳板机认证失败。"));
            LIBSSH2_CHANNEL* raw_jump_channel = nullptr;
            const auto jump_channel_deadline = NewDeadline();
            while (!stop_token.stop_requested() && !raw_jump_channel)
            {
                CheckConnectionDeadline(stop_token, jump_channel_deadline, "跳板机打开目标连接超时。");
                raw_jump_channel = libssh2_channel_direct_tcpip(jump_session.get(), options.host.c_str(), options.port);
                if (!raw_jump_channel && libssh2_session_last_errno(jump_session.get()) == LIBSSH2_ERROR_EAGAIN)
                {
                    WaitSocket(socket.value, jump_session.get());
                    continue;
                }
                break;
            }
            jump_channel.reset(raw_jump_channel);
            if (!jump_channel) throw std::runtime_error(LastSessionError(jump_session.get(), "跳板机无法打开目标连接。"));
            // The inner libssh2 session uses this channel as its transport. Keep the
            // outer transport blocking while the inner handshake is being built;
            // otherwise an outer-channel EAGAIN can be interpreted by the inner
            // banner reader as an early EOF/socket failure.
            libssh2_session_set_timeout(jump_session.get(), 15000);
            libssh2_session_set_blocking(jump_session.get(), 1);
            SendEvent({{"type", "status"}, {"status", "connecting"},
                       {"message", "正在通过 " + jump_host_name + " 连接目标主机…"}});
        }
        else
        {
            const auto route_message = "SSH route: target=" + options.host_id + " direct";
            logI(route_message.c_str());
            socket = ConnectSocket(options.host, options.port, stop_token, NewDeadline());
        }
        SessionPtr session(options.jump_host_id.empty()
                               ? libssh2_session_init()
                               : libssh2_session_init_ex(nullptr, nullptr, nullptr, jump_channel.get()));
        if (!session)
        {
            throw std::runtime_error("无法创建 SSH 会话。");
        }
        libssh2_session_set_blocking(session.get(), jump_channel ? 1 : 0);
        libssh2_session_set_timeout(session.get(), 15000);
        if (jump_channel)
        {
            libssh2_session_callback_set2(session.get(), LIBSSH2_CALLBACK_RECV,
                                          reinterpret_cast<libssh2_cb_generic*>(ChannelReceive));
            libssh2_session_callback_set2(session.get(), LIBSSH2_CALLBACK_SEND,
                                          reinterpret_cast<libssh2_cb_generic*>(ChannelSend));
        }
        if (Retry(socket.value, session.get(), stop_token, NewDeadline(), "SSH 握手超时。", [&] {
                return libssh2_session_handshake(session.get(), socket.value);
            }) != 0)
        {
            throw std::runtime_error(LastSessionError(session.get(), "SSH 握手失败。"));
        }

        const auto fingerprint = HostFingerprint(session.get());
        if (!options.expected_fingerprint.empty() && options.expected_fingerprint != fingerprint)
        {
            SendEvent({
                {"type", "host-key-mismatch"},
                {"expected", options.expected_fingerprint},
                {"actual", fingerprint},
                {"message", "主机密钥已变化，连接已中止。"},
            });
            return;
        }
        if (options.expected_fingerprint.empty())
        {
            SendEvent({
                {"type", "host-key"},
                {"fingerprint", fingerprint},
                {"host", options.host},
                {"port", options.port},
            });
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [&] { return trust_answered_ || stop_token.stop_requested(); });
            if (stop_token.stop_requested() || !host_trusted_)
            {
                SendEvent({{"type", "status"}, {"status", "closed"}, {"message", "未信任主机密钥。"}});
                return;
            }
            lock.unlock();
            config_store_.SaveSshHostFingerprint(options.host_id, fingerprint);
        }

        SendEvent({{"type", "status"}, {"status", "authenticating"}, {"message", "正在认证…"}});
        const auto auth_result = Authenticate(session.get(), socket.value, stop_token, options.username,
                                              options.password, options.private_key, options.passphrase,
                                              options.use_agent, NewDeadline());
        if (auth_result != 0)
        {
            std::fill(options.password.begin(), options.password.end(), '\0');
            std::fill(options.private_key.begin(), options.private_key.end(), '\0');
            std::fill(options.passphrase.begin(), options.passphrase.end(), '\0');
            throw std::runtime_error(LastSessionError(session.get(), "SSH 认证失败。"));
        }
        if (options.save_credential)
        {
            try
            {
                config_store_.SaveSshCredential(options.host_id, {
                    {"method", options.private_key.empty() ? "password" : "privateKey"},
                    {"password", options.password},
                    {"privateKey", options.private_key},
                    {"passphrase", options.passphrase},
                });
            }
            catch (const std::exception& error)
            {
                const auto log_message = std::string("SSH credential save failed: ") + error.what();
                logE(log_message.c_str());
                SendEvent({{"type", "warning"}, {"message", "SSH 已连接，但凭据保存失败。"}});
            }
        }
        std::fill(options.password.begin(), options.password.end(), '\0');
        std::fill(options.private_key.begin(), options.private_key.end(), '\0');
        std::fill(options.passphrase.begin(), options.passphrase.end(), '\0');

        LIBSSH2_CHANNEL* raw_channel = nullptr;
        const auto channel_deadline = NewDeadline();
        while (!stop_token.stop_requested() && !raw_channel)
        {
            CheckConnectionDeadline(stop_token, channel_deadline, "打开 SSH Channel 超时。");
            raw_channel = libssh2_channel_open_session(session.get());
            if (!raw_channel && libssh2_session_last_errno(session.get()) == LIBSSH2_ERROR_EAGAIN)
            {
                WaitSocket(socket.value, session.get());
                continue;
            }
            break;
        }
        ChannelPtr channel(raw_channel);
        if (!channel)
        {
            throw std::runtime_error(LastSessionError(session.get(), "无法创建 SSH Channel。"));
        }
        // Interactive programs may emit terminal control sequences through the
        // SSH extended-data (stderr) stream. Merge it into the normal stream so
        // full-screen applications such as Vim cannot stall behind an unread
        // extended-data window.
        if (Retry(socket.value, session.get(), stop_token, NewDeadline(), "配置 SSH 终端输出超时。", [&] {
                return libssh2_channel_handle_extended_data2(channel.get(), LIBSSH2_CHANNEL_EXTENDED_DATA_MERGE);
            }) != 0)
        {
            throw std::runtime_error(LastSessionError(session.get(), "无法配置 SSH 终端输出。"));
        }
        options.columns = ClampTerminalSize(options.columns, 100);
        options.rows = ClampTerminalSize(options.rows, 30);
        if (Retry(socket.value, session.get(), stop_token, NewDeadline(), "请求 SSH PTY 超时。", [&] {
                return libssh2_channel_request_pty_ex(channel.get(), "xterm-256color", 14, nullptr, 0,
                                                      options.columns, options.rows, 0, 0);
            }) != 0 ||
            Retry(socket.value, session.get(), stop_token, NewDeadline(), "启动远程 Shell 超时。",
                  [&] { return libssh2_channel_shell(channel.get()); }) != 0)
        {
            throw std::runtime_error(LastSessionError(session.get(), "无法启动远程 Shell。"));
        }
        // Both nested sessions can now use non-blocking I/O. The custom transport
        // callbacks return the callback API's required -EAGAIN value when the
        // outer channel would block.
        libssh2_session_set_blocking(session.get(), 0);
        if (jump_session) libssh2_session_set_blocking(jump_session.get(), 0);
        libssh2_keepalive_config(session.get(), 1, 20);
        SendEvent({{"type", "status"}, {"status", "connected"},
                   {"message", jump_host_name.empty() ? "已连接" : "已通过 " + jump_host_name + " 连接"}});

        std::string pending_input;
        std::size_t pending_offset = 0;
        std::optional<std::pair<int, int>> pending_resize;
        std::array<char, 32768> output{};
        auto last_keepalive = std::chrono::steady_clock::now();
        while (!stop_token.stop_requested() && !libssh2_channel_eof(channel.get()))
        {
            std::deque<Command> commands;
            {
                std::lock_guard lock(mutex_);
                commands.swap(commands_);
            }
            for (auto& command : commands)
            {
                if (command.type == CommandType::Resize)
                {
                    // Keep only the latest dimensions, but retry it if libssh2's
                    // non-blocking state machine cannot send the request yet.
                    pending_resize = std::pair(command.columns, command.rows);
                }
                else
                {
                    pending_input.append(command.data);
                }
            }

            if (pending_resize)
            {
                const auto resize_result = libssh2_channel_request_pty_size(
                    channel.get(), pending_resize->first, pending_resize->second);
                if (resize_result == 0)
                {
                    pending_resize.reset();
                }
                else if (resize_result == LIBSSH2_ERROR_EAGAIN)
                {
                    // libssh2 requires the same non-blocking operation to be
                    // retried before starting another channel operation.
                    WaitSocket(socket.value, session.get());
                    continue;
                }
                else
                {
                    const auto resize_message = LastSessionError(session.get(), "调整 SSH 终端大小失败。");
                    logW(resize_message.c_str());
                    pending_resize.reset();
                }
            }

            while (pending_offset < pending_input.size())
            {
                const auto written = libssh2_channel_write(channel.get(), pending_input.data() + pending_offset,
                                                           pending_input.size() - pending_offset);
                if (written == 0 || written == LIBSSH2_ERROR_EAGAIN)
                {
                    break;
                }
                if (written < 0)
                {
                    throw std::runtime_error(LastSessionError(session.get(), "终端输入发送失败。"));
                }
                pending_offset += static_cast<std::size_t>(written);
            }
            if (pending_offset == pending_input.size())
            {
                pending_input.clear();
                pending_offset = 0;
            }

            bool received = false;
            while (true)
            {
                const auto count = libssh2_channel_read(channel.get(), output.data(), output.size());
                if (count > 0)
                {
                    received = true;
                    SendOutput(output.data(), static_cast<std::size_t>(count));
                    continue;
                }
                if (count != 0 && count != LIBSSH2_ERROR_EAGAIN)
                {
                    throw std::runtime_error(LastSessionError(session.get(), "终端输出读取失败。"));
                }
                break;
            }
            if (std::chrono::steady_clock::now() - last_keepalive >= 20s)
            {
                int seconds_to_next = 0;
                const auto keepalive_result = libssh2_keepalive_send(session.get(), &seconds_to_next);
                if (keepalive_result < 0 && keepalive_result != LIBSSH2_ERROR_EAGAIN)
                    throw std::runtime_error(LastSessionError(session.get(), "SSH 保活失败，连接已断开。"));
                if (keepalive_result != LIBSSH2_ERROR_EAGAIN) last_keepalive = std::chrono::steady_clock::now();
            }
            if (!received)
            {
                std::unique_lock lock(mutex_);
                condition_.wait_for(lock, 20ms, [&] { return !commands_.empty() || stop_token.stop_requested(); });
            }
        }

        if (stop_token.stop_requested())
        {
            const auto message = "SSH terminal stopped after WebSocket close: host=" + options.host_id;
            logI(message.c_str());
        }
        else
        {
            const auto message = "SSH remote channel reached EOF: host=" + options.host_id;
            logW(message.c_str());
        }

        libssh2_channel_send_eof(channel.get());
        libssh2_session_disconnect(session.get(), "Space Station terminal closed");
        SendEvent({{"type", "status"}, {"status", "closed"}, {"message", "连接已关闭"}});
    }
    catch (const std::exception& error)
    {
        const auto log_message = "SSH session failed: host=" + options.host_id + " error=" + error.what();
        logE(log_message.c_str());
        SendEvent({{"type", "error"}, {"message", error.what()}});
    }
}

void SshSession::SendEvent(const nlohmann::json& event)
{
    std::lock_guard lock(mutex_);
    auto payload = event;
    const auto sequence = next_output_sequence_++;
    payload["seq"] = sequence;
    BufferedMessage buffered{sequence, drogon::WebSocketMessageType::Text, payload.dump()};
    StoreBufferedMessage(buffered);
    if (const auto connection = connection_.lock(); connection && connection->connected())
    {
        connection->send(buffered.data, buffered.type);
    }
}

void SshSession::SendOutput(const char* data, std::size_t size)
{
    if (plugin_service_ && !plugin_session_id_.empty())
        plugin_service_->OnOutput(plugin_session_id_, std::string(data, size));

    std::lock_guard lock(mutex_);
    const auto sequence = next_output_sequence_++;
    std::string frame(kOutputFrameMagic.begin(), kOutputFrameMagic.end());
    frame.resize(kOutputFrameMagic.size() + sizeof(sequence) + size);
    for (std::size_t index = 0; index < sizeof(sequence); ++index)
    {
        frame[kOutputFrameMagic.size() + index] =
            static_cast<char>((sequence >> ((sizeof(sequence) - index - 1) * 8)) & 0xff);
    }
    std::memcpy(frame.data() + kOutputFrameMagic.size() + sizeof(sequence), data, size);
    BufferedMessage buffered{sequence, drogon::WebSocketMessageType::Binary, std::move(frame)};
    StoreBufferedMessage(buffered);
    if (const auto connection = connection_.lock(); connection && connection->connected())
    {
        connection->send(buffered.data, buffered.type);
    }
}

void SshSession::StoreBufferedMessage(BufferedMessage message)
{
    output_buffer_bytes_ += message.data.size();
    output_buffer_.push_back(std::move(message));
    while (output_buffer_bytes_ > kDetachedOutputBufferBytes && output_buffer_.size() > 1)
    {
        output_buffer_bytes_ -= output_buffer_.front().data.size();
        output_buffer_.pop_front();
    }
}
} // namespace spacestation::ssh
