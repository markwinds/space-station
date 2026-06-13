#include "cert/certificate_service.hpp"

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace spacestation::cert
{
namespace
{
template <typename T, auto Deleter>
using OpenSslPtr = std::unique_ptr<T, decltype(Deleter)>;

using BioPtr = OpenSslPtr<BIO, BIO_free>;
using BnPtr = OpenSslPtr<BIGNUM, BN_free>;
using EvpPkeyPtr = OpenSslPtr<EVP_PKEY, EVP_PKEY_free>;
using EvpPkeyCtxPtr = OpenSslPtr<EVP_PKEY_CTX, EVP_PKEY_CTX_free>;
using Pkcs12Ptr = OpenSslPtr<PKCS12, PKCS12_free>;
using X509Ptr = OpenSslPtr<X509, X509_free>;
using X509ReqPtr = OpenSslPtr<X509_REQ, X509_REQ_free>;
using Asn1IntegerPtr = OpenSslPtr<ASN1_INTEGER, ASN1_INTEGER_free>;
using Asn1TimePtr = OpenSslPtr<ASN1_TIME, ASN1_TIME_free>;

void OpenSslFree(void* value)
{
    OPENSSL_free(value);
}

void X509ExtensionStackFree(STACK_OF(X509_EXTENSION)* extensions)
{
    sk_X509_EXTENSION_pop_free(extensions, X509_EXTENSION_free);
}

void X509StackFree(STACK_OF(X509)* certificates)
{
    sk_X509_pop_free(certificates, X509_free);
}

struct SubjectAltNames
{
    std::vector<std::string> dns;
    std::vector<std::string> ips;
    std::vector<std::string> emails;
    std::vector<std::string> uris;
};

std::string StringValue(const nlohmann::json& json, std::string_view key, std::string_view fallback = "")
{
    const auto it = json.find(std::string(key));
    if (it == json.end() || !it->is_string())
    {
        return std::string(fallback);
    }
    return it->get<std::string>();
}

int IntValue(const nlohmann::json& json, std::string_view key, int fallback)
{
    const auto it = json.find(std::string(key));
    if (it == json.end() || !it->is_number_integer())
    {
        return fallback;
    }
    return it->get<int>();
}

bool BoolValue(const nlohmann::json& json, std::string_view key, bool fallback)
{
    const auto it = json.find(std::string(key));
    if (it == json.end() || !it->is_boolean())
    {
        return fallback;
    }
    return it->get<bool>();
}

std::vector<std::string> StringArrayValue(const nlohmann::json& json, std::string_view key)
{
    std::vector<std::string> values;
    const auto it = json.find(std::string(key));
    if (it == json.end() || !it->is_array())
    {
        return values;
    }

    for (const auto& value : *it)
    {
        if (value.is_string() && !value.get<std::string>().empty())
        {
            values.push_back(value.get<std::string>());
        }
    }
    return values;
}

std::string Join(const std::vector<std::string>& values, std::string_view separator)
{
    std::ostringstream output;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (i > 0)
        {
            output << separator;
        }
        output << values[i];
    }
    return output.str();
}

std::string Trim(std::string value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    if (first >= last)
    {
        return {};
    }
    return std::string(first, last);
}

std::vector<std::string> SplitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line))
    {
        const auto trimmed = Trim(line);
        if (!trimmed.empty())
        {
            lines.push_back(trimmed);
        }
    }
    return lines;
}

std::string BioToString(BIO* bio)
{
    BUF_MEM* buffer = nullptr;
    BIO_get_mem_ptr(bio, &buffer);
    if (!buffer)
    {
        throw std::runtime_error("无法读取 OpenSSL 内存缓冲区。");
    }
    return std::string(buffer->data, buffer->length);
}

std::string Base64Encode(const std::string& bytes)
{
    if (bytes.empty())
    {
        return {};
    }

    std::string output(4 * ((bytes.size() + 2) / 3), '\0');
    const int length = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(output.data()),
                                       reinterpret_cast<const unsigned char*>(bytes.data()),
                                       static_cast<int>(bytes.size()));
    if (length < 0)
    {
        throw std::runtime_error("P12 Base64 编码失败。");
    }
    output.resize(static_cast<std::size_t>(length));
    return output;
}

std::string Base64Decode(const std::string& base64)
{
    if (base64.empty())
    {
        return {};
    }

    std::string compact;
    compact.reserve(base64.size());
    for (const char c : base64)
    {
        if (!std::isspace(static_cast<unsigned char>(c)))
        {
            compact.push_back(c);
        }
    }

    std::string output(3 * (compact.size() / 4 + 1), '\0');
    const int length = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(output.data()),
                                       reinterpret_cast<const unsigned char*>(compact.data()),
                                       static_cast<int>(compact.size()));
    if (length < 0)
    {
        throw std::runtime_error("P12 Base64 解码失败。");
    }

    std::size_t padding = 0;
    if (!compact.empty() && compact.back() == '=')
    {
        ++padding;
        if (compact.size() > 1 && compact[compact.size() - 2] == '=')
        {
            ++padding;
        }
    }
    output.resize(static_cast<std::size_t>(length) - padding);
    return output;
}

std::string PrivateKeyPem(EVP_PKEY* key)
{
    BioPtr bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!bio || PEM_write_bio_PrivateKey(bio.get(), key, nullptr, nullptr, 0, nullptr, nullptr) != 1)
    {
        throw std::runtime_error("私钥导出失败。");
    }
    return BioToString(bio.get());
}

std::string Pkcs12Der(PKCS12* pkcs12)
{
    BioPtr bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!bio || i2d_PKCS12_bio(bio.get(), pkcs12) != 1)
    {
        throw std::runtime_error("P12 导出失败。");
    }
    return BioToString(bio.get());
}

