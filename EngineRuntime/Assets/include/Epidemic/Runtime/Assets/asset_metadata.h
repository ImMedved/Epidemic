#pragma once

#include "Epidemic/Runtime/Assets/asset_location.h"
#include "Epidemic/Runtime/Assets/asset_state.h"
#include "Epidemic/Runtime/Assets/asset_type.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"

// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

#include <cstdint>
#include <cstddef>
#include <vector>

namespace epidemic::runtime
{
// Public mutation limits keep caller-controlled metadata bounded before the catalog
// performs normalization/copies. std::bad_alloc outside these validated limits remains
// a process-level allocation failure; Assets does not invent a module-local OOM policy.
inline constexpr std::size_t kMaxAssetPathBytes = 4096;
inline constexpr std::size_t kMaxAssetDependencies = 4096;
inline constexpr std::size_t kMaxAssetTags = 1024;

struct AssetDependency
{
    AssetId asset_id{};
    bool required = true;

    [[nodiscard]] constexpr bool operator==(const AssetDependency&) const noexcept = default;
};

struct AssetMetadata
{
    AssetId id{};
    AssetType type{};
    AssetLocation location{};
    AssetState state = AssetState::Unknown;
    std::vector<AssetDependency> dependencies;
    std::vector<foundation::StringId> tags;
    std::uint64_t content_hash = 0;
    std::uint32_t version = 0;
};
} 
