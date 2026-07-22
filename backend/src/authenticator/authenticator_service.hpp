#pragma once

#include "config/config_store.hpp"

#include <cstdint>
#include <string>

namespace spacestation::authenticator
{
std::string GenerateTotp(const std::string& base32_secret,
                         std::int64_t unix_seconds,
                         int period = 30,
                         int digits = 6,
                         const std::string& algorithm = "SHA1");

class AuthenticatorService
{
  public:
    explicit AuthenticatorService(ConfigStore& store);

    nlohmann::json ListCodes();
    nlohmann::json SaveEntry(const nlohmann::json& input);
    void DeleteEntry(const std::string& id);

  private:
    ConfigStore& store_;
};
} // namespace spacestation::authenticator
