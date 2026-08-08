#include "files/file_manager_service.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace spacestation::files
{
namespace
{
using Json = nlohmann::json;

std::int64_t ModifiedAtMilliseconds(const std::filesystem::path& path)
{
    std::error_code error;
    const auto file_time = std::filesystem::last_write_time(path, error);
    if (error)
    {
        return 0;
    }
    const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        file_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::milliseconds>(system_time.time_since_epoch()).count();
}

std::string ItemType(const std::filesystem::file_status& status)
{
    if (std::filesystem::is_symlink(status)) return "symlink";
    if (std::filesystem::is_directory(status)) return "directory";
    if (std::filesystem::is_regular_file(status)) return "file";
    return "other";
}

Json Breadcrumbs(const std::filesystem::path& path)
{
    Json result = Json::array();
    auto current = path.root_path();
    if (!current.empty())
    {
        auto label = current.string();
        result.push_back({{"label", label.empty() ? "/" : label}, {"path", current.string()}});
    }
    for (const auto& component : path.relative_path())
    {
        current /= component;
        result.push_back({{"label", component.string()}, {"path", current.string()}});
    }
    return result;
}

bool IsFileSystemRoot(const std::filesystem::path& path)
{
    return path == path.root_path();
}

bool IsWithin(const std::filesystem::path& candidate, const std::filesystem::path& parent)
{
    auto candidate_part = candidate.begin();
    for (auto parent_part = parent.begin(); parent_part != parent.end(); ++parent_part, ++candidate_part)
    {
        if (candidate_part == candidate.end() || *candidate_part != *parent_part) return false;
    }
    return true;
}

std::filesystem::path KeepBothPath(const std::filesystem::path& destination, bool directory)
{
    const auto parent = destination.parent_path();
    const auto stem = directory ? destination.filename().string() : destination.stem().string();
    const auto extension = directory ? std::string() : destination.extension().string();
    for (std::size_t index = 1; index < 10000; ++index)
    {
        const auto suffix = index == 1 ? " copy" : " copy " + std::to_string(index);
        const auto candidate = parent / (stem + suffix + extension);
        std::error_code error;
        if (std::filesystem::symlink_status(candidate, error).type() == std::filesystem::file_type::not_found)
        {
            return candidate;
        }
    }
    throw std::runtime_error("无法为副本生成可用名称。");
}

std::runtime_error FileSystemError(const std::string& action,
                                   const std::filesystem::path& path,
                                   const std::error_code& error)
{
    return std::runtime_error(action + "失败：" + path.string() + "（" + error.message() + "）");
}
} // namespace

std::filesystem::path FileManagerService::HomePath()
{
#ifdef _WIN32
    if (const char* profile = std::getenv("USERPROFILE"); profile && *profile)
    {
        return std::filesystem::path(profile);
    }
#else
    if (const char* home = std::getenv("HOME"); home && *home)
    {
        return std::filesystem::path(home);
    }
#endif
    return std::filesystem::current_path();
}

std::filesystem::path FileManagerService::ResolveExisting(const std::string& path)
{
    const auto requested = path.empty() ? HomePath() : std::filesystem::path(path);
    if (!requested.is_absolute())
    {
        throw std::invalid_argument("文件路径必须是绝对路径。");
    }
    std::error_code error;
    const auto resolved = std::filesystem::canonical(requested, error);
    if (error)
    {
        throw FileSystemError("读取路径", requested, error);
    }
    return resolved;
}

std::filesystem::path FileManagerService::ResolveDirectory(const std::string& path)
{
    const auto resolved = ResolveExisting(path);
    if (!std::filesystem::is_directory(resolved))
    {
        throw std::invalid_argument("目标不是文件夹：" + resolved.string());
    }
    return resolved;
}

std::filesystem::path FileManagerService::ResolveEntry(const std::string& path)
{
    const auto requested = std::filesystem::path(path);
    if (!requested.is_absolute())
    {
        throw std::invalid_argument("文件路径必须是绝对路径。");
    }
    if (requested == requested.root_path())
    {
        return ResolveExisting(path);
    }
    std::error_code error;
    const auto parent = std::filesystem::canonical(requested.parent_path(), error);
    if (error) throw FileSystemError("读取路径", requested, error);
    const auto entry = parent / requested.filename();
    const auto status = std::filesystem::symlink_status(entry, error);
    if (error || status.type() == std::filesystem::file_type::not_found)
    {
        if (!error) error = std::make_error_code(std::errc::no_such_file_or_directory);
        throw FileSystemError("读取路径", requested, error);
    }
    return entry;
}

std::string FileManagerService::ValidateName(const std::string& name)
{
    if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos || name.find('\0') != std::string::npos)
    {
        throw std::invalid_argument("请输入不包含路径分隔符的有效名称。");
    }
    return name;
}

