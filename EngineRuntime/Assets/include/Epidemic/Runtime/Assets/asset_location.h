#pragma once

#include "Epidemic/Foundation/string_id.h"

#include <string>
#include <utility>

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

// Canonicalizes only lexical separators and dot segments. It performs no I/O
// and does not make an invalid location valid; call IsValidAssetLocation() at
// authoritative mutation boundaries.
[[nodiscard]] AssetLocation CanonicalizeAssetLocation(AssetLocation location);

// A valid location is a non-empty engine-root-relative path with no embedded
// NUL and no host-rooted, UNC/device or Windows drive-relative form. Package
// and virtual locations require mount_id. Generated locations require
// generator_id. Extra fields are metadata only and do not imply I/O ownership.
[[nodiscard]] bool IsValidAssetLocation(const AssetLocation& location) noexcept;
} // namespace epidemic::runtime