std::string OpenSslErrorMessage()
{
    std::string message;
    unsigned long code = 0;
    while ((code = ERR_get_error()) != 0)
    {
        char buffer[256] = {};
        ERR_error_string_n(code, buffer, sizeof(buffer));
        if (!message.empty())
        {
            message += "; ";
        }
        message += buffer;
    }
    return message;
}

const char* Pkcs12Password(const std::string& password)
{
    return password.empty() ? nullptr : password.c_str();
}

bool ParsePkcs12(PKCS12* pkcs12,
                 const std::string& password,
                 EVP_PKEY** private_key,
                 X509** certificate,
                 STACK_OF(X509)** ca_chain)
{
    const auto* password_arg = Pkcs12Password(password);
    if (PKCS12_parse(pkcs12, password_arg, private_key, certificate, ca_chain) == 1)
    {
        return true;
    }
    if (!password.empty())
    {
        return false;
    }

    ERR_clear_error();
    return PKCS12_parse(pkcs12, "", private_key, certificate, ca_chain) == 1;
}

void EnsurePkcs12CanParse(const std::string& der, const std::string& password)
{
    BioPtr bio(BIO_new_mem_buf(der.data(), static_cast<int>(der.size())), BIO_free);
    Pkcs12Ptr pkcs12(d2i_PKCS12_bio(bio.get(), nullptr), PKCS12_free);
    if (!pkcs12)
    {
        throw std::runtime_error("P12 生成后校验失败。");
    }

    EVP_PKEY* raw_private_key = nullptr;
    X509* raw_certificate = nullptr;
    STACK_OF(X509)* raw_ca_chain = nullptr;
    if (!ParsePkcs12(pkcs12.get(), password, &raw_private_key, &raw_certificate, &raw_ca_chain))
    {
        const auto openssl_error = OpenSslErrorMessage();
        throw std::runtime_error(openssl_error.empty() ? "P12 生成后密码校验失败。" : "P12 生成后密码校验失败：" + openssl_error);
    }

    EvpPkeyPtr private_key(raw_private_key, EVP_PKEY_free);
    X509Ptr certificate(raw_certificate, X509_free);
    OpenSslPtr<STACK_OF(X509), X509StackFree> ca_chain(raw_ca_chain, X509StackFree);
}

std::string PublicKeyPem(EVP_PKEY* key)
{
    BioPtr bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!bio || PEM_write_bio_PUBKEY(bio.get(), key) != 1)
    {
        throw std::runtime_error("公钥导出失败。");
    }
    return BioToString(bio.get());
}

std::string CertificatePem(X509* certificate)
{
    BioPtr bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!bio || PEM_write_bio_X509(bio.get(), certificate) != 1)
    {
        throw std::runtime_error("证书导出失败。");
    }
    return BioToString(bio.get());
}

std::string CsrPem(X509_REQ* request)
{
    BioPtr bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!bio || PEM_write_bio_X509_REQ(bio.get(), request) != 1)
    {
        throw std::runtime_error("CSR 导出失败。");
    }
    return BioToString(bio.get());
}

std::string NameToString(X509_NAME* name)
{
    if (!name)
    {
        return {};
    }
    BioPtr bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!bio || X509_NAME_print_ex(bio.get(), name, 0, XN_FLAG_RFC2253) < 0)
    {
        throw std::runtime_error("证书名称解析失败。");
    }
    return BioToString(bio.get());
}

std::string NameEntryValue(X509_NAME* name, int nid)
{
    if (!name)
    {
        return {};
    }

    const int index = X509_NAME_get_index_by_NID(name, nid, -1);
    if (index < 0)
    {
        return {};
    }

    X509_NAME_ENTRY* entry = X509_NAME_get_entry(name, index);
    ASN1_STRING* data = entry ? X509_NAME_ENTRY_get_data(entry) : nullptr;
    unsigned char* raw = nullptr;
    const int length = ASN1_STRING_to_UTF8(&raw, data);
    if (length < 0 || !raw)
    {
        return {};
    }
    OpenSslPtr<unsigned char, OpenSslFree> value(raw, OpenSslFree);
    return std::string(reinterpret_cast<const char*>(value.get()), static_cast<std::size_t>(length));
}

nlohmann::json NameToJson(X509_NAME* name)
{
    return {
        {"commonName", NameEntryValue(name, NID_commonName)},
        {"organization", NameEntryValue(name, NID_organizationName)},
        {"organizationalUnit", NameEntryValue(name, NID_organizationalUnitName)},
        {"country", NameEntryValue(name, NID_countryName)},
        {"state", NameEntryValue(name, NID_stateOrProvinceName)},
        {"locality", NameEntryValue(name, NID_localityName)},
        {"email", NameEntryValue(name, NID_pkcs9_emailAddress)},
        {"raw", NameToString(name)},
    };
}

std::string Asn1TimeToString(const ASN1_TIME* time)
{
    BioPtr bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!bio || ASN1_TIME_print(bio.get(), time) != 1)
    {
        throw std::runtime_error("证书时间解析失败。");
    }
    return BioToString(bio.get());
}

std::string Asn1IntegerToDecimal(const ASN1_INTEGER* integer)
{
    BnPtr number(ASN1_INTEGER_to_BN(integer, nullptr), BN_free);
    if (!number)
    {
        return {};
    }
    char* raw = BN_bn2dec(number.get());
    if (!raw)
    {
        return {};
    }
    OpenSslPtr<char, OpenSslFree> value(raw, OpenSslFree);
    return value.get();
}

