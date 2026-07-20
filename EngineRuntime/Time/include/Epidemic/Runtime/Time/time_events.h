#pragma once

#include "Epidemic/Runtime/Time/time_snapshot.h"

namespace epidemic::runtime
{
enum class TimeEventKind
{
    TimeAdvanced,
    TimeScaleChanged,
    Paused,
    Resumed,
    TimeJumped,
    DayChanged,
    DayPhaseChanged,
};

struct TimeEvent
{
    TimeEventKind kind = TimeEventKind::TimeAdvanced;
    TimeSnapshot snapshot{};

    [[nodiscard]] constexpr bool operator==(const TimeEvent&) const noexcept = default;
};
} // namespace epidemic::runtime
