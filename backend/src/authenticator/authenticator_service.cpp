#include "authenticator/authenticator_service.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace spacestation::authenticator
{
namespace
{
std::vector<unsigned char> DecodeBase32(const std::string& input)
{
    std::vector<unsigned char> output;
    unsigned int accumulator = 0;
    int bits = 0;
    for (const auto raw : input)
    {
        if (raw == ' ' || raw == '-' || raw == '=') continue;
        const auto character = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
        int value = -1;
        if (character >= 'A' && character <= 'Z') value = character - 'A';
        else if (character >= '2' && character <= '7') value = character - '2' + 26;
        if (value < 0) throw std::invalid_argument("密钥不是有效的 Base32 字符串。");
        accumulator = (accumulator << 5U) | static_cast<unsigned int>(value);
        bits += 5;
        if (bits >= 8)
        {
            bits -= 8;
            output.push_back(static_cast<unsigned char>((accumulator >> bits) & 0xffU));
        }
    }
    if (output.size() < 10) throw std::invalid_argument("认证器密钥过短。");
    return output;
}

const EVP_MD* DigestForAlgorithm(const std::string& algorithm)
{
    if (algorithm == "SHA1") return EVP_sha1();
    if (algorithm == "SHA256") return EVP_sha256();
    if (algorithm == "SHA512") return EVP_sha512();
    throw std::invalid_argument("仅支持 SHA1、SHA256 或 SHA512。");
}

std::string NormalizeSecret(const std::string& input)
{
    std::string normalized;
    normalized.reserve(input.size());
    for (const auto raw : input)
    {
        if (raw == ' ' || raw == '-' || raw == '=') continue;
        normalized.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(raw))));
    }
    DecodeBase32(normalized);
    return normalized;
}

nlohmann::json PublicEntry(const nlohmann::json& entry, std::int64_t now)
{
    const auto period = std::clamp(entry.value("period", 30), 15, 120);
    const auto digits = entry.value("digits", 6) == 8 ? 8 : 6;
    const auto algorithm = entry.value("algorithm", "SHA1");
    return {
        {"id", entry.value("id", "")},
        {"name", entry.value("name", "")},
        {"issuer", entry.value("issuer", "")},
        {"account", entry.value("account", "")},
        {"algorithm", algorithm},
        {"digits", digits},
        {"period", period},
        {"code", GenerateTotp(entry.value("secret", ""), now, period, digits, algorithm)},
        {"remaining", period - static_cast<int>(now % period)},
    };
}
} // namespace

std::string GenerateTotp(const std::string& base32_secret,
                         std::int64_t unix_seconds,
                         int period,
                         int digits,
                         const std::string& algorithm)
{
    if (period < 1 || (digits != 6 && digits != 8)) throw std::invalid_argument("TOTP 参数无效。");
    auto secret = DecodeBase32(base32_secret);
    const auto counter = static_cast<std::uint64_t>(unix_seconds / period);
    std::array<unsigned char, 8> message{};
    for (std::size_t index = 0; index < message.size(); ++index)
    {
        message[message.size() - index - 1] = static_cast<unsigned char>((counter >> (index * 8U)) & 0xffU);
    }
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0;
    if (!HMAC(DigestForAlgorithm(algorithm), secret.data(), static_cast<int>(secret.size()),
              message.data(), message.size(), digest.data(), &digest_size))
    {
        OPENSSL_cleanse(secret.data(), secret.size());
        throw std::runtime_error("TOTP 计算失败。");
    }
    OPENSSL_cleanse(secret.data(), secret.size());
    const auto offset = digest[digest_size - 1] & 0x0fU;
    if (offset + 3 >= digest_size) throw std::runtime_error("TOTP 摘要长度无效。");
    const auto binary = (static_cast<std::uint32_t>(digest[offset]) & 0x7fU) << 24U |
                        static_cast<std::uint32_t>(digest[offset + 1]) << 16U |
                        static_cast<std::uint32_t>(digest[offset + 2]) << 8U |
                        static_cast<std::uint32_t>(digest[offset + 3]);
    std::uint32_t divisor = 1;
    for (int index = 0; index < digits; ++index) divisor *= 10U;
    std::ostringstream code;
    code << std::setw(digits) << std::setfill('0') << binary % divisor;
    OPENSSL_cleanse(digest.data(), digest.size());
    return code.str();
}

AuthenticatorService::AuthenticatorService(ConfigStore& store) : store_(store)
{
}

nlohmann::json AuthenticatorService::ListCodes()
{
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto public_entries = nlohmann::json::array();
    for (const auto& entry : store_.LoadAuthenticatorEntries())
    {
        if (entry.is_object()) public_entries.push_back(PublicEntry(entry, now));
    }
    std::sort(public_entries.begin(), public_entries.end(), [](const auto& left, const auto& right) {
        const auto left_label = left.value("issuer", "") + left.value("name", "");
        const auto right_label = right.value("issuer", "") + right.value("name", "");
        return left_label < right_label;
    });
    return {{"entries", public_entries}, {"serverTime", now}};
}

nlohmann::json AuthenticatorService::SaveEntry(const nlohmann::json& input)
{
    const auto id = input.value("id", "");
    const auto name = input.value("name", "");
    if (id.empty() || name.empty()) throw std::invalid_argument("名称不能为空。");
    const auto digits = input.value("digits", 6);
    const auto period = input.value("period", 30);
    auto algorithm = input.value("algorithm", "SHA1");
    std::transform(algorithm.begin(), algorithm.end(), algorithm.begin(), [](unsigned char value) {
        return static_cast<char>(std::toupper(value));
    });
    if (digits != 6 && digits != 8) throw std::invalid_argument("验证码位数只能是 6 或 8。");
    if (period < 15 || period > 120) throw std::invalid_argument("刷新周期必须在 15 到 120 秒之间。");
    DigestForAlgorithm(algorithm);
    auto secret_input = input.value("secret", "");
    if (secret_input.empty())
    {
        for (const auto& existing : store_.LoadAuthenticatorEntries())
        {
            if (existing.value("id", "") == id)
            {
                secret_input = existing.value("secret", "");
                break;
            }
        }
    }
    if (secret_input.empty()) throw std::invalid_argument("密钥不能为空。");
    const auto secret = NormalizeSecret(secret_input);
    store_.SaveAuthenticatorEntry({
        {"id", id},
        {"name", name},
        {"issuer", input.value("issuer", "")},
        {"account", input.value("account", "")},
        {"secret", secret},
        {"algorithm", algorithm},
        {"digits", digits},
        {"period", period},
    });
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto saved = input;
    saved["secret"] = secret;
    saved["algorithm"] = algorithm;
    saved["digits"] = digits;
    saved["period"] = period;
    return PublicEntry(saved, now);
}

void AuthenticatorService::DeleteEntry(const std::string& id)
{
    if (id.empty()) throw std::invalid_argument("认证器条目 ID 不能为空。");
    store_.DeleteAuthenticatorEntry(id);
}
} // namespace spacestation::authenticator