std::string Asn1IntegerToHex(const ASN1_INTEGER* integer)
{
    BnPtr number(ASN1_INTEGER_to_BN(integer, nullptr), BN_free);
    if (!number)
    {
        return {};
    }
    char* raw = BN_bn2hex(number.get());
    if (!raw)
    {
        return {};
    }
    OpenSslPtr<char, OpenSslFree> value(raw, OpenSslFree);
    return value.get();
}

std::string ExtensionToString(X509_EXTENSION* extension)
{
    BioPtr bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!bio || X509V3_EXT_print(bio.get(), extension, 0, 0) != 1)
    {
        ASN1_STRING* data = X509_EXTENSION_get_data(extension);
        if (!bio || ASN1_STRING_print(bio.get(), data) != 1)
        {
            return {};
        }
    }
    return BioToString(bio.get());
}

std::string ExtensionValueByNid(X509* certificate, int nid)
{
    const int index = X509_get_ext_by_NID(certificate, nid, -1);
    if (index < 0)
    {
        return {};
    }
    X509_EXTENSION* extension = X509_get_ext(certificate, index);
    return extension ? ExtensionToString(extension) : "";
}

nlohmann::json EmptySanJson()
{
    return {
        {"dns", nlohmann::json::array()},
        {"ips", nlohmann::json::array()},
        {"emails", nlohmann::json::array()},
        {"uris", nlohmann::json::array()},
    };
}

void AppendGeneralNamesToJson(GENERAL_NAMES* names, nlohmann::json& san)
{
    if (!names)
    {
        return;
    }

    const int count = sk_GENERAL_NAME_num(names);
    for (int i = 0; i < count; ++i)
    {
        const GENERAL_NAME* name = sk_GENERAL_NAME_value(names, i);
        if (!name)
        {
            continue;
        }

        if (name->type == GEN_DNS || name->type == GEN_EMAIL || name->type == GEN_URI)
        {
            const ASN1_IA5STRING* data = name->d.ia5;
            const std::string value(reinterpret_cast<const char*>(ASN1_STRING_get0_data(data)),
                                    static_cast<std::size_t>(ASN1_STRING_length(data)));
            if (name->type == GEN_DNS)
            {
                san["dns"].push_back(value);
            }
            else if (name->type == GEN_EMAIL)
            {
                san["emails"].push_back(value);
            }
            else
            {
                san["uris"].push_back(value);
            }
            continue;
        }

        if (name->type == GEN_IPADD)
        {
            const unsigned char* data = ASN1_STRING_get0_data(name->d.iPAddress);
            const int length = ASN1_STRING_length(name->d.iPAddress);
            BioPtr bio(BIO_new(BIO_s_mem()), BIO_free);
            if (bio && length == 4)
            {
                BIO_printf(bio.get(), "%u.%u.%u.%u", data[0], data[1], data[2], data[3]);
                san["ips"].push_back(BioToString(bio.get()));
            }
            else if (bio && length == 16)
            {
                for (int part = 0; part < 8; ++part)
                {
                    if (part > 0)
                    {
                        BIO_puts(bio.get(), ":");
                    }
                    BIO_printf(bio.get(), "%x", (data[part * 2] << 8) | data[part * 2 + 1]);
                }
                san["ips"].push_back(BioToString(bio.get()));
            }
        }
    }
}

nlohmann::json ParseSubjectAltNames(X509* certificate)
{
    nlohmann::json san = EmptySanJson();

    OpenSslPtr<GENERAL_NAMES, GENERAL_NAMES_free> names(
        static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(certificate, NID_subject_alt_name, nullptr, nullptr)),
        GENERAL_NAMES_free);
    AppendGeneralNamesToJson(names.get(), san);
    return san;
}

X509_EXTENSION* ExtensionByNid(const STACK_OF(X509_EXTENSION)* extensions, int nid)
{
    if (!extensions)
    {
        return nullptr;
    }

    for (int i = 0; i < sk_X509_EXTENSION_num(extensions); ++i)
    {
        X509_EXTENSION* extension = sk_X509_EXTENSION_value(extensions, i);
        const ASN1_OBJECT* object = extension ? X509_EXTENSION_get_object(extension) : nullptr;
        if (object && OBJ_obj2nid(object) == nid)
        {
            return extension;
        }
    }
    return nullptr;
}

std::string CsrExtensionValueByNid(const STACK_OF(X509_EXTENSION)* extensions, int nid)
{
    X509_EXTENSION* extension = ExtensionByNid(extensions, nid);
    return extension ? ExtensionToString(extension) : "";
}

nlohmann::json ParseSubjectAltNamesFromExtension(X509_EXTENSION* extension)
{
    nlohmann::json san = EmptySanJson();
    if (!extension)
    {
        return san;
    }

    OpenSslPtr<GENERAL_NAMES, GENERAL_NAMES_free> names(
        static_cast<GENERAL_NAMES*>(X509V3_EXT_d2i(extension)),
        GENERAL_NAMES_free);
    AppendGeneralNamesToJson(names.get(), san);
    return san;
}

std::vector<std::string> ParseKeyUsageValues(X509_EXTENSION* extension)
{
    std::vector<std::string> values;
    if (!extension)
    {
        return values;
    }

    if (X509_EXTENSION_get_critical(extension))
    {
        values.push_back("critical");
    }

    OpenSslPtr<ASN1_BIT_STRING, ASN1_BIT_STRING_free> usage(
        static_cast<ASN1_BIT_STRING*>(X509V3_EXT_d2i(extension)),
        ASN1_BIT_STRING_free);
    if (!usage)
    {
        return values;
    }

    const std::vector<std::pair<int, std::string>> known_usages = {
        {0, "digitalSignature"},
        {2, "keyEncipherment"},
        {3, "dataEncipherment"},
        {4, "keyAgreement"},
        {5, "keyCertSign"},
        {6, "cRLSign"},
    };
    for (const auto& [bit, value] : known_usages)
    {
        if (ASN1_BIT_STRING_get_bit(usage.get(), bit))
        {
            values.push_back(value);
        }
    }
    return values;
}

