#include "time_runtime_impl.h"

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

bool TestDefaultGameTimeStartsAtZero()
{
    const GameTime time{};
    const GameDuration duration{};

    return time.ticks == 0 && duration.IsZero();
}

bool TestGameTimeArithmeticWorks()
{
    const GameTime start{120};
    const GameDuration delta{45};
    const GameTime end = start + delta;

    return end.ticks == 165 && (end - start).ticks == 45;
}

bool TestCalendarDateDefaultsToFirstMinute()
{
    const CalendarDate date{};
    return date.year == 1 && date.day_of_year == 1 && date.hour == 0 && date.minute == 0;
}

bool TestTimeStateEnumsRemainDistinct()
{
    return static_cast<int>(DayPhase::Dawn) != static_cast<int>(DayPhase::Night) &&
           static_cast<int>(TimeRuntimeState::Running) != static_cast<int>(TimeRuntimeState::PhaseChanged);
}

bool TestTimeRuntimeContractCanBeConsumedThroughReadOnlyClock()
{
    TimeRuntime runtime;
    runtime.SetTimeScale(3.0f);
    runtime.Pause();

    IGameClock& clock = runtime;
    return clock.GetTimeScale() == 3.0f && clock.IsPaused();
}

bool TestInitialTimeIsValid()
{
    TimeRuntime runtime;
    const auto snapshot = runtime.GetSnapshot();

    return runtime.Now().ticks == 0 && runtime.LastDelta().ticks == 0 && snapshot.date.year == 1 && !snapshot.paused;
}

bool TestAdvanceUpdatesGameTime()
{
    TimeRuntime runtime;
    runtime.Update(GameDuration{90});

    const auto snapshot = runtime.GetSnapshot();
    const auto& events = runtime.GetEvents();
    return runtime.Now().ticks == 90 && runtime.LastDelta().ticks == 90 && snapshot.now.ticks == 90 &&
           !events.empty() && events.front().kind == TimeEventKind::TimeAdvanced;
}

bool TestPauseStopsGameDelta()
{
    TimeRuntime runtime;
    runtime.Update(GameDuration{60});
    runtime.Pause();
    runtime.Update(GameDuration{120});

    return runtime.Now().ticks == 60 && runtime.LastDelta().ticks == 0 && runtime.GetSnapshot().paused;
}

bool TestResumeContinuesAdvance()
{
    TimeRuntime runtime;
    runtime.Pause();
    runtime.Update(GameDuration{30});
    runtime.Resume();
    runtime.Update(GameDuration{30});

    return !runtime.IsPaused() && runtime.Now().ticks == 30 && runtime.LastDelta().ticks == 30;
}

bool TestTimeScaleChangesDeltaMultiplier()
{
    TimeRuntime runtime;
    runtime.SetTimeScale(2.5f);
    runtime.Update(GameDuration{40});

    const auto& events = runtime.GetEvents();
    return runtime.GetTimeScale() == 2.5f && runtime.LastDelta().ticks == 100 && runtime.Now().ticks == 100 &&
           !events.empty() && events.front().kind == TimeEventKind::TimeAdvanced;
}

bool TestSkipMovesTimeForward()
{
    TimeRuntime runtime;
    const auto result = runtime.Skip(GameDuration{90});

    return result.HasValue() && runtime.Now().ticks == 90 && runtime.LastDelta().ticks == 90;
}

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

int main()
{
    static_assert(std::is_trivially_copyable_v<GameTime>);
    static_assert(std::is_trivially_copyable_v<GameDuration>);
    static_assert(std::is_trivially_copyable_v<CalendarDate>);
    static_assert(std::has_virtual_destructor_v<IGameClock>);
    static_assert(std::has_virtual_destructor_v<ITimeRuntime>);
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
