#pragma once

#include "Epidemic/Foundation/string_id.h"

#include <string>
#include <vector>

namespace epidemic::runtime
{
enum class AssetLocationKind
{
    FilePath,
    PackageEntry,
    VirtualPath,
    Generated,
};

[[nodiscard]] constexpr bool IsValidAssetLocationKind(AssetLocationKind value) noexcept
{
    switch (value)
    {
    case AssetLocationKind::FilePath:
    case AssetLocationKind::PackageEntry:
    case AssetLocationKind::VirtualPath:
    case AssetLocationKind::Generated:
        return true;
    }
    return false;
}

struct AssetLocation
{
    AssetLocationKind kind = AssetLocationKind::FilePath;
    foundation::StringId mount_id{};
    std::string path;
    foundation::StringId generator_id{};

    AssetLocation() = default;
    AssetLocation(AssetLocationKind location_kind, std::string location_path)
        : kind(location_kind), path(std::move(location_path))
    {
    }

    [[nodiscard]] bool Empty() const noexcept { return path.empty(); }
    [[nodiscard]] bool operator==(const AssetLocation&) const = default;
};

[[nodiscard]] AssetLocation CanonicalizeAssetLocation(AssetLocation location);
[[nodiscard]] bool IsValidAssetLocation(const AssetLocation& location) noexcept;
} // namespace epidemic::runtime
