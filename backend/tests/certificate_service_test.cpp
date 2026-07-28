#include "cert/certificate_service.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
using nlohmann::json;

void Fail(std::string_view message)
{
    std::cerr << message << '\n';
    std::exit(1);
}

void ExpectOk(const json& result, std::string_view label)
{
    if (!result.value("ok", false))
    {
        Fail(std::string(label) + ": " + result.value("error", "unknown error"));
    }
}

void ExpectErrorContains(const json& result, std::string_view expected, std::string_view label)
{
    if (result.value("ok", false))
    {
        Fail(std::string(label) + ": expected failure");
    }
    const auto error = result.value("error", "");
    if (error.find(expected) == std::string::npos)
    {
        Fail(std::string(label) + ": unexpected error: " + error);
    }
}

json Generate(std::string_view common_name, bool is_ca)
{
    return spacestation::cert::GenerateCertificateBundle({
        {"subject", {{"commonName", common_name}, {"organization", "Space Station Test"}}},
        {"san", json::object()},
        {"isCa", is_ca},
        {"validDays", is_ca ? 3650 : 365},
        {"keyAlgorithm", "ec"},
        {"ecCurve", "prime256v1"},
        {"keyUsage", is_ca ? json{"critical", "keyCertSign", "cRLSign"}
                            : json{"critical", "digitalSignature"}},
        {"extendedKeyUsage", is_ca ? json::array() : json{"clientAuth"}},
    });
}

json Sign(const json& ca, const json& key_source, const json& leaf)
{
    return spacestation::cert::SignCertificateRequest({
        {"caCertificatePem", ca.at("certificatePem")},
        {"caPrivateKeyPem", key_source.at("privateKeyPem")},
        {"csrPem", leaf.at("csrPem")},
        {"san", json::object()},
        {"validDays", 365},
        {"keyUsage", {"critical", "digitalSignature"}},
        {"extendedKeyUsage", {"clientAuth"}},
    });
}
} // namespace

int main()
{
    const auto root = Generate("Space Station Root CA", true);
    const auto other_root = Generate("Other Root CA", true);
    const auto leaf = Generate("test-client", false);
    const auto invalid_usage_ca = spacestation::cert::GenerateCertificateBundle({
        {"subject", {{"commonName", "Invalid Usage CA"}}},
        {"san", json::object()},
        {"isCa", true},
        {"validDays", 3650},
        {"keyAlgorithm", "ec"},
        {"ecCurve", "prime256v1"},
        {"keyUsage", {"critical", "digitalSignature"}},
        {"extendedKeyUsage", json::array()},
    });
    ExpectOk(root, "generate root");
    ExpectOk(other_root, "generate other root");
    ExpectOk(leaf, "generate leaf");
    ExpectOk(invalid_usage_ca, "generate invalid-usage CA");

    const auto mismatched_key = Sign(root, other_root, leaf);
    ExpectErrorContains(mismatched_key, "CA 证书与 CA 私钥不匹配", "mismatched CA key");

    const auto non_ca_signer = Sign(leaf, leaf, leaf);
    ExpectErrorContains(non_ca_signer, "不是有效的 CA 证书", "non-CA signer");

    const auto invalid_usage_signer = Sign(invalid_usage_ca, invalid_usage_ca, leaf);
    ExpectErrorContains(invalid_usage_signer, "缺少 keyCertSign", "CA without keyCertSign");

    const auto signed_leaf = Sign(root, root, leaf);
    ExpectOk(signed_leaf, "sign leaf");
    const auto parsed_leaf = spacestation::cert::ParseCertificate({{"certificatePem", signed_leaf.at("certificatePem")}});
    ExpectOk(parsed_leaf, "parse signed leaf");
    if (parsed_leaf.value("selfSigned", true))
    {
        Fail("signed leaf must not be reported as self-signed");
    }

    const auto valid_p12 = spacestation::cert::CreatePkcs12({
        {"certificatePem", signed_leaf.at("certificatePem")},
        {"privateKeyPem", leaf.at("privateKeyPem")},
        {"caCertificatePem", root.at("certificatePem")},
        {"password", "test"},
        {"friendlyName", "test-client"},
    });
    ExpectOk(valid_p12, "valid P12 chain");

    const auto invalid_p12 = spacestation::cert::CreatePkcs12({
        {"certificatePem", signed_leaf.at("certificatePem")},
        {"privateKeyPem", leaf.at("privateKeyPem")},
        {"caCertificatePem", other_root.at("certificatePem")},
        {"password", "test"},
        {"friendlyName", "test-client"},
    });
    ExpectErrorContains(invalid_p12, "无法通过提供的完整 CA 链校验", "invalid P12 chain");
    return 0;
}
