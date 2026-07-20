#pragma once

#include "Epidemic/Runtime/Time/game_calendar.h"
#include "Epidemic/Runtime/Time/game_time.h"
#include "Epidemic/Runtime/Time/time_scale.h"
#include "Epidemic/Runtime/Time/time_state.h"

#include <cstdint>

namespace epidemic::runtime
{
struct TimeSnapshot
{
    GameTimePoint now{};
    // Transient result of the latest runtime operation. Snapshot revision tracks
    // authoritative clock state changes, not last_delta-only refreshes.
    GameDuration last_delta{};
    TimeScale time_scale{};
    bool paused = false;
    CalendarDate calendar{};
    DayPhase day_phase = DayPhase::Night;
    std::uint64_t revision = 0;

    [[nodiscard]] constexpr bool operator==(const TimeSnapshot&) const noexcept = default;
};
} // namespace epidemic::runtime