std::vector<std::string> ParseExtendedKeyUsageValues(X509_EXTENSION* extension)
{
    std::vector<std::string> values;
    if (!extension)
    {
        return values;
    }

    OpenSslPtr<EXTENDED_KEY_USAGE, EXTENDED_KEY_USAGE_free> usages(
        static_cast<EXTENDED_KEY_USAGE*>(X509V3_EXT_d2i(extension)),
        EXTENDED_KEY_USAGE_free);
    if (!usages)
    {
        return values;
    }

    for (int i = 0; i < sk_ASN1_OBJECT_num(usages.get()); ++i)
    {
        const ASN1_OBJECT* object = sk_ASN1_OBJECT_value(usages.get(), i);
        const int nid = object ? OBJ_obj2nid(object) : NID_undef;
        if (nid == NID_server_auth)
        {
            values.push_back("serverAuth");
        }
        else if (nid == NID_client_auth)
        {
            values.push_back("clientAuth");
        }
        else if (nid == NID_code_sign)
        {
            values.push_back("codeSigning");
        }
        else if (nid == NID_email_protect)
        {
            values.push_back("emailProtection");
        }
        else if (nid == NID_time_stamp)
        {
            values.push_back("timeStamping");
        }
        else if (nid == NID_OCSP_sign)
        {
            values.push_back("OCSPSigning");
        }
    }
    return values;
}

std::string PublicKeyAlgorithm(EVP_PKEY* key)
{
    if (!key)
    {
        return {};
    }
    const int type = EVP_PKEY_base_id(key);
    const char* name = OBJ_nid2sn(type);
    return name ? name : "unknown";
}

int PublicKeyBits(EVP_PKEY* key)
{
    return key ? EVP_PKEY_bits(key) : 0;
}

nlohmann::json CertificateToJson(X509* certificate)
{
    EvpPkeyPtr public_key(X509_get_pubkey(certificate), EVP_PKEY_free);

    const auto serial = X509_get_serialNumber(certificate);
    const auto subject = NameToJson(X509_get_subject_name(certificate));
    const auto issuer = NameToJson(X509_get_issuer_name(certificate));
    const auto basic_constraints = ExtensionValueByNid(certificate, NID_basic_constraints);
    const auto key_usage = ExtensionValueByNid(certificate, NID_key_usage);
    const auto extended_key_usage = ExtensionValueByNid(certificate, NID_ext_key_usage);
    const auto san = ParseSubjectAltNames(certificate);
    const bool self_signed = subject.value("raw", "") == issuer.value("raw", "");
    const bool is_ca = basic_constraints.find("CA:TRUE") != std::string::npos;
    const int signature_nid = X509_get_signature_nid(certificate);
    const char* signature_name = OBJ_nid2ln(signature_nid);

    return {
        {"version", X509_get_version(certificate) + 1},
        {"serialNumber", Asn1IntegerToDecimal(serial)},
        {"serialNumberHex", Asn1IntegerToHex(serial)},
        {"subject", subject},
        {"issuer", issuer},
        {"validFrom", Asn1TimeToString(X509_get0_notBefore(certificate))},
        {"validTo", Asn1TimeToString(X509_get0_notAfter(certificate))},
        {"signatureAlgorithm", signature_name ? signature_name : ""},
        {"publicKeyAlgorithm", PublicKeyAlgorithm(public_key.get())},
        {"publicKeyBits", PublicKeyBits(public_key.get())},
        {"isCa", is_ca},
        {"selfSigned", self_signed},
        {"san", san},
        {"basicConstraints", basic_constraints},
        {"keyUsage", key_usage},
        {"extendedKeyUsage", extended_key_usage},
    };
}

EvpPkeyPtr ReadPrivateKey(const std::string& pem)
{
    BioPtr bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), BIO_free);
    EvpPkeyPtr key(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    if (!key)
    {
        throw std::runtime_error("CA 私钥解析失败。");
    }
    return key;
}

X509Ptr ReadCertificate(const std::string& pem)
{
    BioPtr bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), BIO_free);
    X509Ptr certificate(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr), X509_free);
    if (!certificate)
    {
        throw std::runtime_error("CA 证书解析失败。");
    }
    return certificate;
}

X509ReqPtr ReadCsr(const std::string& pem)
{
    BioPtr bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), BIO_free);
    X509ReqPtr request(PEM_read_bio_X509_REQ(bio.get(), nullptr, nullptr, nullptr), X509_REQ_free);
    if (!request)
    {
        throw std::runtime_error("CSR 解析失败。");
    }
    return request;
}

OpenSslPtr<STACK_OF(X509), X509StackFree> ReadCertificateChain(const std::string& pem)
{
    OpenSslPtr<STACK_OF(X509), X509StackFree> certificates(nullptr, X509StackFree);
    if (pem.empty())
    {
        return certificates;
    }

    BioPtr bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), BIO_free);
    certificates.reset(sk_X509_new_null());
    if (!bio || !certificates)
    {
        throw std::runtime_error("CA 链证书集合创建失败。");
    }

    while (true)
    {
        X509* raw_certificate = PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr);
        if (!raw_certificate)
        {
            break;
        }
        X509Ptr certificate(raw_certificate, X509_free);
        if (sk_X509_push(certificates.get(), certificate.get()) <= 0)
        {
            throw std::runtime_error("CA 链证书写入失败。");
        }
        certificate.release();
    }

    if (sk_X509_num(certificates.get()) == 0)
    {
        throw std::runtime_error("CA 链证书解析失败。");
    }
    return certificates;
}