void FileManagerService::ValidateUploadId(const std::string& upload_id)
{
    if (upload_id.empty() || upload_id.size() > 80 ||
        !std::all_of(upload_id.begin(), upload_id.end(), [](const unsigned char value) {
            return std::isalnum(value) || value == '-' || value == '_';
        }))
    {
        throw std::invalid_argument("上传任务标识无效。");
    }
}

Json FileManagerService::List(const std::string& path) const
{
    const auto directory = ResolveDirectory(path);
    const auto home = ResolveDirectory(HomePath().string());
    const auto parent = IsFileSystemRoot(directory) ? directory : directory.parent_path();
    std::error_code space_error;
    const auto space = std::filesystem::space(directory, space_error);
    Json items = Json::array();
    std::error_code iterator_error;
    for (std::filesystem::directory_iterator iterator(
             directory, std::filesystem::directory_options::skip_permission_denied, iterator_error), end;
         iterator != end; iterator.increment(iterator_error))
    {
        if (iterator_error)
        {
            iterator_error.clear();
            continue;
        }
        const auto& entry = *iterator;
        std::error_code status_error;
        const auto status = entry.symlink_status(status_error);
        if (status_error) continue;
        const auto type = ItemType(status);
        std::uintmax_t size = 0;
        if (type == "file")
        {
            std::error_code size_error;
            size = entry.file_size(size_error);
            if (size_error) size = 0;
        }
        const auto name = entry.path().filename().string();
        items.push_back({
            {"name", name},
            {"path", entry.path().string()},
            {"type", type},
            {"size", size},
            {"modifiedAt", ModifiedAtMilliseconds(entry.path())},
            {"hidden", !name.empty() && name.front() == '.'},
        });
    }
    if (iterator_error)
    {
        throw FileSystemError("读取文件夹", directory, iterator_error);
    }
    std::sort(items.begin(), items.end(), [](const Json& left, const Json& right) {
        const bool left_directory = left.value("type", "") == "directory";
        const bool right_directory = right.value("type", "") == "directory";
        if (left_directory != right_directory) return left_directory;
        auto left_name = left.value("name", "");
        auto right_name = right.value("name", "");
        std::transform(left_name.begin(), left_name.end(), left_name.begin(), [](const unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        std::transform(right_name.begin(), right_name.end(), right_name.begin(), [](const unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        return left_name < right_name;
    });
    return {
        {"ok", true},
        {"path", directory.string()},
        {"parentPath", parent.string()},
        {"homePath", home.string()},
        {"rootPath", directory.root_path().string()},
        {"availableBytes", space_error ? 0 : space.available},
        {"trashSupported", TrashSupported()},
        {"breadcrumbs", Breadcrumbs(directory)},
        {"items", std::move(items)},
    };
}

void FileManagerService::CreateDirectory(const std::string& parent, const std::string& name) const
{
    const auto destination = ResolveDirectory(parent) / ValidateName(name);
    std::error_code error;
    if (!std::filesystem::create_directory(destination, error))
    {
        if (!error) throw std::runtime_error("同名文件或文件夹已存在。");
        throw FileSystemError("新建文件夹", destination, error);
    }
}

void FileManagerService::Rename(const std::string& path, const std::string& name) const
{
    const auto source = ResolveEntry(path);
    if (IsFileSystemRoot(source)) throw std::invalid_argument("不能重命名文件系统根目录。");
    const auto destination = source.parent_path() / ValidateName(name);
    if (std::filesystem::exists(destination)) throw std::runtime_error("同名文件或文件夹已存在。");
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    if (error) throw FileSystemError("重命名", source, error);
}

std::uintmax_t FileManagerService::Remove(const std::string& path) const
{
    const auto target = ResolveEntry(path);
    if (IsFileSystemRoot(target)) throw std::invalid_argument("不能删除文件系统根目录。");
    std::error_code error;
    const auto removed = std::filesystem::remove_all(target, error);
    if (error) throw FileSystemError("删除", target, error);
    return removed;
}

Json FileManagerService::Transfer(const std::vector<std::string>& sources,
                                  const std::string& destination,
                                  const bool move,
                                  const std::string& conflict_policy) const
{
    if (sources.empty()) throw std::invalid_argument("请选择要传送的文件或文件夹。");
    if (conflict_policy != "error" && conflict_policy != "replace" &&
        conflict_policy != "keepBoth" && conflict_policy != "skip")
    {
        throw std::invalid_argument("文件冲突处理方式无效。");
    }
    const auto target_directory = ResolveDirectory(destination);
    Json results = Json::array();
    std::size_t completed = 0;
    std::size_t skipped = 0;
    for (const auto& source_text : sources)
    {
        const auto source = ResolveEntry(source_text);
        if (IsFileSystemRoot(source)) throw std::invalid_argument("不能复制或移动文件系统根目录。");
        const auto status = std::filesystem::symlink_status(source);
        const bool directory = std::filesystem::is_directory(status);
        if (directory && IsWithin(target_directory, source))
        {
            throw std::invalid_argument("不能将文件夹复制或移动到它自身内部。");
        }
        auto target = target_directory / source.filename();
        const bool same_location = source == target;
        if (same_location && !move && conflict_policy == "keepBoth")
        {
            target = KeepBothPath(target, directory);
        }
        else if (same_location)
        {
            throw std::invalid_argument("来源和目标位置相同。");
        }

        std::error_code error;
        const auto target_status = std::filesystem::symlink_status(target, error);
        const bool target_exists = target_status.type() != std::filesystem::file_type::not_found;
        if (target_exists)
        {
            if (conflict_policy == "error")
            {
                throw std::runtime_error("目标位置已存在同名项目：" + target.filename().string());
            }
            if (conflict_policy == "skip")
            {
                ++skipped;
                results.push_back({{"source", source.string()}, {"target", target.string()}, {"status", "skipped"}});
                continue;
            }
            if (conflict_policy == "keepBoth")
            {
                target = KeepBothPath(target, directory);
            }
            else
            {
                std::filesystem::remove_all(target, error);
                if (error) throw FileSystemError("替换同名项目", target, error);
            }
        }

        error.clear();
        if (move)
        {
            std::filesystem::rename(source, target, error);
            if (error == std::errc::cross_device_link)
            {
                error.clear();
                std::filesystem::copy(source, target,
                                      std::filesystem::copy_options::recursive |
                                          std::filesystem::copy_options::copy_symlinks,
                                      error);
                if (!error) std::filesystem::remove_all(source, error);
            }
            if (error) throw FileSystemError("移动", source, error);
        }
        else
        {
            std::filesystem::copy(source, target,
                                  std::filesystem::copy_options::recursive |
                                      std::filesystem::copy_options::copy_symlinks,
                                  error);
            if (error) throw FileSystemError("复制", source, error);
        }
        ++completed;
        results.push_back({{"source", source.string()}, {"target", target.string()}, {"status", "completed"}});
    }
    return {{"ok", true}, {"completed", completed}, {"skipped", skipped}, {"items", std::move(results)}};
}

bool FileManagerService::TrashSupported() const
{
#ifdef __APPLE__
    return true;
#else
    return false;
#endif
}

Json FileManagerService::MoveToTrash(const std::vector<std::string>& sources) const
{
    if (!TrashSupported()) throw std::runtime_error("当前系统暂不支持移到废纸篓。");
    if (sources.empty()) throw std::invalid_argument("请选择要移到废纸篓的项目。");
    const auto trash = HomePath() / ".Trash";
    std::error_code error;
    std::filesystem::create_directories(trash, error);
    if (error) throw FileSystemError("打开废纸篓", trash, error);
    std::size_t completed = 0;
    for (const auto& source_text : sources)
    {
        const auto source = ResolveEntry(source_text);
        if (IsFileSystemRoot(source)) throw std::invalid_argument("不能将文件系统根目录移到废纸篓。");
        const auto status = std::filesystem::symlink_status(source);
        auto target = trash / source.filename();
        if (std::filesystem::symlink_status(target, error).type() != std::filesystem::file_type::not_found)
        {
            target = KeepBothPath(target, std::filesystem::is_directory(status));
        }
        error.clear();
        std::filesystem::rename(source, target, error);
        if (error == std::errc::cross_device_link)
        {
            error.clear();
            std::filesystem::copy(source, target,
                                  std::filesystem::copy_options::recursive |
                                      std::filesystem::copy_options::copy_symlinks,
                                  error);
            if (!error) std::filesystem::remove_all(source, error);
        }
        if (error) throw FileSystemError("移到废纸篓", source, error);
        ++completed;
    }
    return {{"ok", true}, {"completed", completed}};
}

Json FileManagerService::WriteUploadChunk(const std::string& upload_id,
                                          const std::string& directory,
                                          const std::string& file_name,
                                          const std::uint64_t offset,
                                          const std::uint64_t total_size,
                                          const std::string_view content) const
{
    ValidateUploadId(upload_id);
    const auto parent = ResolveDirectory(directory);
    const auto destination = parent / ValidateName(file_name);
    const auto temporary = parent / (".space-station-upload-" + upload_id + ".part");
    if (offset > total_size || content.size() > total_size - offset)
    {
        throw std::invalid_argument("上传分块范围无效。");
    }
    if (offset == 0)
    {
        std::ofstream reset(temporary, std::ios::binary | std::ios::trunc);
        if (!reset) throw std::runtime_error("无法创建上传临时文件。");
    }
    std::error_code size_error;
    const auto current_size = std::filesystem::file_size(temporary, size_error);
    if (size_error || current_size != offset)
    {
        throw std::runtime_error("上传分块偏移不连续，请重新上传。");
    }
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::app);
        if (!output) throw std::runtime_error("无法写入上传临时文件。");
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!output) throw std::runtime_error("写入上传文件失败。");
    }
    const auto uploaded = offset + content.size();
    if (uploaded == total_size)
    {
        std::error_code error;
        const auto destination_status = std::filesystem::symlink_status(destination, error);
        if (error && error != std::errc::no_such_file_or_directory)
        {
            throw FileSystemError("检查目标文件", destination, error);
        }
        error.clear();
        if (destination_status.type() != std::filesystem::file_type::not_found)
        {
            if (std::filesystem::is_directory(destination_status))
            {
                throw std::runtime_error("同名文件夹已存在。");
            }
            std::filesystem::remove(destination, error);
            if (error) throw FileSystemError("覆盖原文件", destination, error);
        }
        std::filesystem::rename(temporary, destination, error);
        if (error) throw FileSystemError("完成上传", destination, error);
    }
    return {{"ok", true}, {"uploadedBytes", uploaded}, {"totalBytes", total_size}, {"complete", uploaded == total_size}};
}

std::filesystem::path FileManagerService::DownloadPath(const std::string& path) const
{
    const auto resolved = ResolveEntry(path);
    if (!std::filesystem::is_regular_file(resolved))
    {
        throw std::invalid_argument("只能下载普通文件。");
    }
    return resolved;
}
} // namespace spacestation::files
