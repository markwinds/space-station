#include "network/network_scan_websocket.hpp"
#include "logging/logger.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iomanip>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <icmpapi.h>
#include <iphlpapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace spacestation::network
{
namespace
{
using Clock = std::chrono::steady_clock;

struct ScanRequest
{
    std::string cidr;
    std::vector<int> ports;
    int timeout_ms = 800;
    int concurrency = 32;
    bool resolve_names = true;
};

struct ScanState
{
    std::jthread worker;
};

struct PingResult
{
    bool reachable = false;
    std::optional<double> latency_ms;
};

struct PortResult
{
    int port = 0;
    double latency_ms = 0;
};

constexpr std::size_t kMaximumHosts = 4096;
constexpr std::size_t kMaximumPorts = 24;

std::string AddressText(std::uint32_t address)
{
    std::ostringstream stream;
    stream << ((address >> 24) & 0xff) << '.' << ((address >> 16) & 0xff) << '.'
           << ((address >> 8) & 0xff) << '.' << (address & 0xff);
    return stream.str();
}

std::uint32_t ParseAddress(const std::string& text)
{
    in_addr parsed{};
    if (inet_pton(AF_INET, text.c_str(), &parsed) != 1)
        throw std::runtime_error("请输入有效的 IPv4 网段，例如 192.168.1.0/24。");
    return ntohl(parsed.s_addr);
}

std::vector<std::string> ExpandCidr(const std::string& cidr)
{
    const auto slash = cidr.find('/');
    if (slash == std::string::npos) throw std::runtime_error("网段必须使用 CIDR 格式，例如 192.168.1.0/24。");
    const auto address = ParseAddress(cidr.substr(0, slash));
    int prefix = -1;
    try { prefix = std::stoi(cidr.substr(slash + 1)); }
    catch (...) { throw std::runtime_error("CIDR 前缀无效。"); }
    if (prefix < 0 || prefix > 32) throw std::runtime_error("CIDR 前缀必须在 0 到 32 之间。");
    const auto count = std::uint64_t{1} << (32 - prefix);
    const auto usable_count = prefix <= 30 ? count - 2 : count;
    if (usable_count == 0 || usable_count > kMaximumHosts)
        throw std::runtime_error("单次最多扫描 4096 个地址，请缩小网段范围。");
    const auto mask = prefix == 0 ? 0u : 0xffffffffu << (32 - prefix);
    const auto first = (address & mask) + (prefix <= 30 ? 1u : 0u);
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(usable_count));
    for (std::uint64_t index = 0; index < usable_count; ++index)
        result.push_back(AddressText(first + static_cast<std::uint32_t>(index)));
    return result;
}

std::string ServiceName(int port)
{
    static const std::unordered_map<int, std::string> names{
        {21, "FTP"}, {22, "SSH"}, {23, "Telnet"}, {25, "SMTP"}, {53, "DNS"},
        {80, "HTTP"}, {110, "POP3"}, {139, "NetBIOS"}, {143, "IMAP"},
        {443, "HTTPS"}, {445, "SMB"}, {587, "SMTP"}, {993, "IMAPS"},
        {995, "POP3S"}, {1433, "MSSQL"}, {1521, "Oracle"}, {3306, "MySQL"},
        {3389, "RDP"}, {5432, "PostgreSQL"}, {5900, "VNC"}, {6379, "Redis"},
        {8080, "HTTP"}, {8443, "HTTPS"}, {27017, "MongoDB"},
    };
    const auto found = names.find(port);
    return found == names.end() ? "TCP" : found->second;
}

std::string AddressScope(std::uint32_t address)
{
    if ((address & 0xff000000u) == 0x0a000000u ||
        (address & 0xfff00000u) == 0xac100000u ||
        (address & 0xffff0000u) == 0xc0a80000u) return "private";
    if ((address & 0xff000000u) == 0x7f000000u) return "loopback";
    if ((address & 0xffff0000u) == 0xa9fe0000u) return "link-local";
    return "public";
}

std::string ReverseName(const std::string& ip)
{
    sockaddr_in address{};
    address.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip.c_str(), &address.sin_addr) != 1) return {};
    std::array<char, NI_MAXHOST> host{};
    if (getnameinfo(reinterpret_cast<const sockaddr*>(&address), sizeof(address), host.data(), host.size(),
                    nullptr, 0, NI_NAMEREQD) != 0) return {};
    return host.data();
}

