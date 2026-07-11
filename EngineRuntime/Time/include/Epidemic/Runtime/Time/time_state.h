#pragma once

namespace epidemic::runtime
{
enum class DayPhase
{
    Dawn,
    Morning,
    Noon,
    Afternoon,
    Evening,
    Night,
};

enum class TimeRuntimeState
{
    Running,
    Paused,
    TimeScaleChanged,
    Skipping,
    TimeJumped,
    DayChanged,
    PhaseChanged,
};
} // namespace epidemic::runtime
