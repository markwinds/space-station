#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

namespace spacestation
{
struct EmbeddedAsset
{
    const unsigned char* data;
    std::size_t size;
    std::string_view content_type;
};

using EmbeddedAssetStore = std::unordered_map<std::string, EmbeddedAsset>;

extern const EmbeddedAssetStore kEmbeddedAssets;
} // namespace spacestation
