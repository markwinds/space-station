#include "ssh/network_utils.hpp"

#include <cassert>
#include <chrono>
#include <stdexcept>
#include <stop_token>
#include <string>

using namespace std::chrono_literals;

namespace
{
void ExpectFailure(std::stop_token token,
                   std::chrono::steady_clock::time_point deadline,
                   const std::string& expected)
{
    try
    {
        spacestation::ssh::network::Resolve("127.0.0.1", 22, token, deadline,
                                            "cancelled", "timeout", "failure");
        assert(false && "resolution should have failed");
    }
    catch (const std::runtime_error& error)
    {
        assert(error.what() == expected);
    }
}
} // namespace

int main()
{
    const auto addresses = spacestation::ssh::network::Resolve(
        "127.0.0.1", 22, {}, std::chrono::steady_clock::now() + 2s,
        "cancelled", "timeout", "failure");
    assert(!addresses.empty());

    std::stop_source cancelled;
    cancelled.request_stop();
    ExpectFailure(cancelled.get_token(), std::chrono::steady_clock::now() + 2s, "cancelled");
    ExpectFailure({}, std::chrono::steady_clock::now() - 1ms, "timeout");
    return 0;
}