EvpPkeyPtr GenerateKey(const nlohmann::json& request)
{
    const auto algorithm = StringValue(request, "keyAlgorithm", "rsa");
    if (algorithm == "ec")
    {
        const auto curve = StringValue(request, "ecCurve", "prime256v1");
        const int nid = OBJ_txt2nid(curve.c_str());
        if (nid == NID_undef)
        {
            throw std::runtime_error("不支持的 EC 曲线。");
        }

        EvpPkeyCtxPtr params_context(EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr), EVP_PKEY_CTX_free);
        EVP_PKEY* raw_params = nullptr;
        if (!params_context || EVP_PKEY_paramgen_init(params_context.get()) != 1 ||
            EVP_PKEY_CTX_set_ec_paramgen_curve_nid(params_context.get(), nid) != 1 ||
            EVP_PKEY_paramgen(params_context.get(), &raw_params) != 1)
        {
            throw std::runtime_error("EC 曲线参数生成失败。");
        }

        EvpPkeyPtr params(raw_params, EVP_PKEY_free);
        EvpPkeyCtxPtr key_context(EVP_PKEY_CTX_new(params.get(), nullptr), EVP_PKEY_CTX_free);
        EVP_PKEY* raw_key = nullptr;
        if (!key_context || EVP_PKEY_keygen_init(key_context.get()) != 1 || EVP_PKEY_keygen(key_context.get(), &raw_key) != 1)
        {
            throw std::runtime_error("EC 密钥生成失败。");
        }
        return EvpPkeyPtr(raw_key, EVP_PKEY_free);
    }

    const auto bits = std::clamp(IntValue(request, "keyBits", 2048), 2048, 8192);
    EvpPkeyCtxPtr key_context(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
    EVP_PKEY* raw_key = nullptr;
    if (!key_context || EVP_PKEY_keygen_init(key_context.get()) != 1 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(key_context.get(), bits) != 1 ||
        EVP_PKEY_keygen(key_context.get(), &raw_key) != 1)
    {
        throw std::runtime_error("RSA 密钥生成失败。");
    }
    return EvpPkeyPtr(raw_key, EVP_PKEY_free);
}

void AddNameEntry(X509_NAME* name, const char* field, const std::string& value)
{
    if (value.empty())
    {
        return;
    }
    if (X509_NAME_add_entry_by_txt(name,
                                   field,
                                   MBSTRING_UTF8,
                                   reinterpret_cast<const unsigned char*>(value.c_str()),
                                   -1,
                                   -1,
                                   0) != 1)
    {
        throw std::runtime_error("证书主题字段写入失败。");
    }
}

X509_NAME* BuildSubjectName(const nlohmann::json& subject)
{
    X509_NAME* name = X509_NAME_new();
    if (!name)
    {
        throw std::runtime_error("证书主题创建失败。");
    }
    AddNameEntry(name, "C", StringValue(subject, "country"));
    AddNameEntry(name, "ST", StringValue(subject, "state"));
    AddNameEntry(name, "L", StringValue(subject, "locality"));
    AddNameEntry(name, "O", StringValue(subject, "organization"));
    AddNameEntry(name, "OU", StringValue(subject, "organizationalUnit"));
    AddNameEntry(name, "CN", StringValue(subject, "commonName", "space-station.local"));
    AddNameEntry(name, "emailAddress", StringValue(subject, "email"));
    return name;
}

SubjectAltNames ParseSans(const nlohmann::json& request)
{
    const auto san = request.value("san", nlohmann::json::object());
    return {
        StringArrayValue(san, "dns"),
        StringArrayValue(san, "ips"),
        StringArrayValue(san, "emails"),
        StringArrayValue(san, "uris"),
    };
}

std::string BuildSanExtensionValue(const SubjectAltNames& san)
{
    std::vector<std::string> parts;
    for (const auto& value : san.dns)
    {
        parts.push_back("DNS:" + value);
    }
    for (const auto& value : san.ips)
    {
        parts.push_back("IP:" + value);
    }
    for (const auto& value : san.emails)
    {
        parts.push_back("email:" + value);
    }
    for (const auto& value : san.uris)
    {
        parts.push_back("URI:" + value);
    }
    return Join(parts, ",");
}

void AddExtension(X509* certificate, X509* issuer, X509_REQ* csr, int nid, const std::string& value)
{
    if (value.empty())
    {
        return;
    }

    X509V3_CTX context;
    X509V3_set_ctx(&context, issuer, certificate, csr, nullptr, 0);
    X509_EXTENSION* raw_extension = X509V3_EXT_conf_nid(nullptr, &context, nid, value.c_str());
    OpenSslPtr<X509_EXTENSION, X509_EXTENSION_free> extension(raw_extension, X509_EXTENSION_free);
    if (!extension || X509_add_ext(certificate, extension.get(), -1) != 1)
    {
        throw std::runtime_error("证书扩展写入失败。");
    }
}

std::string BuildKeyUsage(bool is_ca, const nlohmann::json& request)
{
    const auto requested = StringArrayValue(request, "keyUsage");
    if (!requested.empty())
    {
        return Join(requested, ",");
    }
    if (is_ca)
    {
        return "critical,keyCertSign,cRLSign";
    }
    return "critical,digitalSignature,keyEncipherment";
}

std::string BuildExtendedKeyUsage(const nlohmann::json& request)
{
    return Join(StringArrayValue(request, "extendedKeyUsage"), ",");
}

