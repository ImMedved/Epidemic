#pragma once

#include "Epidemic/Foundation/string_id.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

namespace epidemic::runtime
{
struct PersistenceLocation
{
    RegionId region_id{};
    ChunkId chunk_id{};
    foundation::StringId location_tag{};

    [[nodiscard]] constexpr bool operator==(const PersistenceLocation&) const noexcept = default;
};
} // namespace epidemic::runtime
