#pragma once

#include "Epidemic/Foundation/string_id.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

namespace epidemic::runtime
{
enum class ObjectPlacementKind
{
    WorldSurface,
    Container,
    Equipped,
    Inventory,
    Hidden,
    Destroyed,
};

struct ObjectPlacement
{
    ObjectPlacementKind kind = ObjectPlacementKind::Hidden;
    RegionId region_id{};
    ChunkId chunk_id{};
    PersistentObjectId container_id{};
    foundation::StringId slot_tag{};

    [[nodiscard]] constexpr bool operator==(const ObjectPlacement&) const noexcept = default;
};
} 