void AddCsrExtension(STACK_OF(X509_EXTENSION)* extensions, X509_REQ* request, int nid, const std::string& value)
{
    if (value.empty())
    {
        return;
    }

    X509V3_CTX context;
    X509V3_set_ctx(&context, nullptr, nullptr, request, nullptr, 0);
    X509_EXTENSION* raw_extension = X509V3_EXT_conf_nid(nullptr, &context, nid, value.c_str());
    OpenSslPtr<X509_EXTENSION, X509_EXTENSION_free> extension(raw_extension, X509_EXTENSION_free);
    if (!extension || sk_X509_EXTENSION_push(extensions, extension.get()) <= 0)
    {
        throw std::runtime_error("CSR 扩展写入失败。");
    }
    extension.release();
}

void AddCsrExtensionRequest(X509_REQ* request, const nlohmann::json& source)
{
    OpenSslPtr<STACK_OF(X509_EXTENSION), X509ExtensionStackFree> extensions(
        sk_X509_EXTENSION_new_null(),
        X509ExtensionStackFree);
    if (!extensions)
    {
        throw std::runtime_error("CSR 扩展集合创建失败。");
    }

    AddCsrExtension(extensions.get(), request, NID_key_usage, BuildKeyUsage(BoolValue(source, "isCa", false), source));
    AddCsrExtension(extensions.get(), request, NID_ext_key_usage, BuildExtendedKeyUsage(source));
    AddCsrExtension(extensions.get(), request, NID_subject_alt_name, BuildSanExtensionValue(ParseSans(source)));

    if (sk_X509_EXTENSION_num(extensions.get()) > 0 && X509_REQ_add_extensions(request, extensions.get()) != 1)
    {
        throw std::runtime_error("CSR 扩展请求写入失败。");
    }
}

void SetSerialNumber(X509* certificate, const nlohmann::json& request)
{
    const auto serial_text = StringValue(request, "serialNumber");
    Asn1IntegerPtr serial(ASN1_INTEGER_new(), ASN1_INTEGER_free);
    if (!serial)
    {
        throw std::runtime_error("证书序列号创建失败。");
    }

    if (!serial_text.empty())
    {
        BnPtr number(nullptr, BN_free);
        BIGNUM* raw_number = nullptr;
        if (BN_dec2bn(&raw_number, serial_text.c_str()) == 0)
        {
            throw std::runtime_error("序列号必须是十进制整数。");
        }
        number.reset(raw_number);
        if (BN_to_ASN1_INTEGER(number.get(), serial.get()) == nullptr)
        {
            throw std::runtime_error("序列号写入失败。");
        }
    }
    else
    {
        unsigned char bytes[16] = {};
        if (RAND_bytes(bytes, sizeof(bytes)) != 1)
        {
            throw std::runtime_error("随机序列号生成失败。");
        }
        bytes[0] &= 0x7F;
        BnPtr number(BN_bin2bn(bytes, sizeof(bytes), nullptr), BN_free);
        if (!number || BN_to_ASN1_INTEGER(number.get(), serial.get()) == nullptr)
        {
            throw std::runtime_error("随机序列号写入失败。");
        }
    }

    if (X509_set_serialNumber(certificate, serial.get()) != 1)
    {
        throw std::runtime_error("证书序列号设置失败。");
    }
}

void SetValidity(X509* certificate, int days)
{
    const auto safe_days = std::clamp(days, 1, 36500);
    if (X509_gmtime_adj(X509_getm_notBefore(certificate), 0) == nullptr ||
        X509_gmtime_adj(X509_getm_notAfter(certificate), static_cast<long>(safe_days) * 24L * 60L * 60L) == nullptr)
    {
        throw std::runtime_error("证书有效期设置失败。");
    }
}

nlohmann::json SubjectFromRequest(const nlohmann::json& request)
{
    const auto subject = request.value("subject", nlohmann::json::object());
    return subject.is_object() ? subject : nlohmann::json::object();
}

X509ReqPtr BuildCsr(EVP_PKEY* key, const nlohmann::json& source)
{
    X509ReqPtr request(X509_REQ_new(), X509_REQ_free);
    if (!request || X509_REQ_set_version(request.get(), 0L) != 1)
    {
        throw std::runtime_error("CSR 创建失败。");
    }

    const auto subject_json = SubjectFromRequest(source);
    OpenSslPtr<X509_NAME, X509_NAME_free> subject(BuildSubjectName(subject_json), X509_NAME_free);
    if (X509_REQ_set_subject_name(request.get(), subject.get()) != 1 || X509_REQ_set_pubkey(request.get(), key) != 1)
    {
        throw std::runtime_error("CSR 主题或公钥写入失败。");
    }

    AddCsrExtensionRequest(request.get(), source);

    if (X509_REQ_sign(request.get(), key, EVP_sha256()) <= 0)
    {
        throw std::runtime_error("CSR 签名失败。");
    }
    return request;
}

X509Ptr BuildCertificate(EVP_PKEY* subject_key,
                         X509_NAME* subject,
                         X509* issuer_certificate,
                         EVP_PKEY* issuer_key,
                         X509_REQ* csr,
                         const nlohmann::json& request,
                         bool is_ca)
{
    X509Ptr certificate(X509_new(), X509_free);
    if (!certificate || X509_set_version(certificate.get(), 2L) != 1)
    {
        throw std::runtime_error("证书创建失败。");
    }

    SetSerialNumber(certificate.get(), request);
    SetValidity(certificate.get(), IntValue(request, "validDays", is_ca ? 3650 : 825));

    if (X509_set_subject_name(certificate.get(), subject) != 1)
    {
        throw std::runtime_error("证书主题设置失败。");
    }

    X509_NAME* issuer_name = issuer_certificate ? X509_get_subject_name(issuer_certificate) : subject;
    if (X509_set_issuer_name(certificate.get(), issuer_name) != 1 || X509_set_pubkey(certificate.get(), subject_key) != 1)
    {
        throw std::runtime_error("证书签发者或公钥设置失败。");
    }

    AddExtension(certificate.get(), issuer_certificate, csr, NID_basic_constraints, is_ca ? "critical,CA:TRUE" : "critical,CA:FALSE");
    AddExtension(certificate.get(), issuer_certificate, csr, NID_key_usage, BuildKeyUsage(is_ca, request));
    AddExtension(certificate.get(), issuer_certificate, csr, NID_ext_key_usage, BuildExtendedKeyUsage(request));
    AddExtension(certificate.get(), issuer_certificate, csr, NID_subject_alt_name, BuildSanExtensionValue(ParseSans(request)));

    if (X509_sign(certificate.get(), issuer_key, EVP_sha256()) <= 0)
    {
        throw std::runtime_error("证书签名失败。");
    }
    return certificate;
}

