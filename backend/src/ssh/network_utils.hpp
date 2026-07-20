#pragma once

#include <chrono>
#include <cstddef>
#include <stop_token>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#endif

namespace spacestation::ssh::network
{
struct ResolvedAddress
{
    sockaddr_storage address{};
    std::size_t length = 0;
    int family = 0;
    int socket_type = 0;
    int protocol = 0;
};

std::vector<ResolvedAddress> Resolve(const std::string& host,
                                     int port,
                                     std::stop_token stop_token,
                                     std::chrono::steady_clock::time_point deadline,
                                     const char* cancelled_message,
                                     const char* timeout_message,
                                     const char* failure_message);
} // namespace spacestation::ssh::network
