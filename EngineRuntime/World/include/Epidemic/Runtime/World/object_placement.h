#pragma once

#include "Epidemic/Foundation/string_id.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Foundation/runtime_time.h"
#include "Epidemic/Runtime/Foundation/spatial.h"

#include <optional>
#include <variant>

namespace epidemic::runtime
{
struct WorldSurfacePlacement
{
    RegionId region{};
    ChunkId chunk{};
    Transform transform{};

    [[nodiscard]] constexpr bool operator==(const WorldSurfacePlacement&) const noexcept = default;
};

struct ContainerPlacement
{
    RuntimeObjectId container{};
    foundation::StringId slot{};

    [[nodiscard]] constexpr bool operator==(const ContainerPlacement&) const noexcept = default;
};

struct InventoryPlacement
{
    RuntimeObjectId owner{};

    [[nodiscard]] constexpr bool operator==(const InventoryPlacement&) const noexcept = default;
};

struct EquippedPlacement
{
    RuntimeObjectId owner{};
    foundation::StringId slot{};

    [[nodiscard]] constexpr bool operator==(const EquippedPlacement&) const noexcept = default;
};

struct HiddenPlacement
{
    RegionId region{};

    [[nodiscard]] constexpr bool operator==(const HiddenPlacement&) const noexcept = default;
};

struct DestroyedPlacement
{
    GameTimePoint destroyed_at{};
    foundation::StringId reason{};

    [[nodiscard]] constexpr bool operator==(const DestroyedPlacement&) const noexcept = default;
};

using ObjectPlacement = std::variant<
    WorldSurfacePlacement,
    ContainerPlacement,
    InventoryPlacement,
    EquippedPlacement,
    HiddenPlacement,
    DestroyedPlacement>;

[[nodiscard]] inline std::optional<RegionId> GetPlacementRegion(const ObjectPlacement& placement)
{
    if (const auto* world = std::get_if<WorldSurfacePlacement>(&placement))
    {
        return world->region;
    }
    if (const auto* hidden = std::get_if<HiddenPlacement>(&placement))
    {
        return hidden->region;
    }
    return std::nullopt;
}

[[nodiscard]] inline std::optional<ChunkId> GetPlacementChunk(const ObjectPlacement& placement)
{
    if (const auto* world = std::get_if<WorldSurfacePlacement>(&placement))
    {
        return world->chunk;
    }
    return std::nullopt;
}
} // namespace epidemic::runtime
