#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace spacestation::plugins
{
class TerminalPluginService
{
  public:
    using WriteCallback = std::function<void(std::string)>;

    explicit TerminalPluginService(std::filesystem::path plugin_directory);
    ~TerminalPluginService();

    TerminalPluginService(const TerminalPluginService&) = delete;
    TerminalPluginService& operator=(const TerminalPluginService&) = delete;

    void Start();
    void Stop();

    void RegisterSession(std::string session_id,
                         std::string transport,
                         std::string target,
                         WriteCallback write);
    void UnregisterSession(const std::string& session_id);
    void OnOutput(const std::string& session_id, std::string data);

    nlohmann::json ListPlugins() const;
    nlohmann::json ListEffectivePlugins(const std::string& transport,
                                        const std::string& target) const;
    nlohmann::json GetPlugin(const std::string& plugin_id) const;
    void SavePlugin(const std::string& plugin_id,
                    const nlohmann::json& manifest,
                    const std::string& source);
    void DeletePlugin(const std::string& plugin_id);
    void Reload();

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace spacestation::plugins
