#pragma once

#include "Epidemic/Runtime/Time/time_snapshot.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

namespace epidemic::runtime
{
enum class TimeEventKind
{
    TimeAdvanced,
    TimeScaleChanged,
    Paused,
    Resumed,
    TimeSkipped,
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
