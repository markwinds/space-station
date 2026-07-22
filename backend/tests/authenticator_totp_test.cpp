#include "authenticator/authenticator_service.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
void ExpectEqual(const std::string& actual, const std::string& expected, const std::string& label)
{
    if (actual == expected) return;
    std::cerr << label << ": expected " << expected << ", got " << actual << '\n';
    std::exit(1);
}
} // namespace

int main()
{
    using spacestation::authenticator::GenerateTotp;
    // RFC 6238 Appendix B test vectors, Base32 encodings of the documented ASCII secrets.
    ExpectEqual(GenerateTotp("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ", 59, 30, 8, "SHA1"), "94287082", "SHA1");
    ExpectEqual(GenerateTotp("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQGEZA", 59, 30, 8, "SHA256"), "46119246", "SHA256");
    ExpectEqual(GenerateTotp("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQGEZDGNA", 59, 30, 8, "SHA512"), "90693936", "SHA512");
    return 0;
}
