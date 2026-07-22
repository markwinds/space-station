#include "serial/serial_service.hpp"

#include "logging/logger.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace spacestation::serial
{
namespace
{
using namespace std::chrono_literals;
constexpr std::size_t kMaximumBacklogBytes = 512 * 1024;
constexpr std::size_t kMaximumQueuedWriteBytes = 1024 * 1024;

std::string NextId()
{
    static std::atomic<std::uint64_t> counter{0};
    return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
           std::to_string(++counter);
}

class NativeSerialPort
{
  public:
    NativeSerialPort() = default;
    NativeSerialPort(const NativeSerialPort&) = delete;
    NativeSerialPort& operator=(const NativeSerialPort&) = delete;
    ~NativeSerialPort() { Close(); }

    static nlohmann::json List()
    {
        auto ports = nlohmann::json::array();
#ifdef _WIN32
        char target[4096]{};
        for (int index = 1; index <= 256; ++index)
        {
            const auto name = "COM" + std::to_string(index);
            if (QueryDosDeviceA(name.c_str(), target, sizeof(target)) != 0)
                ports.push_back({{"id", name}, {"name", name}, {"path", name}});
        }
#else
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator("/dev", error))
        {
            const auto name = entry.path().filename().string();
#ifdef __APPLE__
            const auto supported = name.starts_with("cu.");
#else
            const auto supported = name.starts_with("ttyS") || name.starts_with("ttyUSB") ||
                                   name.starts_with("ttyACM") || name.starts_with("rfcomm");
#endif
            if (!supported) continue;
            const auto path = entry.path().string();
            ports.push_back({{"id", path}, {"name", name}, {"path", path}});
        }
#endif
        std::sort(ports.begin(), ports.end(), [](const auto& left, const auto& right) {
            return left.value("name", "") < right.value("name", "");
        });
        return ports;
    }

    void Open(const std::string& path, const SerialOptions& options)
    {
#ifdef _WIN32
        const auto device = path.starts_with("\\\\.\\") ? path : "\\\\.\\" + path;
        handle_ = CreateFileA(device.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
        {
            const auto code = GetLastError();
            if (code == ERROR_ACCESS_DENIED || code == ERROR_SHARING_VIOLATION)
                throw std::runtime_error("串口已被其他程序占用或当前用户无权访问。");
            throw std::runtime_error("无法打开串口，Windows 错误码 " + std::to_string(code) + "。");
        }
        DCB state{};
        state.DCBlength = sizeof(state);
        if (!GetCommState(handle_, &state)) throw std::runtime_error("无法读取串口参数。");
        state.BaudRate = static_cast<DWORD>(options.baud_rate);
        state.ByteSize = static_cast<BYTE>(options.data_bits);
        state.StopBits = options.stop_bits == 2 ? TWOSTOPBITS : ONESTOPBIT;
        state.Parity = options.parity == "even" ? EVENPARITY : options.parity == "odd" ? ODDPARITY : NOPARITY;
        state.fParity = options.parity != "none";
        state.fOutxCtsFlow = options.flow_control == "hardware";
        state.fRtsControl = options.flow_control == "hardware" ? RTS_CONTROL_HANDSHAKE : RTS_CONTROL_ENABLE;
        state.fInX = state.fOutX = options.flow_control == "software";
        if (!SetCommState(handle_, &state)) throw std::runtime_error("串口参数不受设备支持。");
        COMMTIMEOUTS timeouts{};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutConstant = 20;
        timeouts.WriteTotalTimeoutConstant = 1000;
        SetCommTimeouts(handle_, &timeouts);
#else
        fd_ = open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ < 0)
        {
            if (errno == EACCES) throw std::runtime_error("没有访问该串口的权限。");
            if (errno == EBUSY) throw std::runtime_error("串口已被其他程序占用。");
            throw std::runtime_error("无法打开串口：" + std::string(std::strerror(errno)));
        }
        if (flock(fd_, LOCK_EX | LOCK_NB) != 0)
            throw std::runtime_error("串口已被其他程序占用。");
#ifdef TIOCEXCL
        if (ioctl(fd_, TIOCEXCL) != 0)
            throw std::runtime_error("无法将串口设置为独占模式。");
#endif
        termios state{};
        if (tcgetattr(fd_, &state) != 0) throw std::runtime_error("无法读取串口参数。");
        cfmakeraw(&state);
        state.c_cflag |= CLOCAL | CREAD;
        state.c_cflag &= ~CSIZE;
        state.c_cflag |= options.data_bits == 5 ? CS5 : options.data_bits == 6 ? CS6 : options.data_bits == 7 ? CS7 : CS8;
        if (options.stop_bits == 2) state.c_cflag |= CSTOPB; else state.c_cflag &= ~CSTOPB;
        if (options.parity == "none") state.c_cflag &= ~(PARENB | PARODD);
        else
        {
            state.c_cflag |= PARENB;
            if (options.parity == "odd") state.c_cflag |= PARODD; else state.c_cflag &= ~PARODD;
        }
#ifdef CRTSCTS
        if (options.flow_control == "hardware") state.c_cflag |= CRTSCTS; else state.c_cflag &= ~CRTSCTS;
#endif
        if (options.flow_control == "software") state.c_iflag |= IXON | IXOFF;
        else state.c_iflag &= ~(IXON | IXOFF | IXANY);
        const auto speed = BaudConstant(options.baud_rate);
        cfsetispeed(&state, speed);
        cfsetospeed(&state, speed);
        if (tcsetattr(fd_, TCSANOW, &state) != 0) throw std::runtime_error("串口参数不受设备支持。");
#endif
    }

    std::ptrdiff_t Read(char* buffer, std::size_t size)
    {
#ifdef _WIN32
        DWORD read_count = 0;
        if (!ReadFile(handle_, buffer, static_cast<DWORD>(size), &read_count, nullptr)) return -1;
        return static_cast<std::ptrdiff_t>(read_count);
#else
        const auto result = read(fd_, buffer, size);
        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        return result;
#endif
    }

    std::ptrdiff_t Write(const char* data, std::size_t size)
    {
#ifdef _WIN32
        DWORD written = 0;
        if (!WriteFile(handle_, data, static_cast<DWORD>(size), &written, nullptr)) return -1;
        return static_cast<std::ptrdiff_t>(written);
#else
        const auto result = write(fd_, data, size);
        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        return result;
#endif
    }

    void Close()
    {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(std::exchange(handle_, INVALID_HANDLE_VALUE));
#else
        if (fd_ >= 0) close(std::exchange(fd_, -1));
#endif
    }

  private:
#ifndef _WIN32
    static speed_t BaudConstant(int value)
    {
        switch (value)
        {
        case 50: return B50; case 75: return B75; case 110: return B110; case 300: return B300;
        case 600: return B600; case 1200: return B1200; case 2400: return B2400; case 4800: return B4800;
        case 9600: return B9600; case 19200: return B19200; case 38400: return B38400;
        case 57600: return B57600; case 115200: return B115200;
#ifdef B230400
        case 230400: return B230400;
#endif
        default: throw std::runtime_error("当前系统不支持该波特率。");
        }
    }
    int fd_ = -1;
#else
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#endif
};
} // namespace