PortResult ProbePort(const std::string& ip, int port, int timeout_ms, std::stop_token stop_token)
{
    PortResult result{port, 0};
    if (stop_token.stop_requested()) return result;
    const auto socket_value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (socket_value == INVALID_SOCKET) return result;
    u_long nonblocking = 1;
    ioctlsocket(socket_value, FIONBIO, &nonblocking);
#else
    if (socket_value < 0) return result;
    fcntl(socket_value, F_SETFL, fcntl(socket_value, F_GETFL, 0) | O_NONBLOCK);
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    inet_pton(AF_INET, ip.c_str(), &address.sin_addr);
    const auto started = Clock::now();
    const auto connected = connect(socket_value, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    bool open = connected == 0;
    if (!open)
    {
        const auto deadline = started + std::chrono::milliseconds(timeout_ms);
        while (!stop_token.stop_requested() && Clock::now() < deadline)
        {
            fd_set writes;
            FD_ZERO(&writes);
            FD_SET(socket_value, &writes);
            timeval wait{0, 50'000};
            const auto ready = select(static_cast<int>(socket_value) + 1, nullptr, &writes, nullptr, &wait);
            if (ready <= 0) continue;
            int error = 0;
#ifdef _WIN32
            int length = sizeof(error);
#else
            socklen_t length = sizeof(error);
#endif
            getsockopt(socket_value, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length);
            open = error == 0;
            break;
        }
    }
    if (open) result.latency_ms = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
#ifdef _WIN32
    closesocket(socket_value);
#else
    close(socket_value);
#endif
    return result;
}

#ifdef _WIN32
PingResult Ping(const std::string& ip, int timeout_ms, std::stop_token stop_token)
{
    if (stop_token.stop_requested()) return {};
    const auto handle = IcmpCreateFile();
    if (handle == INVALID_HANDLE_VALUE) return {};
    in_addr address{};
    inet_pton(AF_INET, ip.c_str(), &address);
    std::array<char, sizeof(ICMP_ECHO_REPLY) + 64> reply{};
    const char payload[] = "space-station";
    const auto count = IcmpSendEcho(handle, address.s_addr, const_cast<char*>(payload), sizeof(payload), nullptr,
                                    reply.data(), static_cast<DWORD>(reply.size()), timeout_ms);
    PingResult result;
    if (count)
    {
        const auto* echo = reinterpret_cast<const ICMP_ECHO_REPLY*>(reply.data());
        result.reachable = echo->Status == IP_SUCCESS;
        if (result.reachable) result.latency_ms = static_cast<double>(echo->RoundTripTime);
    }
    IcmpCloseHandle(handle);
    return result;
}
#else
PingResult Ping(const std::string& ip, int timeout_ms, std::stop_token stop_token)
{
    if (stop_token.stop_requested()) return {};
    std::string timeout =
#ifdef __APPLE__
        std::to_string(timeout_ms);
#else
        std::to_string(std::max(1, static_cast<int>(std::ceil(timeout_ms / 1000.0))));
#endif
    std::vector<char*> arguments;
    std::string command = "ping";
    std::string numeric = "-n";
    std::string count = "-c";
    std::string one = "1";
    std::string wait = "-W";
    std::string target = ip;
    for (auto* value : {&command, &numeric, &count, &one, &wait, &timeout, &target}) arguments.push_back(value->data());
    arguments.push_back(nullptr);
    int descriptors[2]{};
    if (pipe(descriptors) != 0) return {};
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, descriptors[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, descriptors[0]);
    pid_t process = 0;
    const auto spawned = posix_spawnp(&process, "ping", &actions, nullptr, arguments.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(descriptors[1]);
    if (spawned != 0) { close(descriptors[0]); return {}; }
    fcntl(descriptors[0], F_SETFL, fcntl(descriptors[0], F_GETFL, 0) | O_NONBLOCK);
    std::string output;
    std::array<char, 512> buffer{};
    int status = 0;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms + 350);
    while (!stop_token.stop_requested() && Clock::now() < deadline)
    {
        const auto bytes = read(descriptors[0], buffer.data(), buffer.size());
        if (bytes > 0) output.append(buffer.data(), static_cast<std::size_t>(bytes));
        if (waitpid(process, &status, WNOHANG) == process) { process = 0; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (process)
    {
        kill(process, SIGKILL);
        waitpid(process, &status, 0);
    }
    for (;;)
    {
        const auto bytes = read(descriptors[0], buffer.data(), buffer.size());
        if (bytes <= 0) break;
        output.append(buffer.data(), static_cast<std::size_t>(bytes));
    }
    close(descriptors[0]);
    if (stop_token.stop_requested() || !WIFEXITED(status) || WEXITSTATUS(status) != 0) return {};
    PingResult result{true, std::nullopt};
    std::smatch match;
    if (std::regex_search(output, match, std::regex(R"(time[=<]([0-9]+(?:\.[0-9]+)?)\s*ms)", std::regex::icase)))
        result.latency_ms = std::stod(match[1].str());
    return result;
}
#endif

ScanRequest ParseRequest(const nlohmann::json& value)
{
    ScanRequest request;
    request.cidr = value.value("cidr", "");
    request.timeout_ms = std::clamp(value.value("timeoutMs", 800), 200, 5000);
    request.concurrency = std::clamp(value.value("concurrency", 32), 1, 128);
    request.resolve_names = value.value("resolveNames", true);
    if (const auto ports = value.find("ports"); ports != value.end() && ports->is_array())
    {
        for (const auto& item : *ports)
        {
            if (!item.is_number_integer()) continue;
            const auto port = item.get<int>();
            if (port >= 1 && port <= 65535 && std::find(request.ports.begin(), request.ports.end(), port) == request.ports.end())
                request.ports.push_back(port);
            if (request.ports.size() >= kMaximumPorts) break;
        }
    }
    return request;
}

void RunScan(std::stop_token stop_token,
             std::weak_ptr<drogon::WebSocketConnection> weak_connection,
             ScanRequest request)
{
    try
    {
        const auto addresses = ExpandCidr(request.cidr);
        if (const auto connection = weak_connection.lock())
            connection->send(nlohmann::json({{"type", "started"}, {"total", addresses.size()}}).dump());
        std::atomic<std::size_t> cursor{0};
        std::atomic<std::size_t> scanned{0};
        std::atomic<std::size_t> reachable{0};
        std::mutex send_mutex;
        std::vector<std::jthread> workers;
        const auto worker_count = std::min<std::size_t>(request.concurrency, addresses.size());
        workers.reserve(worker_count);
        for (std::size_t worker = 0; worker < worker_count; ++worker)
        {
            workers.emplace_back([&, stop_token](std::stop_token) {
                while (!stop_token.stop_requested())
                {
                    const auto index = cursor.fetch_add(1);
                    if (index >= addresses.size()) break;
                    const auto& ip = addresses[index];
                    const auto ping = Ping(ip, request.timeout_ms, stop_token);
                    std::vector<PortResult> open_ports;
                    std::optional<double> minimum_tcp_latency;
                    for (const auto port : request.ports)
                    {
                        if (stop_token.stop_requested()) break;
                        const auto probe = ProbePort(ip, port, std::min(request.timeout_ms, 1200), stop_token);
                        if (probe.latency_ms <= 0) continue;
                        open_ports.push_back(probe);
                        if (!minimum_tcp_latency || probe.latency_ms < *minimum_tcp_latency) minimum_tcp_latency = probe.latency_ms;
                    }
                    const auto online = ping.reachable || !open_ports.empty();
                    if (online) reachable.fetch_add(1);
                    nlohmann::json ports = nlohmann::json::array();
                    for (const auto& item : open_ports)
                        ports.push_back({{"port", item.port}, {"service", ServiceName(item.port)},
                                         {"latencyMs", std::round(item.latency_ms * 10) / 10}});
                    const auto latency = ping.latency_ms ? ping.latency_ms : minimum_tcp_latency;
                    nlohmann::json result{{"type", "result"}, {"ip", ip}, {"reachable", online},
                                          {"hostname", online && request.resolve_names ? ReverseName(ip) : ""},
                                          {"scope", AddressScope(ParseAddress(ip))}, {"openPorts", ports}};
                    if (latency) result["latencyMs"] = std::round(*latency * 10) / 10;
                    else result["latencyMs"] = nullptr;
                    const auto done = scanned.fetch_add(1) + 1;
                    result["scanned"] = done;
                    result["total"] = addresses.size();
                    if (const auto connection = weak_connection.lock())
                    {
                        std::lock_guard lock(send_mutex);
                        connection->send(result.dump());
                    }
                }
            });
        }
        workers.clear();
        if (const auto connection = weak_connection.lock())
            connection->send(nlohmann::json({{"type", stop_token.stop_requested() ? "cancelled" : "complete"},
                                             {"scanned", scanned.load()}, {"total", addresses.size()},
                                             {"reachable", reachable.load()}}).dump());
    }
    catch (const std::exception& error)
    {
        if (const auto connection = weak_connection.lock())
            connection->send(nlohmann::json({{"type", "error"}, {"message", error.what()}}).dump());
    }
}
} // namespace

void NetworkScanWebSocketController::handleNewConnection(const drogon::HttpRequestPtr& request,
                                                          const drogon::WebSocketConnectionPtr& connection)
{
    if (!request->isOnSecureConnection() && !request->peerAddr().isLoopbackIp())
    {
        connection->shutdown(drogon::CloseCode::kViolation, "Network scan requires HTTPS");
        return;
    }
    connection->setContext(std::make_shared<ScanState>());
    connection->send(nlohmann::json({{"type", "ready"}}).dump());
}

void NetworkScanWebSocketController::handleNewMessage(const drogon::WebSocketConnectionPtr& connection,
                                                       std::string&& message,
                                                       const drogon::WebSocketMessageType& type)
{
    const auto state = connection->getContext<ScanState>();
    if (!state || type != drogon::WebSocketMessageType::Text || message.size() > 64 * 1024) return;
    const auto payload = nlohmann::json::parse(message, nullptr, false);
    if (!payload.is_object())
    {
        connection->send(nlohmann::json({{"type", "error"}, {"message", "扫描请求格式无效。"}}).dump());
        return;
    }
    const auto action = payload.value("type", "");
    if (action == "cancel")
    {
        if (state->worker.joinable()) state->worker.request_stop();
        return;
    }
    if (action != "scan") return;
    if (state->worker.joinable()) { state->worker.request_stop(); state->worker.join(); }
    try
    {
        auto request = ParseRequest(payload);
        ExpandCidr(request.cidr);
        state->worker = std::jthread([weak = std::weak_ptr<drogon::WebSocketConnection>(connection), request = std::move(request)](std::stop_token token) mutable {
            RunScan(token, std::move(weak), std::move(request));
        });
    }
    catch (const std::exception& error)
    {
        connection->send(nlohmann::json({{"type", "error"}, {"message", error.what()}}).dump());
    }
}

void NetworkScanWebSocketController::handleConnectionClosed(const drogon::WebSocketConnectionPtr& connection)
{
    if (const auto state = connection->getContext<ScanState>(); state && state->worker.joinable())
        state->worker.request_stop();
    connection->clearContext();
    logI("Network scan WebSocket closed");
}
} // namespace spacestation::network
