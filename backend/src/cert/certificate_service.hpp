#pragma once

#include <nlohmann/json.hpp>

namespace spacestation::cert
{
nlohmann::json GenerateCertificateBundle(const nlohmann::json& request);
nlohmann::json SignCertificateRequest(const nlohmann::json& request);
nlohmann::json ParseCertificate(const nlohmann::json& request);
nlohmann::json ParseCsr(const nlohmann::json& request);
} // namespace spacestation::cert
