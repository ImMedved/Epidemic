#include "time_runtime_impl.h"

// File note:
// Focused module-level tests for the surrounding runtime component. Each helper builds
// a narrow fixture, and each Test* function verifies one public contract or regression.
#include "Epidemic/Runtime/Time/game_calendar.h"
#include "Epidemic/Runtime/Time/game_time.h"
#include "Epidemic/Runtime/Time/time_events.h"
#include "Epidemic/Runtime/Time/time_runtime.h"
#include "Epidemic/Runtime/Time/time_snapshot.h"
#include "Epidemic/Runtime/Time/time_state.h"

#include <type_traits>

namespace
{
using epidemic::runtime::CalendarDate;
using epidemic::runtime::DayPhase;
using epidemic::runtime::GameDuration;
using epidemic::runtime::GameTime;
using epidemic::runtime::IGameClock;
using epidemic::runtime::ITimeRuntime;
using epidemic::runtime::TimeEventKind;
using epidemic::runtime::TimeRuntime;
using epidemic::runtime::TimeRuntimeState;

// Verifies default game time starts at zero.
bool TestDefaultGameTimeStartsAtZero()
{
    const GameTime time{};
    const GameDuration duration{};

    return time.ticks == 0 && duration.IsZero();
}

// Verifies game time arithmetic works.
bool TestGameTimeArithmeticWorks()
{
    const GameTime start{120};
    const GameDuration delta{45};
    const GameTime end = start + delta;

    return end.ticks == 165 && (end - start).ticks == 45;
}

// Verifies calendar date defaults to first minute.
bool TestCalendarDateDefaultsToFirstMinute()
{
    const CalendarDate date{};
    return date.year == 1 && date.day_of_year == 1 && date.hour == 0 && date.minute == 0;
}

// Verifies time state enums remain distinct.
bool TestTimeStateEnumsRemainDistinct()
{
    return static_cast<int>(DayPhase::Dawn) != static_cast<int>(DayPhase::Night) &&
           static_cast<int>(TimeRuntimeState::Running) != static_cast<int>(TimeRuntimeState::PhaseChanged);
}

// Verifies time runtime contract can be consumed through read only clock.
bool TestTimeRuntimeContractCanBeConsumedThroughReadOnlyClock()
{
    TimeRuntime runtime;
    runtime.SetTimeScale(3.0f);
    runtime.Pause();

    IGameClock& clock = runtime;
    return clock.GetTimeScale() == 3.0f && clock.IsPaused();
}

// Verifies initial time is valid.
bool TestInitialTimeIsValid()
{
    TimeRuntime runtime;
    const auto snapshot = runtime.GetSnapshot();

    return runtime.Now().ticks == 0 && runtime.LastDelta().ticks == 0 && snapshot.date.year == 1 && !snapshot.paused;
}

// Verifies advance updates game time.
bool TestAdvanceUpdatesGameTime()
{
    TimeRuntime runtime;
    runtime.Update(GameDuration{90});

    const auto snapshot = runtime.GetSnapshot();
    const auto& events = runtime.GetEvents();
    return runtime.Now().ticks == 90 && runtime.LastDelta().ticks == 90 && snapshot.now.ticks == 90 &&
           !events.empty() && events.front().kind == TimeEventKind::TimeAdvanced;
}

// Verifies pause stops game delta.
bool TestPauseStopsGameDelta()
{
    TimeRuntime runtime;
    runtime.Update(GameDuration{60});
    runtime.Pause();
    runtime.Update(GameDuration{120});

    return runtime.Now().ticks == 60 && runtime.LastDelta().ticks == 0 && runtime.GetSnapshot().paused;
}

// Verifies resume continues advance.
bool TestResumeContinuesAdvance()
{
    TimeRuntime runtime;
    runtime.Pause();
    runtime.Update(GameDuration{30});
    runtime.Resume();
    runtime.Update(GameDuration{30});

    return !runtime.IsPaused() && runtime.Now().ticks == 30 && runtime.LastDelta().ticks == 30;
}

// Verifies time scale changes delta multiplier.
bool TestTimeScaleChangesDeltaMultiplier()
{
    TimeRuntime runtime;
    runtime.SetTimeScale(2.5f);
    runtime.Update(GameDuration{40});

    const auto& events = runtime.GetEvents();
    return runtime.GetTimeScale() == 2.5f && runtime.LastDelta().ticks == 100 && runtime.Now().ticks == 100 &&
           !events.empty() && events.front().kind == TimeEventKind::TimeAdvanced;
}

// Verifies skip moves time forward.
bool TestSkipMovesTimeForward()
{
    TimeRuntime runtime;
    const auto result = runtime.Skip(GameDuration{90});

    return result.HasValue() && runtime.Now().ticks == 90 && runtime.LastDelta().ticks == 90;
}

// Verifies calendar date conversion is deterministic.
bool TestCalendarDateConversionIsDeterministic()
{
    TimeRuntime runtime;
    const auto skip_result = runtime.Skip(GameDuration{(24 * 60) + 61});
    if (!skip_result.HasValue())
    {
        return false;
    }

    const auto date = runtime.GetCalendarDate();
    return date.year == 1 && date.day_of_year == 2 && date.hour == 1 && date.minute == 1;
}

// Verifies day phase changes at expected thresholds.
bool TestDayPhaseChangesAtExpectedThresholds()
{
    TimeRuntime runtime;
    const auto to_dawn = runtime.Skip(GameDuration{5 * 60});
    if (!to_dawn.HasValue() || runtime.GetDayPhase() != DayPhase::Dawn)
    {
        return false;
    }

    const auto to_morning = runtime.Skip(GameDuration{3 * 60});
    if (!to_morning.HasValue() || runtime.GetDayPhase() != DayPhase::Morning)
    {
        return false;
    }

    const auto to_noon = runtime.Skip(GameDuration{4 * 60});
    if (!to_noon.HasValue() || runtime.GetDayPhase() != DayPhase::Noon)
    {
        return false;
    }

    const auto to_afternoon = runtime.Skip(GameDuration{2 * 60});
    if (!to_afternoon.HasValue() || runtime.GetDayPhase() != DayPhase::Afternoon)
    {
        return false;
    }

    const auto to_evening = runtime.Skip(GameDuration{4 * 60});
    if (!to_evening.HasValue() || runtime.GetDayPhase() != DayPhase::Evening)
    {
        return false;
    }

    const auto to_night = runtime.Skip(GameDuration{4 * 60});
    return to_night.HasValue() && runtime.GetDayPhase() == DayPhase::Night;
}

// Verifies day phase boundary event is produced.
bool TestDayPhaseBoundaryEventIsProduced()
{
    TimeRuntime runtime;
    const auto skip_result = runtime.Skip(GameDuration{5 * 60});
    if (!skip_result.HasValue())
    {
        return false;
    }

    const auto& events = runtime.GetEvents();
    for (const auto& event : events)
    {
        if (event.kind == TimeEventKind::DayPhaseChanged && event.snapshot.phase == DayPhase::Dawn)
        {
            return true;
        }
    }

    return false;
}
} // namespace

