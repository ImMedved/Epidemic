#pragma once

#include "Epidemic/Runtime/Time/game_calendar.h"
#include "Epidemic/Runtime/Time/game_time.h"
#include "Epidemic/Runtime/Time/time_state.h"

#include <cstdint>

namespace epidemic::runtime
{
struct TimeSnapshot
{
    GameTimePoint now{};
    GameDuration last_delta{};
    double time_scale = 1.0;
    bool paused = false;
    CalendarDate calendar{};
    DayPhase day_phase = DayPhase::Night;
    std::uint64_t revision = 0;

    [[nodiscard]] constexpr bool operator==(const TimeSnapshot&) const noexcept = default;
};
} // namespace epidemic::runtime
