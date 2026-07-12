#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

#include <cstdint>

namespace epidemic::runtime
{
struct EnvironmentUpdateInput
{
    std::uint64_t game_time_ticks = 0;
    std::int64_t game_delta_ticks = 0;
    RegionId region_id{};

    [[nodiscard]] constexpr bool operator==(const EnvironmentUpdateInput&) const noexcept = default;
};
} // namespace epidemic::runtime