// Runs the local test suite and maps failures to stable exit codes.
int main()
{
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::is_trivially_copyable_v<GameTime>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::is_trivially_copyable_v<GameDuration>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::is_trivially_copyable_v<CalendarDate>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::has_virtual_destructor_v<IGameClock>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::has_virtual_destructor_v<ITimeRuntime>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::is_base_of_v<IGameClock, ITimeRuntime>);

    if (!TestDefaultGameTimeStartsAtZero())
    {
        return 1;
    }

    if (!TestGameTimeArithmeticWorks())
    {
        return 2;
    }

    if (!TestCalendarDateDefaultsToFirstMinute())
    {
        return 3;
    }

    if (!TestTimeStateEnumsRemainDistinct())
    {
        return 4;
    }

    if (!TestTimeRuntimeContractCanBeConsumedThroughReadOnlyClock())
    {
        return 5;
    }

    if (!TestInitialTimeIsValid())
    {
        return 6;
    }

    if (!TestAdvanceUpdatesGameTime())
    {
        return 7;
    }

    if (!TestPauseStopsGameDelta())
    {
        return 8;
    }

    if (!TestResumeContinuesAdvance())
    {
        return 9;
    }

    if (!TestTimeScaleChangesDeltaMultiplier())
    {
        return 10;
    }

    if (!TestSkipMovesTimeForward())
    {
        return 11;
    }

    if (!TestCalendarDateConversionIsDeterministic())
    {
        return 12;
    }

    if (!TestDayPhaseChangesAtExpectedThresholds())
    {
        return 13;
    }

    if (!TestDayPhaseBoundaryEventIsProduced())
    {
        return 14;
    }

    return 0;
}
