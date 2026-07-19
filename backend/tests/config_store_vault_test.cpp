#include "config/config_store.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
} // namespace

int main()
{
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() / ("space-station-vault-test-" + unique);
    try
    {
        spacestation::ConfigStore store(root / "config.json");
        store.SavePartial({{"dataPath", (root / "data").string()}});
        store.SaveSshCredential("host-1", {
            {"method", "password"},
            {"password", "correct horse battery staple"},
            {"privateKey", ""},
            {"passphrase", ""},
        });
        Expect(store.HasSshCredential("host-1"), "saved credential was not found");
        const auto credential = store.LoadSshCredential("host-1");
        Expect(credential.has_value(), "saved credential could not be decrypted");
        Expect(credential->value("password", "") == "correct horse battery staple", "credential changed after decrypt");
        const auto database = root / "data" / "space-station.db";
        const auto key = root / "data" / "ssh" / "vault.key";
        Expect(std::filesystem::is_regular_file(database), "vault database was not created");
        Expect(std::filesystem::file_size(key) == 32, "vault key has the wrong size");
        store.DeleteSshCredential("host-1");
        Expect(!store.HasSshCredential("host-1"), "credential was not deleted");
        std::filesystem::remove_all(root);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        return 1;
    }
}
