#pragma once

#include "Epidemic/Runtime/Time/game_calendar.h"
#include "Epidemic/Runtime/Time/game_time.h"
#include "Epidemic/Runtime/Time/time_state.h"

namespace epidemic::runtime
{
struct TimeSnapshot
{
    GameTime now{};
    GameDuration delta{};
    CalendarDate date{};
    DayPhase phase = DayPhase::Night;
    float time_scale = 1.0f;
    bool paused = false;

    [[nodiscard]] constexpr bool operator==(const TimeSnapshot&) const noexcept = default;
};
} // namespace epidemic::runtime