nlohmann::json ErrorJson(const std::exception& error)
{
    return {
        {"ok", false},
        {"error", error.what()},
    };
}

} // namespace

nlohmann::json GenerateCertificateBundle(const nlohmann::json& request)
{
    try
    {
        auto key = GenerateKey(request);
        const auto subject_json = SubjectFromRequest(request);
        auto csr = BuildCsr(key.get(), request);
        const bool is_ca = BoolValue(request, "isCa", false);

        OpenSslPtr<X509_NAME, X509_NAME_free> subject(BuildSubjectName(subject_json), X509_NAME_free);
        auto certificate = BuildCertificate(key.get(), subject.get(), nullptr, key.get(), nullptr, request, is_ca);

        return {
            {"ok", true},
            {"privateKeyPem", PrivateKeyPem(key.get())},
            {"publicKeyPem", PublicKeyPem(key.get())},
            {"csrPem", CsrPem(csr.get())},
            {"certificatePem", CertificatePem(certificate.get())},
        };
    }
    catch (const std::exception& error)
    {
        return ErrorJson(error);
    }
}

nlohmann::json SignCertificateRequest(const nlohmann::json& request)
{
    try
    {
        const auto ca_certificate_pem = StringValue(request, "caCertificatePem");
        const auto ca_private_key_pem = StringValue(request, "caPrivateKeyPem");
        const auto csr_pem = StringValue(request, "csrPem");
        if (ca_certificate_pem.empty() || ca_private_key_pem.empty() || csr_pem.empty())
        {
            throw std::runtime_error("请提供 CA 证书、CA 私钥和 CSR。");
        }

        auto ca_certificate = ReadCertificate(ca_certificate_pem);
        auto ca_private_key = ReadPrivateKey(ca_private_key_pem);
        auto csr = ReadCsr(csr_pem);
        EvpPkeyPtr subject_key(X509_REQ_get_pubkey(csr.get()), EVP_PKEY_free);
        if (!subject_key)
        {
            throw std::runtime_error("CSR 公钥读取失败。");
        }
        if (X509_REQ_verify(csr.get(), subject_key.get()) != 1)
        {
            throw std::runtime_error("CSR 自签名校验失败。");
        }

        X509_NAME* subject = X509_REQ_get_subject_name(csr.get());
        auto certificate =
            BuildCertificate(subject_key.get(), subject, ca_certificate.get(), ca_private_key.get(), csr.get(), request, false);

        return {
            {"ok", true},
            {"certificatePem", CertificatePem(certificate.get())},
        };
    }
    catch (const std::exception& error)
    {
        return ErrorJson(error);
    }
}

nlohmann::json ParseCertificate(const nlohmann::json& request)
{
    try
    {
        const auto certificate_pem = StringValue(request, "certificatePem");
        if (certificate_pem.empty())
        {
            throw std::runtime_error("请提供证书 PEM。");
        }

        auto certificate = ReadCertificate(certificate_pem);
        auto result = CertificateToJson(certificate.get());
        result["ok"] = true;
        return result;
    }
    catch (const std::exception& error)
    {
        return ErrorJson(error);
    }
}

nlohmann::json ParseCsr(const nlohmann::json& request)
{
    try
    {
        const auto csr_pem = StringValue(request, "csrPem");
        if (csr_pem.empty())
        {
            throw std::runtime_error("请提供 CSR PEM。");
        }

        auto csr = ReadCsr(csr_pem);
        EvpPkeyPtr public_key(X509_REQ_get_pubkey(csr.get()), EVP_PKEY_free);
        if (!public_key)
        {
            throw std::runtime_error("CSR 公钥读取失败。");
        }

        OpenSslPtr<STACK_OF(X509_EXTENSION), X509ExtensionStackFree> extensions(
            X509_REQ_get_extensions(csr.get()),
            X509ExtensionStackFree);
        X509_EXTENSION* san_extension = ExtensionByNid(extensions.get(), NID_subject_alt_name);
        X509_EXTENSION* key_usage_extension = ExtensionByNid(extensions.get(), NID_key_usage);
        X509_EXTENSION* extended_key_usage_extension = ExtensionByNid(extensions.get(), NID_ext_key_usage);
        const int signature_nid = X509_REQ_get_signature_nid(csr.get());
        const char* signature_name = OBJ_nid2ln(signature_nid);

        return {
            {"ok", true},
            {"subject", NameToJson(X509_REQ_get_subject_name(csr.get()))},
            {"publicKeyAlgorithm", PublicKeyAlgorithm(public_key.get())},
            {"publicKeyBits", PublicKeyBits(public_key.get())},
            {"signatureAlgorithm", signature_name ? signature_name : ""},
            {"signatureValid", X509_REQ_verify(csr.get(), public_key.get()) == 1},
            {"san", ParseSubjectAltNamesFromExtension(san_extension)},
            {"keyUsage", ParseKeyUsageValues(key_usage_extension)},
            {"extendedKeyUsage", ParseExtendedKeyUsageValues(extended_key_usage_extension)},
            {"keyUsageText", CsrExtensionValueByNid(extensions.get(), NID_key_usage)},
            {"extendedKeyUsageText", CsrExtensionValueByNid(extensions.get(), NID_ext_key_usage)},
        };
    }
    catch (const std::exception& error)
    {
        return ErrorJson(error);
    }
}

