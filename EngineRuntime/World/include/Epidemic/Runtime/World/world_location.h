#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"

namespace epidemic::runtime
{
struct WorldLocation
{
    RegionId region_id{};
    ChunkId chunk_id{};

    [[nodiscard]] constexpr bool operator==(const WorldLocation&) const noexcept = default;
};
} // namespace epidemic::runtime
