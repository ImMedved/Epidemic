#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

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
    SurfaceConditionKind condition = SurfaceConditionKind::Dry;
    float wetness = 0.0f;
    float snow_depth = 0.0f;
    float mud_depth = 0.0f;
    float ice_thickness = 0.0f;
    float temperature = 0.0f;

    [[nodiscard]] constexpr bool operator==(const SurfaceState&) const noexcept = default;
};
} // namespace epidemic::runtime
