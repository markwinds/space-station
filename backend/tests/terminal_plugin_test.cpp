#include "plugins/terminal_plugin_service.hpp"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

using namespace std::chrono_literals;

int main()
{
    const auto root = std::filesystem::temp_directory_path() /
                      ("space-station-plugin-test-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto plugin = root / "split-marker";
    std::filesystem::create_directories(plugin);
    {
        std::ofstream manifest(plugin / "manifest.json");
        manifest << R"({
          "id": "split-marker",
          "name": "Split marker test",
          "enabled": true,
          "transports": ["serial"],
          "targets": ["/dev/test"],
          "allowedHosts": []
        })";
        std::ofstream script(plugin / "index.js");
        script << R"(
          const sessions = new Map();
          const prefix = "@@PING";
          const suffix = "@@";
          function onTerminalData(event) {
            const state = sessions.get(event.sessionId) || { buffered: "", seenIds: [] };
            state.buffered += event.data;
            while (true) {
              const start = state.buffered.indexOf(prefix);
              if (start < 0) {
                state.buffered = state.buffered.slice(-prefix.length);
                break;
              }
              const end = state.buffered.indexOf(suffix, start + prefix.length);
              if (end < 0) {
                state.buffered = state.buffered.slice(start);
                break;
              }
              const frame = state.buffered.slice(start, end + suffix.length);
              state.buffered = state.buffered.slice(end + suffix.length);
              const requestId = frame === "@@PING@@" ? "legacy" : frame.startsWith("@@PING:") ? frame.slice(7, -2) : "";
              if (!requestId || state.seenIds.includes(requestId)) continue;
              state.seenIds.push(requestId);
              sessions.set(event.sessionId, state);
              space.terminal.write("PONG\r\n");
            }
            sessions.set(event.sessionId, state);
          }
        )";
    }

    std::mutex mutex;
    std::condition_variable condition;
    std::string written;
    spacestation::plugins::TerminalPluginService service(root);
    service.Start();
    service.RegisterSession("session-1", "serial", "/dev/test", [&](std::string data) {
        {
            std::lock_guard lock(mutex);
            written += data;
        }
        condition.notify_all();
    });
    service.OnOutput("session-1", "noise@@PING:test");
    service.OnOutput("session-1", "-1@@tail");

    {
        std::unique_lock lock(mutex);
        const auto completed = condition.wait_for(lock, 3s, [&] { return written == "PONG\r\n"; });
        assert(completed);
    }
    service.OnOutput("session-1", "@@PING:test-1@@PONG: command not found\r\n");
    std::this_thread::sleep_for(100ms);
    {
        std::lock_guard lock(mutex);
        assert(written == "PONG\r\n");
    }
    service.OnOutput("session-1", "@@PING:test-2@@");
    {
        std::unique_lock lock(mutex);
        const auto completed = condition.wait_for(lock, 3s, [&] { return written == "PONG\r\nPONG\r\n"; });
        assert(completed);
    }
    const auto status = service.ListPlugins();
    assert(status["plugins"].size() == 1);
    assert(status["plugins"][0]["loaded"] == true);
    const auto effective = service.ListEffectivePlugins("serial", "/dev/test");
    assert(effective["plugins"].size() == 1);
    assert(service.ListEffectivePlugins("ssh", "host-1")["plugins"].empty());

    auto detail = service.GetPlugin("split-marker");
    auto manifest = detail["manifest"];
    manifest["transports"] = {"serial", "ssh", "browser-serial"};
    manifest["targets"] = {"*"};
    service.SavePlugin("split-marker", manifest, R"(
      function onTerminalData(event) {
        if (event.data.includes("@@RELOAD@@")) {
          space.terminal.write(event.transport + ":OK\n");
        }
      }
    )");

    std::string ssh_written;
    std::string browser_written;
    service.RegisterSession("session-ssh", "ssh", "host-1", [&](std::string data) {
        {
            std::lock_guard lock(mutex);
            ssh_written += data;
        }
        condition.notify_all();
    });
    service.RegisterSession("session-browser", "browser-serial", "share-1", [&](std::string data) {
        {
            std::lock_guard lock(mutex);
            browser_written += data;
        }
        condition.notify_all();
    });
    service.OnOutput("session-ssh", "@@RELOAD@@");
    service.OnOutput("session-browser", "@@RELOAD@@");
    {
        std::unique_lock lock(mutex);
        const auto completed = condition.wait_for(lock, 3s, [&] {
            return ssh_written == "ssh:OK\n" && browser_written == "browser-serial:OK\n";
        });
        assert(completed);
    }
    service.UnregisterSession("session-ssh");
    service.UnregisterSession("session-browser");
    service.UnregisterSession("session-1");
    service.DeletePlugin("split-marker");
    assert(!std::filesystem::exists(plugin));
    service.Stop();
    std::filesystem::remove_all(root);
    return 0;
}
