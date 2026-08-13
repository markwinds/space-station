#include "telnet/telnet_session.hpp"
#include "logging/logger.hpp"
#include "ssh/network_utils.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace spacestation::telnet
{
namespace
{
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr unsigned char kIac = 255;
constexpr unsigned char kDont = 254;
constexpr unsigned char kDo = 253;
constexpr unsigned char kWont = 252;
constexpr unsigned char kWill = 251;
constexpr unsigned char kSb = 250;
constexpr unsigned char kSe = 240;
constexpr unsigned char kBinary = 0;
constexpr unsigned char kEcho = 1;
constexpr unsigned char kSuppressGoAhead = 3;
constexpr unsigned char kTerminalType = 24;
constexpr unsigned char kNaws = 31;
constexpr unsigned char kTerminalTypeIs = 0;
constexpr unsigned char kTerminalTypeSend = 1;

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
void CloseSocket(Socket value) { if (value != kInvalidSocket) closesocket(value); }
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;
void CloseSocket(Socket value) { if (value != kInvalidSocket) close(value); }
#endif

class SocketHandle
{
  public:
    ~SocketHandle() { CloseSocket(value); }
    Socket value = kInvalidSocket;
};

void SetNonBlocking(Socket socket_value)
{
#ifdef _WIN32
    u_long enabled = 1;
    ioctlsocket(socket_value, FIONBIO, &enabled);
#else
    fcntl(socket_value, F_SETFL, fcntl(socket_value, F_GETFL, 0) | O_NONBLOCK);
#endif
}

bool WouldBlock()
{
#ifdef _WIN32
    const auto error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS;
#endif
}

Socket ConnectSocket(const std::string& host, int port, std::stop_token stop_token)
{
    const auto deadline = Clock::now() + 15s;
    const auto addresses = ssh::network::Resolve(host, port, stop_token, deadline,
        "Telnet 连接已取消。", "Telnet DNS 解析超时。", "无法解析 Telnet 主机");
    for (const auto& address : addresses)
    {
        SocketHandle socket_handle;
        socket_handle.value = socket(address.family, address.socket_type, address.protocol);
        if (socket_handle.value == kInvalidSocket) continue;
        SetNonBlocking(socket_handle.value);
#ifdef _WIN32
        const auto address_length = static_cast<int>(address.length);
#else
        const auto address_length = static_cast<socklen_t>(address.length);
#endif
        if (connect(socket_handle.value, reinterpret_cast<const sockaddr*>(&address.address), address_length) == 0)
        {
            return std::exchange(socket_handle.value, kInvalidSocket);
        }
        if (!WouldBlock()) continue;
        while (!stop_token.stop_requested() && Clock::now() < deadline)
        {
            fd_set writes;
            FD_ZERO(&writes);
            FD_SET(socket_handle.value, &writes);
            timeval wait{0, 100'000};
            if (select(static_cast<int>(socket_handle.value) + 1, nullptr, &writes, nullptr, &wait) <= 0) continue;
            int error = 0;
#ifdef _WIN32
            int length = sizeof(error);
#else
            socklen_t length = sizeof(error);
#endif
            getsockopt(socket_handle.value, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length);
            if (error == 0) return std::exchange(socket_handle.value, kInvalidSocket);
            break;
        }
    }
    if (stop_token.stop_requested()) throw std::runtime_error("Telnet 连接已取消。");
    throw std::runtime_error("无法连接 Telnet 主机或连接超时。");
}

std::string EscapeIac(std::string_view input)
{
    std::string output;
    output.reserve(input.size());
    for (const auto value : input)
    {
        output.push_back(value);
        if (static_cast<unsigned char>(value) == kIac) output.push_back(value);
    }
    return output;
}

std::string Negotiation(unsigned char action, unsigned char option)
{
    return {static_cast<char>(kIac), static_cast<char>(action), static_cast<char>(option)};
}

std::string WindowSize(int columns, int rows)
{
    columns = std::clamp(columns, 2, 1000);
    rows = std::clamp(rows, 2, 1000);
    std::string result{static_cast<char>(kIac), static_cast<char>(kSb), static_cast<char>(kNaws)};
    const auto append = [&](unsigned char value) {
        result.push_back(static_cast<char>(value));
        if (value == kIac) result.push_back(static_cast<char>(value));
    };
    append(static_cast<unsigned char>((columns >> 8) & 0xff));
    append(static_cast<unsigned char>(columns & 0xff));
    append(static_cast<unsigned char>((rows >> 8) & 0xff));
    append(static_cast<unsigned char>(rows & 0xff));
    result.push_back(static_cast<char>(kIac));
    result.push_back(static_cast<char>(kSe));
    return result;
}

bool SendAll(Socket socket_value, std::string_view data, std::stop_token stop_token)
{
    std::size_t offset = 0;
    while (offset < data.size() && !stop_token.stop_requested())
    {
        const auto sent = send(socket_value, data.data() + offset,
                               static_cast<int>(data.size() - offset), 0);
        if (sent > 0) { offset += static_cast<std::size_t>(sent); continue; }
        if (!WouldBlock()) return false;
        std::this_thread::sleep_for(10ms);
    }
    return offset == data.size();
}

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool EndsWithPrompt(std::string_view value, std::string_view prompt)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
    return value.ends_with(prompt);
}
} // namespace

TelnetSession::TelnetSession(ConfigStore& config_store, drogon::WebSocketConnectionPtr connection)
    : config_store_(config_store), connection_(std::move(connection))
{
}

TelnetSession::~TelnetSession() { Stop(); }

void TelnetSession::Start(TelnetConnectOptions options)
{
    Stop();
    worker_ = std::jthread([this, options = std::move(options)](std::stop_token token) mutable {
        Run(token, std::move(options));
    });
}

void TelnetSession::Write(std::string data)
{
    std::lock_guard lock(mutex_);
    commands_.push_back({false, std::move(data), 0, 0});
    condition_.notify_all();
}

void TelnetSession::Resize(int columns, int rows)
{
    std::lock_guard lock(mutex_);
    commands_.push_back({true, {}, columns, rows});
    condition_.notify_all();
}

void TelnetSession::Stop()
{
    if (!worker_.joinable()) return;
    worker_.request_stop();
    condition_.notify_all();
    worker_.join();
}

void TelnetSession::SendEvent(const nlohmann::json& event) const
{
    if (const auto connection = connection_.lock()) connection->send(event.dump());
}

void TelnetSession::Run(std::stop_token stop_token, TelnetConnectOptions options)
{
    SocketHandle socket_handle;
    try
    {
        SendEvent({{"type", "status"}, {"status", "connecting"}, {"message", "正在连接 Telnet…"}});
        socket_handle.value = ConnectSocket(options.host, options.port, stop_token);
        if (options.save_credential && !options.password.empty())
        {
            config_store_.SaveSshCredential(options.host_id, {{"method", "password"}, {"password", options.password},
                                                              {"privateKey", ""}, {"passphrase", ""}});
        }
        SendAll(socket_handle.value, Negotiation(kWill, kTerminalType) + Negotiation(kWill, kNaws) +
                                     Negotiation(kDo, kSuppressGoAhead) + Negotiation(kWill, kSuppressGoAhead) +
                                     WindowSize(options.columns, options.rows), stop_token);
        SendEvent({{"type", "status"}, {"status", "connected"}, {"message", "Telnet 已连接"}});

        enum class ParseState { Data, Iac, Option, Sub, SubIac };
        ParseState parse_state = ParseState::Data;
        unsigned char negotiation_action = 0;
        std::string subnegotiation;
        std::string prompt_buffer;
        bool username_sent = options.username.empty();
        bool password_sent = options.password.empty();
        std::array<char, 32768> buffer{};
        while (!stop_token.stop_requested())
        {
            std::deque<Command> commands;
            {
                std::lock_guard lock(mutex_);
                commands.swap(commands_);
            }
            for (auto& command : commands)
            {
                const auto data = command.resize ? WindowSize(command.columns, command.rows) : EscapeIac(command.data);
                if (!SendAll(socket_handle.value, data, stop_token)) throw std::runtime_error("Telnet 写入失败。");
            }

            fd_set reads;
            FD_ZERO(&reads);
            FD_SET(socket_handle.value, &reads);
            timeval wait{0, 50'000};
            const auto ready = select(static_cast<int>(socket_handle.value) + 1, &reads, nullptr, nullptr, &wait);
            if (ready < 0) throw std::runtime_error("Telnet 连接读取失败。");
            if (ready == 0) continue;
            const auto count = recv(socket_handle.value, buffer.data(), static_cast<int>(buffer.size()), 0);
            if (count == 0) break;
            if (count < 0)
            {
                if (WouldBlock()) continue;
                throw std::runtime_error("Telnet 连接读取失败。");
            }
            std::string output;
            for (int index = 0; index < count; ++index)
            {
                const auto value = static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)]);
                if (parse_state == ParseState::Data)
                {
                    if (value == kIac) parse_state = ParseState::Iac;
                    else output.push_back(static_cast<char>(value));
                }
                else if (parse_state == ParseState::Iac)
                {
                    if (value == kIac) { output.push_back(static_cast<char>(value)); parse_state = ParseState::Data; }
                    else if (value == kWill || value == kWont || value == kDo || value == kDont)
                    { negotiation_action = value; parse_state = ParseState::Option; }
                    else if (value == kSb) { subnegotiation.clear(); parse_state = ParseState::Sub; }
                    else parse_state = ParseState::Data;
                }
                else if (parse_state == ParseState::Option)
                {
                    const auto accepted_remote = value == kBinary || value == kEcho || value == kSuppressGoAhead;
                    const auto accepted_local = value == kBinary || value == kSuppressGoAhead || value == kTerminalType || value == kNaws;
                    unsigned char response = kWont;
                    if (negotiation_action == kWill) response = accepted_remote ? kDo : kDont;
                    else if (negotiation_action == kWont) response = kDont;
                    else if (negotiation_action == kDo) response = accepted_local ? kWill : kWont;
                    else if (negotiation_action == kDont) response = kWont;
                    SendAll(socket_handle.value, Negotiation(response, value), stop_token);
                    if (negotiation_action == kDo && value == kNaws)
                        SendAll(socket_handle.value, WindowSize(options.columns, options.rows), stop_token);
                    parse_state = ParseState::Data;
                }
                else if (parse_state == ParseState::Sub)
                {
                    if (value == kIac) parse_state = ParseState::SubIac;
                    else subnegotiation.push_back(static_cast<char>(value));
                }
                else
                {
                    if (value == kSe)
                    {
                        if (subnegotiation.size() >= 2 && static_cast<unsigned char>(subnegotiation[0]) == kTerminalType &&
                            static_cast<unsigned char>(subnegotiation[1]) == kTerminalTypeSend)
                        {
                            const std::string terminal = "xterm-256color";
                            SendAll(socket_handle.value, std::string{static_cast<char>(kIac), static_cast<char>(kSb),
                                static_cast<char>(kTerminalType), static_cast<char>(kTerminalTypeIs)} + terminal +
                                std::string{static_cast<char>(kIac), static_cast<char>(kSe)}, stop_token);
                        }
                        parse_state = ParseState::Data;
                    }
                    else if (value == kIac) { subnegotiation.push_back(static_cast<char>(value)); parse_state = ParseState::Sub; }
                    else parse_state = ParseState::Sub;
                }
            }
            if (!output.empty())
            {
                if (const auto connection = connection_.lock()) connection->send(output, drogon::WebSocketMessageType::Binary);
                prompt_buffer += Lower(output);
                if (prompt_buffer.size() > 2048) prompt_buffer.erase(0, prompt_buffer.size() - 2048);
                if (!username_sent && (EndsWithPrompt(prompt_buffer, "login:") ||
                                        EndsWithPrompt(prompt_buffer, "username:") ||
                                        EndsWithPrompt(prompt_buffer, "user:")))
                {
                    SendAll(socket_handle.value, EscapeIac(options.username + "\r\n"), stop_token);
                    username_sent = true;
                    prompt_buffer.clear();
                }
                else if (!password_sent && (EndsWithPrompt(prompt_buffer, "password:") ||
                                             EndsWithPrompt(prompt_buffer, "passcode:")))
                {
                    SendAll(socket_handle.value, EscapeIac(options.password + "\r\n"), stop_token);
                    password_sent = true;
                    std::fill(options.password.begin(), options.password.end(), '\0');
                    prompt_buffer.clear();
                }
            }
        }
        std::fill(options.password.begin(), options.password.end(), '\0');
        if (!stop_token.stop_requested()) SendEvent({{"type", "status"}, {"status", "closed"}, {"message", "Telnet 连接已关闭"}});
    }
    catch (const std::exception& error)
    {
        std::fill(options.password.begin(), options.password.end(), '\0');
        if (!stop_token.stop_requested()) SendEvent({{"type", "error"}, {"message", error.what()}});
    }
}
} // namespace spacestation::telnet