nlohmann::json SerialOptions::ToJson() const
{
    return {{"baudRate", baud_rate}, {"dataBits", data_bits}, {"stopBits", stop_bits},
            {"parity", parity}, {"flowControl", flow_control}};
}

SerialOptions SerialOptions::FromJson(const nlohmann::json& value)
{
    SerialOptions options;
    options.baud_rate = std::clamp(value.value("baudRate", 115200), 50, 4000000);
    options.data_bits = std::clamp(value.value("dataBits", 8), 5, 8);
    options.stop_bits = value.value("stopBits", 1) == 2 ? 2 : 1;
    options.parity = value.value("parity", "none");
    if (options.parity != "none" && options.parity != "even" && options.parity != "odd") options.parity = "none";
    options.flow_control = value.value("flowControl", "none");
    if (options.flow_control != "none" && options.flow_control != "hardware" && options.flow_control != "software")
        options.flow_control = "none";
    return options;
}

class SerialService::Session : public std::enable_shared_from_this<Session>
{
  public:
    Session(std::string port, SerialOptions options) : port_(std::move(port)), options_(std::move(options))
    {
        device_.Open(port_, options_);
        running_ = true;
        worker_ = std::jthread([this](std::stop_token token) { Run(token); });
    }

    ~Session()
    {
        worker_.request_stop();
        condition_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    const SerialOptions& Options() const { return options_; }
    bool Running() const { return running_; }

    void Add(const std::string& id, const drogon::WebSocketConnectionPtr& connection)
    {
        std::string backlog;
        std::size_t viewers = 0;
        {
            std::lock_guard lock(mutex_);
            subscribers_[id] = connection;
            backlog = backlog_;
            viewers = subscribers_.size();
        }
        connection->send(nlohmann::json({{"type", "opened"}, {"subscriptionId", id}, {"port", port_},
                                         {"options", options_.ToJson()}, {"viewers", viewers}}).dump());
        if (!backlog.empty()) connection->send(backlog, drogon::WebSocketMessageType::Binary);
        BroadcastState();
    }

    std::size_t Remove(const std::string& id)
    {
        std::size_t remaining = 0;
        {
            std::lock_guard lock(mutex_);
            subscribers_.erase(id);
            remaining = subscribers_.size();
        }
        if (remaining) BroadcastState();
        return remaining;
    }

    void QueueWrite(std::string data)
    {
        std::lock_guard lock(mutex_);
        if (!running_) throw std::runtime_error("串口连接已经断开。");
        if (queued_write_bytes_ + data.size() > kMaximumQueuedWriteBytes)
            throw std::runtime_error("串口发送队列已满，请稍后重试。");
        queued_write_bytes_ += data.size();
        writes_.push_back(std::move(data));
        condition_.notify_all();
    }

  private:
    void Run(std::stop_token token)
    {
        std::array<char, 16384> buffer{};
        while (!token.stop_requested())
        {
            const auto count = device_.Read(buffer.data(), buffer.size());
            if (count < 0)
            {
                Fail("串口读取失败，设备可能已经断开。");
                return;
            }
            if (count > 0)
            {
                std::string data(buffer.data(), static_cast<std::size_t>(count));
                std::vector<drogon::WebSocketConnectionPtr> connections;
                {
                    std::lock_guard lock(mutex_);
                    backlog_.append(data);
                    if (backlog_.size() > kMaximumBacklogBytes)
                        backlog_.erase(0, backlog_.size() - kMaximumBacklogBytes);
                    CollectConnections(connections);
                }
                for (const auto& connection : connections)
                    connection->send(data, drogon::WebSocketMessageType::Binary);
            }

            std::string write_data;
            {
                std::lock_guard lock(mutex_);
                if (!writes_.empty())
                {
                    write_data = std::move(writes_.front());
                    queued_write_bytes_ -= write_data.size();
                    writes_.pop_front();
                }
            }
            std::size_t offset = 0;
            while (offset < write_data.size() && !token.stop_requested())
            {
                const auto written = device_.Write(write_data.data() + offset, write_data.size() - offset);
                if (written < 0)
                {
                    Fail("串口写入失败，设备可能已经断开。");
                    return;
                }
                if (written == 0) { std::this_thread::sleep_for(5ms); continue; }
                offset += static_cast<std::size_t>(written);
            }
            if (count == 0 && write_data.empty())
            {
                std::unique_lock lock(mutex_);
                condition_.wait_for(lock, 10ms, [&] { return !writes_.empty() || token.stop_requested(); });
            }
        }
        running_ = false;
    }

    void CollectConnections(std::vector<drogon::WebSocketConnectionPtr>& result)
    {
        for (auto iterator = subscribers_.begin(); iterator != subscribers_.end();)
        {
            if (const auto connection = iterator->second.lock(); connection && connection->connected())
            {
                result.push_back(connection);
                ++iterator;
            }
            else iterator = subscribers_.erase(iterator);
        }
    }

    void BroadcastState()
    {
        std::vector<drogon::WebSocketConnectionPtr> connections;
        {
            std::lock_guard lock(mutex_);
            CollectConnections(connections);
        }
        const auto message = nlohmann::json({{"type", "state"}, {"status", "open"},
                                             {"viewers", connections.size()}, {"port", port_}}).dump();
        for (const auto& connection : connections) connection->send(message);
    }

    void Fail(const std::string& message)
    {
        running_ = false;
        std::vector<drogon::WebSocketConnectionPtr> connections;
        {
            std::lock_guard lock(mutex_);
            CollectConnections(connections);
        }
        const auto event = nlohmann::json({{"type", "error"}, {"message", message}}).dump();
        for (const auto& connection : connections) connection->send(event);
    }

    std::string port_;
    SerialOptions options_;
    NativeSerialPort device_;
    std::jthread worker_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::unordered_map<std::string, std::weak_ptr<drogon::WebSocketConnection>> subscribers_;
    std::deque<std::string> writes_;
    std::size_t queued_write_bytes_ = 0;
    std::string backlog_;
};

SerialService::SerialService() = default;
SerialService::~SerialService() = default;

nlohmann::json SerialService::ListPorts() const
{
    return {{"ports", NativeSerialPort::List()}};
}

std::string SerialService::Attach(const std::string& port,
                                  const SerialOptions& options,
                                  const drogon::WebSocketConnectionPtr& connection)
{
    if (port.empty() || port.size() > 512) throw std::runtime_error("串口名称无效。");
    const auto available_ports = NativeSerialPort::List();
    const auto known_port = std::any_of(available_ports.begin(), available_ports.end(), [&](const auto& item) {
        return item.value("id", "") == port;
    });
    if (!known_port) throw std::runtime_error("串口不存在或已经移除，请刷新设备列表后重试。");
    std::shared_ptr<Session> session;
    const auto id = NextId();
    {
        std::lock_guard lock(mutex_);
        const auto found = sessions_.find(port);
        if (found != sessions_.end() && found->second->Running())
        {
            if (!(found->second->Options() == options))
                throw std::runtime_error("该串口已由本程序使用其他参数打开，请使用当前会话参数或先关闭原会话。");
            session = found->second;
        }
        else
        {
            if (found != sessions_.end()) sessions_.erase(found);
            session = std::make_shared<Session>(port, options);
            sessions_[port] = session;
            const auto log_message = "Serial port opened: " + port;
            logI(log_message.c_str());
        }
        subscriptions_[id] = {session, port};
        // Add the subscriber while holding the service lock. This prevents the
        // final detach of another subscriber from releasing the physical port
        // between registration and Session::Add().
        session->Add(id, connection);
    }
    return id;
}

void SerialService::Detach(const std::string& subscription_id)
{
    std::shared_ptr<Session> released;
    std::string port;
    {
        std::lock_guard lock(mutex_);
        const auto found = subscriptions_.find(subscription_id);
        if (found == subscriptions_.end()) return;
        const auto session = found->second.session.lock();
        port = found->second.port;
        subscriptions_.erase(found);
        if (!session || session->Remove(subscription_id) != 0) return;

        // Keep removal and the last-subscriber decision atomic with Attach().
        // Otherwise a new subscriber could join after Remove() returned zero
        // and have its newly shared physical session released here.
        const auto session_found = sessions_.find(port);
        if (session_found != sessions_.end() && session_found->second == session)
        {
            released = std::move(session_found->second);
            sessions_.erase(session_found);
        }
    }
    if (released)
    {
        released.reset();
        const auto log_message = "Serial port closed after last subscriber: " + port;
        logI(log_message.c_str());
    }
}

void SerialService::Write(const std::string& subscription_id, std::string data)
{
    if (data.empty()) return;
    if (data.size() > 1024 * 1024) throw std::runtime_error("单次串口发送不能超过 1 MiB。");
    std::shared_ptr<Session> session;
    {
        std::lock_guard lock(mutex_);
        const auto found = subscriptions_.find(subscription_id);
        if (found == subscriptions_.end()) throw std::runtime_error("串口会话尚未打开。");
        session = found->second.session.lock();
    }
    if (!session) throw std::runtime_error("串口会话已经关闭。");
    session->QueueWrite(std::move(data));
}
} // namespace spacestation::serial
