#include "files/file_manager_service.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

int main()
{
    const auto root = std::filesystem::temp_directory_path() /
                      ("space-station-files-test-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    spacestation::files::FileManagerService service;

    service.CreateDirectory(root.string(), "folder");
    const auto listing = service.List(root.string());
    assert(listing["items"].size() == 1);
    assert(listing["items"][0]["name"] == "folder");

    service.Rename((root / "folder").string(), "renamed");
    assert(std::filesystem::is_directory(root / "renamed"));

    const auto first_chunk = service.WriteUploadChunk("test-upload", (root / "renamed").string(),
                                                       "sample.txt", 0, 6, "abc");
    assert(first_chunk["complete"] == false);
    const auto final_chunk = service.WriteUploadChunk("test-upload", (root / "renamed").string(),
                                                       "sample.txt", 3, 6, "def");
    assert(final_chunk["complete"] == true);
    std::ifstream uploaded(root / "renamed" / "sample.txt", std::ios::binary);
    assert(std::string(std::istreambuf_iterator<char>(uploaded), std::istreambuf_iterator<char>()) == "abcdef");

#ifndef _WIN32
    const auto target = root / "target.txt";
    {
        std::ofstream output(target);
        output << "keep";
    }
    const auto link = root / "target-link";
    std::filesystem::create_symlink(target, link);
    service.Rename(link.string(), "renamed-link");
    assert(std::filesystem::exists(target));
    assert(std::filesystem::is_symlink(root / "renamed-link"));
    service.Remove((root / "renamed-link").string());
    assert(std::filesystem::exists(target));
#endif

    assert(service.DownloadPath((root / "renamed" / "sample.txt").string()).filename() == "sample.txt");
    service.CreateDirectory(root.string(), "destination");
    const auto copied = service.Transfer({(root / "renamed" / "sample.txt").string()},
                                         (root / "destination").string(), false, "error");
    assert(copied["completed"] == 1);
    assert(std::filesystem::exists(root / "destination" / "sample.txt"));
    const auto kept_both = service.Transfer({(root / "renamed" / "sample.txt").string()},
                                            (root / "destination").string(), false, "keepBoth");
    assert(kept_both["completed"] == 1);
    assert(std::filesystem::exists(root / "destination" / "sample copy.txt"));
    const auto same_folder_copy = service.Transfer({(root / "renamed" / "sample.txt").string()},
                                                   (root / "renamed").string(), false, "keepBoth");
    assert(same_folder_copy["completed"] == 1);
    assert(std::filesystem::exists(root / "renamed" / "sample copy.txt"));
    service.CreateDirectory(root.string(), "move-target");
    const auto moved = service.Transfer({(root / "destination" / "sample.txt").string()},
                                        (root / "move-target").string(), true, "error");
    assert(moved["completed"] == 1);
    assert(!std::filesystem::exists(root / "destination" / "sample.txt"));
    assert(std::filesystem::exists(root / "move-target" / "sample.txt"));
    assert(service.Remove((root / "renamed").string()) == 3);
    std::filesystem::remove_all(root);
    return 0;
}
