#pragma once

#include <string>

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

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
} 
