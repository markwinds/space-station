#include "config/config_store.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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

template <typename Action>
void ExpectThrows(Action&& action, const char* message)
{
    try
    {
        action();
    }
    catch (const std::exception&)
    {
        return;
    }
    throw std::runtime_error(message);
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

        store.SaveAuthenticatorEntry({
            {"id", "otp-1"},
            {"name", "Example"},
            {"issuer", "Example Inc"},
            {"account", "user@example.com"},
            {"secret", "JBSWY3DPEHPK3PXP"},
            {"algorithm", "SHA1"},
            {"digits", 6},
            {"period", 30},
        });
        const auto authenticator_entries = store.LoadAuthenticatorEntries();
        Expect(authenticator_entries.size() == 1, "authenticator entry was not saved");
        Expect(authenticator_entries[0].value("secret", "") == "JBSWY3DPEHPK3PXP", "authenticator secret changed after decrypt");
        const auto authenticator_key = root / "data" / "authenticator" / "vault.key";
        Expect(std::filesystem::file_size(authenticator_key) == 32, "authenticator vault key has the wrong size");
        std::ifstream database_input(database, std::ios::binary);
        const std::string database_contents((std::istreambuf_iterator<char>(database_input)), std::istreambuf_iterator<char>());
        Expect(database_contents.find("JBSWY3DPEHPK3PXP") == std::string::npos, "authenticator secret was stored as plaintext");
        store.DeleteAuthenticatorEntry("otp-1");
        Expect(store.LoadAuthenticatorEntries().empty(), "authenticator entry was not deleted");

        store.SaveSshHosts(nlohmann::json::array({
            {
                {"id", "jump-1"},
                {"name", "Jump"},
                {"host", "192.0.2.10"},
                {"port", 22},
                {"username", "jump-user"},
                {"useAgent", true},
                {"favorite", true},
                {"lastUsedAt", "2026-07-26T12:34:56.000Z"},
            },
            {
                {"id", "target-1"},
                {"name", "Target"},
                {"host", "192.0.2.20"},
                {"port", 22},
                {"username", "target-user"},
                {"jumpHostId", "jump-1"},
            },
        }));
        const auto hosts = store.LoadSshHosts();
        Expect(hosts.size() == 2, "SSH hosts were not saved");
        Expect(hosts[0].value("useAgent", false), "SSH agent setting was not persisted");
        Expect(hosts[0].value("favorite", false), "SSH favorite setting was not persisted");
        Expect(hosts[0].value("lastUsedAt", "") == "2026-07-26T12:34:56.000Z", "SSH recent-use timestamp was not persisted");
        Expect(hosts[1].value("jumpHostId", "") == "jump-1", "jump host setting was not persisted");

        store.SaveSshCredential("target-1", {{"method", "password"}, {"password", "retained-secret"}});
        store.SaveSshHosts(hosts);
        Expect(store.LoadSshCredential("target-1")->value("password", "") == "retained-secret",
               "saving existing hosts changed their credential");
        store.SaveSshHosts(nlohmann::json::array({hosts[0]}));
        Expect(!store.HasSshCredential("target-1"), "removing a host did not remove its credential");
        store.SaveSshHosts(hosts);

        const auto saved_snippet = store.SaveCommandSnippet({
            {"id", "snippet-1"},
            {"name", "Disk usage"},
            {"command", "df -h"},
            {"action", "insert"},
            {"pinned", true},
        });
        Expect(saved_snippet.value("action", "") == "insert", "command snippet action was not saved");
        Expect(!saved_snippet.contains("pinned"), "browser-only pinned state was stored in the shared library");
        store.SaveCommandSnippet({
            {"id", "snippet-1"},
            {"name", "Disk usage updated"},
            {"command", "df -h /"},
            {"action", "run"},
        });
        const auto snippets = store.LoadCommandSnippets();
        Expect(snippets.size() == 1, "saving a shared snippet created a duplicate");
        Expect(snippets[0].value("name", "") == "Disk usage updated", "shared snippet was not updated by id");
        Expect(store.DeleteCommandSnippet("snippet-1"), "shared snippet was not deleted");
        Expect(store.LoadCommandSnippets().empty(), "deleted shared snippet was still returned");
        Expect(!store.DeleteCommandSnippet("missing"), "deleting a missing shared snippet reported success");
        ExpectThrows([&] {
            store.SaveCommandSnippet({{"id", "invalid"}, {"name", ""}, {"command", "whoami"}});
        }, "invalid shared snippet was accepted");

        ExpectThrows([&] {
            store.SaveSshHosts(nlohmann::json::array({{
                {"id", "self"},
                {"name", "Self"},
                {"host", "192.0.2.30"},
                {"port", 22},
                {"username", "root"},
                {"jumpHostId", "self"},
            }}));
        }, "self-referencing jump host was accepted");
        ExpectThrows([&] {
            store.SaveSshHosts(nlohmann::json::array({
                {
                    {"id", "first"},
                    {"name", "First"},
                    {"host", "192.0.2.40"},
                    {"port", 22},
                    {"username", "root"},
                    {"jumpHostId", "second"},
                },
                {
                    {"id", "second"},
                    {"name", "Second"},
                    {"host", "192.0.2.50"},
                    {"port", 22},
                    {"username", "root"},
                    {"jumpHostId", "first"},
                },
            }));
        }, "nested jump hosts were accepted");
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
