#pragma once

namespace epidemic::runtime
{
enum class DayPhase
{
    Dawn,
    Day,
    Dusk,
    Night,
};

enum class TimeRuntimeState
{
    Running,
    Paused,
    TimeScaleChanged,
    TimeJumped,
    DayChanged,
    PhaseChanged,
};
} // namespace epidemic::runtime
