#pragma once


// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.
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
} 
