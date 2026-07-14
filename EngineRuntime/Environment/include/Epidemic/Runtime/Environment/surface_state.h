#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"

#include <cstdint>

namespace epidemic::runtime
{
enum class SurfaceConditionKind
{
    Dry,
    Wet,
    Muddy,
    SnowCovered,
    Icy,
    Frozen,
    Thawing,
    Drying,
};

struct SurfaceState
{
    SurfaceId surface_id{};
    RegionId region_id{};
    SurfaceConditionKind condition = SurfaceConditionKind::Dry;
    float wetness = 0.0f;
    float snow_depth = 0.0f;
    float mud_depth = 0.0f;
    float ice_thickness = 0.0f;
    float temperature = 0.0f;
    std::uint64_t revision = 0;

    [[nodiscard]] constexpr bool operator==(const SurfaceState&) const noexcept = default;
};
} // namespace epidemic::runtime