nlohmann::json CreatePkcs12(const nlohmann::json& request)
{
    try
    {
        const auto certificate_pem = StringValue(request, "certificatePem");
        const auto private_key_pem = StringValue(request, "privateKeyPem");
        const auto ca_certificate_pem = StringValue(request, "caCertificatePem");
        const auto password = StringValue(request, "password");
        const auto friendly_name = StringValue(request, "friendlyName", "space-station");
        if (certificate_pem.empty() || private_key_pem.empty())
        {
            throw std::runtime_error("请提供证书 PEM 和私钥 PEM。");
        }

        auto certificate = ReadCertificate(certificate_pem);
        auto private_key = ReadPrivateKey(private_key_pem);
        auto ca_chain = ReadCertificateChain(ca_certificate_pem);
        if (X509_check_private_key(certificate.get(), private_key.get()) != 1)
        {
            throw std::runtime_error("证书和私钥不匹配。");
        }

        const auto* password_arg = Pkcs12Password(password);
        Pkcs12Ptr pkcs12(PKCS12_create(password_arg,
                                       friendly_name.c_str(),
                                       private_key.get(),
                                       certificate.get(),
                                       ca_chain.get(),
                                       NID_pbe_WithSHA1And3_Key_TripleDES_CBC,
                                       NID_pbe_WithSHA1And3_Key_TripleDES_CBC,
                                       PKCS12_DEFAULT_ITER,
                                       PKCS12_DEFAULT_ITER,
                                       0),
                         PKCS12_free);
        if (!pkcs12)
        {
            throw std::runtime_error("P12 合成失败。");
        }
        if (PKCS12_set_mac(pkcs12.get(), password_arg, -1, nullptr, 0, PKCS12_DEFAULT_ITER, EVP_sha1()) != 1)
        {
            throw std::runtime_error("P12 SHA1 MAC 设置失败。");
        }
        const auto p12_der = Pkcs12Der(pkcs12.get());
        EnsurePkcs12CanParse(p12_der, password);

        return {
            {"ok", true},
            {"filename", friendly_name.empty() ? "certificate.p12" : friendly_name + ".p12"},
            {"p12Base64", Base64Encode(p12_der)},
        };
    }
    catch (const std::exception& error)
    {
        return ErrorJson(error);
    }
}

nlohmann::json ParsePkcs12(const nlohmann::json& request)
{
    try
    {
        const auto p12_base64 = StringValue(request, "p12Base64");
        const auto password = StringValue(request, "password");
        if (p12_base64.empty())
        {
            throw std::runtime_error("请提供 P12 文件内容。");
        }

        const auto p12_der = Base64Decode(p12_base64);
        BioPtr bio(BIO_new_mem_buf(p12_der.data(), static_cast<int>(p12_der.size())), BIO_free);
        Pkcs12Ptr pkcs12(d2i_PKCS12_bio(bio.get(), nullptr), PKCS12_free);
        if (!pkcs12)
        {
            throw std::runtime_error("P12 解析失败。");
        }

        EVP_PKEY* raw_private_key = nullptr;
        X509* raw_certificate = nullptr;
        STACK_OF(X509)* raw_ca_chain = nullptr;
        if (!ParsePkcs12(pkcs12.get(), password, &raw_private_key, &raw_certificate, &raw_ca_chain))
        {
            throw std::runtime_error("P12 密码错误或内容无效。");
        }

        EvpPkeyPtr private_key(raw_private_key, EVP_PKEY_free);
        X509Ptr certificate(raw_certificate, X509_free);
        OpenSslPtr<STACK_OF(X509), X509StackFree> ca_chain(raw_ca_chain, X509StackFree);

        nlohmann::json certificates = nlohmann::json::array();
        if (certificate)
        {
            auto certificate_json = CertificateToJson(certificate.get());
            certificate_json["role"] = "certificate";
            certificates.push_back(certificate_json);
        }

        const int ca_count = ca_chain ? sk_X509_num(ca_chain.get()) : 0;
        for (int i = 0; i < ca_count; ++i)
        {
            X509* ca_certificate = sk_X509_value(ca_chain.get(), i);
            if (!ca_certificate)
            {
                continue;
            }
            auto certificate_json = CertificateToJson(ca_certificate);
            certificate_json["role"] = "ca";
            certificates.push_back(certificate_json);
        }

        std::string friendly_name;
        if (certificate)
        {
            int alias_length = 0;
            unsigned char* alias = X509_alias_get0(certificate.get(), &alias_length);
            if (alias && alias_length > 0)
            {
                friendly_name.assign(reinterpret_cast<const char*>(alias), static_cast<std::size_t>(alias_length));
            }
        }

        return {
            {"ok", true},
            {"friendlyName", friendly_name},
            {"hasPrivateKey", private_key != nullptr},
            {"privateKeyAlgorithm", PublicKeyAlgorithm(private_key.get())},
            {"privateKeyBits", PublicKeyBits(private_key.get())},
            {"certificateCount", certificates.size()},
            {"caCertificateCount", ca_count},
            {"certificates", certificates},
        };
    }
    catch (const std::exception& error)
    {
        return ErrorJson(error);
    }
}
} // namespace spacestation::cert
