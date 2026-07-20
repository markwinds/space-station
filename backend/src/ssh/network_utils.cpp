#include "ssh/network_utils.hpp"

#include <ares.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace spacestation::ssh::network
{
namespace
{
struct ResolveState
{
    bool completed = false;
    int status = ARES_ECONNREFUSED;
    std::vector<ResolvedAddress> addresses;
    std::unordered_map<ares_socket_t, unsigned int> sockets;
};

void SocketStateCallback(void* argument, ares_socket_t socket_value, int readable, int writable)
{
    auto& state = *static_cast<ResolveState*>(argument);
    unsigned int events = ARES_FD_EVENT_NONE;
    if (readable) events |= ARES_FD_EVENT_READ;
    if (writable) events |= ARES_FD_EVENT_WRITE;
    if (events == ARES_FD_EVENT_NONE) state.sockets.erase(socket_value);
    else state.sockets[socket_value] = events;
}

void ResolveCallback(void* argument, int status, int, ares_addrinfo* result)
{
    auto& state = *static_cast<ResolveState*>(argument);
    state.status = status;
    if (status == ARES_SUCCESS && result)
    {
        for (auto* node = result->nodes; node; node = node->ai_next)
        {
            if (!node->ai_addr || node->ai_addrlen <= 0 ||
                static_cast<std::size_t>(node->ai_addrlen) > sizeof(sockaddr_storage))
                continue;
            ResolvedAddress address;
            std::memcpy(&address.address, node->ai_addr, static_cast<std::size_t>(node->ai_addrlen));
            address.length = static_cast<std::size_t>(node->ai_addrlen);
            address.family = node->ai_family;
            address.socket_type = node->ai_socktype;
            address.protocol = node->ai_protocol;
            state.addresses.push_back(address);
        }
    }
    if (result) ares_freeaddrinfo(result);
    state.completed = true;
}

struct ChannelDeleter
{
    void operator()(ares_channel_t* channel) const
    {
        if (channel) ares_destroy(channel);
    }
};
} // namespace

std::vector<ResolvedAddress> Resolve(const std::string& host,
                                     int port,
                                     std::stop_token stop_token,
                                     std::chrono::steady_clock::time_point deadline,
                                     const char* cancelled_message,
                                     const char* timeout_message,
                                     const char* failure_message)
{
    if (stop_token.stop_requested()) throw std::runtime_error(cancelled_message);
    if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error(timeout_message);
    static const int initialized = ares_library_init(ARES_LIB_INIT_ALL);
    if (initialized != ARES_SUCCESS) throw std::runtime_error("DNS 解析器初始化失败。");

    ares_channel_t* raw_channel = nullptr;
    ares_options options{};
    options.timeout = 1000;
    options.tries = 2;
    ResolveState state;
    options.sock_state_cb = SocketStateCallback;
    options.sock_state_cb_data = &state;
    if (ares_init_options(&raw_channel, &options,
                          ARES_OPT_TIMEOUTMS | ARES_OPT_TRIES | ARES_OPT_SOCK_STATE_CB) != ARES_SUCCESS)
        throw std::runtime_error("DNS 解析器初始化失败。");
    std::unique_ptr<ares_channel_t, ChannelDeleter> channel(raw_channel);

    ares_addrinfo_hints hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    const auto service = std::to_string(port);
    ares_getaddrinfo(channel.get(), host.c_str(), service.c_str(), &hints, ResolveCallback, &state);

    while (!state.completed)
    {
        if (stop_token.stop_requested())
        {
            ares_cancel(channel.get());
            throw std::runtime_error(cancelled_message);
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            ares_cancel(channel.get());
            throw std::runtime_error(timeout_message);
        }
        timeval maximum_wait{0, 100000};
        timeval wait{};
        const auto* selected_wait = ares_timeout(channel.get(), &maximum_wait, &wait);
        std::vector<ares_fd_events_t> ready;
        if (state.sockets.empty())
        {
            const auto duration = std::chrono::microseconds(selected_wait->tv_sec * 1000000LL + selected_wait->tv_usec);
            std::this_thread::sleep_for(duration);
        }
        else
        {
            fd_set reads;
            fd_set writes;
            FD_ZERO(&reads);
            FD_ZERO(&writes);
            int maximum = 0;
            for (const auto& [socket_value, events] : state.sockets)
            {
                if ((events & ARES_FD_EVENT_READ) != 0) FD_SET(socket_value, &reads);
                if ((events & ARES_FD_EVENT_WRITE) != 0) FD_SET(socket_value, &writes);
                maximum = std::max(maximum, static_cast<int>(socket_value));
            }
            select(maximum + 1, &reads, &writes, nullptr, const_cast<timeval*>(selected_wait));
            for (const auto& [socket_value, events] : state.sockets)
            {
                unsigned int occurred = ARES_FD_EVENT_NONE;
                if ((events & ARES_FD_EVENT_READ) != 0 && FD_ISSET(socket_value, &reads)) occurred |= ARES_FD_EVENT_READ;
                if ((events & ARES_FD_EVENT_WRITE) != 0 && FD_ISSET(socket_value, &writes)) occurred |= ARES_FD_EVENT_WRITE;
                if (occurred != ARES_FD_EVENT_NONE) ready.push_back({socket_value, occurred});
            }
        }
        ares_process_fds(channel.get(), ready.data(), ready.size(), ARES_PROCESS_FLAG_NONE);
    }

    if (stop_token.stop_requested()) throw std::runtime_error(cancelled_message);
    if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error(timeout_message);
    if (state.status != ARES_SUCCESS || state.addresses.empty())
        throw std::runtime_error(std::string(failure_message) + "（" + ares_strerror(state.status) + "）");
    return state.addresses;
}
} // namespace spacestation::ssh::network
