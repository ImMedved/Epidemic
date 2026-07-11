#pragma once

#include <string>

namespace epidemic::runtime
{
enum class AssetLocationKind
{
    FilePath,
    PackageEntry,
    VirtualPath,
    Generated,
};

struct AssetLocation
{
    AssetLocationKind kind = AssetLocationKind::FilePath;
    std::string value;

    [[nodiscard]] bool Empty() const noexcept
    {
        return value.empty();
    }

    [[nodiscard]] bool operator==(const AssetLocation&) const = default;
};
} // namespace epidemic::runtime
