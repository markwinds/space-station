#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace spacestation::files
{
class FileManagerService
{
  public:
    nlohmann::json List(const std::string& path) const;
    void CreateDirectory(const std::string& parent, const std::string& name) const;
    void Rename(const std::string& path, const std::string& name) const;
    std::uintmax_t Remove(const std::string& path) const;
    nlohmann::json Transfer(const std::vector<std::string>& sources,
                            const std::string& destination,
                            bool move,
                            const std::string& conflict_policy) const;
    bool TrashSupported() const;
    nlohmann::json MoveToTrash(const std::vector<std::string>& sources) const;
    nlohmann::json WriteUploadChunk(const std::string& upload_id,
                                    const std::string& directory,
                                    const std::string& file_name,
                                    std::uint64_t offset,
                                    std::uint64_t total_size,
                                    std::string_view content) const;
    std::filesystem::path DownloadPath(const std::string& path) const;

  private:
    static std::filesystem::path HomePath();
    static std::filesystem::path ResolveExisting(const std::string& path);
    static std::filesystem::path ResolveEntry(const std::string& path);
    static std::filesystem::path ResolveDirectory(const std::string& path);
    static std::string ValidateName(const std::string& name);
    static void ValidateUploadId(const std::string& upload_id);
};
} // namespace spacestation::files
