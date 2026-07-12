#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

namespace epidemic::runtime
{
struct WorldLocation
{
    RegionId region_id{};
    ChunkId chunk_id{};

    [[nodiscard]] constexpr bool operator==(const WorldLocation&) const noexcept = default;
};
} 
