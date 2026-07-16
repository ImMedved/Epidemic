#include "time_runtime_impl.h"

#include "Epidemic/Runtime/Time/time_runtime.h"

#include <chrono>
#include <limits>
#include <type_traits>

namespace
{
using epidemic::runtime::CalendarDate;
using epidemic::runtime::CreateTimeServices;
using epidemic::runtime::DayPhase;
using epidemic::runtime::GameDuration;
using epidemic::runtime::GameTimePoint;
using epidemic::runtime::IGameClock;
using epidemic::runtime::ITimeRuntime;
using epidemic::runtime::IsZero;
using epidemic::runtime::PhaseBoundary;
using epidemic::runtime::TimeEventKind;
using epidemic::runtime::TimeOptions;
using epidemic::runtime::TimeRuntime;

bool TestGameTimeArithmeticWorks()
{
    const GameTimePoint start{120};
    const GameDuration delta{45};
    const GameTimePoint end = start + delta;

    return end.ticks == 165 && (end - delta).ticks == 120 && (end - start).ticks == 45;
}

bool TestInitialSnapshotIsStable()
{
    TimeRuntime runtime;
    const auto snapshot = runtime.GetSnapshot();

    return runtime.Now().ticks == 0 && IsZero(runtime.LastDelta()) && snapshot.calendar.year == 1 &&
           snapshot.calendar.month == 1 && snapshot.calendar.day == 1 && !snapshot.paused &&
           snapshot.revision == 0;
}

bool TestDeterministicAccumulationKeepsFractionalRemainder()
{
    TimeOptions options{};
    options.game_ticks_per_real_second = 10;
    TimeRuntime runtime(options);

    const auto first = runtime.Advance(std::chrono::milliseconds(50));
    const auto second = runtime.Advance(std::chrono::milliseconds(50));

    return first && second && first.Value().current.now.ticks == 0 && second.Value().current.now.ticks == 1 &&
           runtime.Now().ticks == 1;
}

bool TestDifferentFrameSplitsProduceSameGameTime()
{
    TimeOptions options{};
    options.game_ticks_per_real_second = 20;

    TimeRuntime single_step(options);
    TimeRuntime split_step(options);

    const auto whole = single_step.Advance(std::chrono::seconds(1));
    bool ok = static_cast<bool>(whole);
    for (int index = 0; index < 10; ++index)
    {
        ok = split_step.Advance(std::chrono::milliseconds(100)) && ok;
    }

    return ok && single_step.Now().ticks == 20 && split_step.Now().ticks == single_step.Now().ticks;
}

bool TestPauseResume()
{
    TimeRuntime runtime;
    const auto advanced = runtime.Advance(std::chrono::seconds(1));
    const auto paused = runtime.Pause();
    const auto blocked = runtime.Advance(std::chrono::seconds(1));
    const auto resumed = runtime.Resume();
    const auto advanced_again = runtime.Advance(std::chrono::seconds(1));

    return advanced && paused && blocked && resumed && advanced_again && runtime.Now().ticks == 2 &&
           runtime.LastDelta().ticks == 1 && !runtime.GetSnapshot().paused;
}

bool TestInvalidTimeScaleIsRejected()
{
    TimeRuntime runtime;
    const auto negative = runtime.SetTimeScale(-1.0);
    const auto infinity = runtime.SetTimeScale(std::numeric_limits<double>::infinity());
    const auto zero = runtime.SetTimeScale(0.0);

    return !negative && negative.GetError().HasCode("time.invalid_scale") && !infinity && !zero &&
           runtime.GetSnapshot().time_scale == 1.0;
}

bool TestTimeScaleChangesDeltaMultiplier()
{
    TimeRuntime runtime;
    const auto scale = runtime.SetTimeScale(2.5);
    const auto advanced = runtime.Advance(std::chrono::seconds(2));

    const auto& events = runtime.GetEvents();
    return scale && advanced && runtime.LastDelta().ticks == 5 && runtime.Now().ticks == 5 &&
           !events.empty() && events.front().kind == TimeEventKind::TimeAdvanced;
}

bool TestTimeSkipProducesJumpEvent()
{
    TimeRuntime runtime;
    const auto result = runtime.Skip(GameDuration{90});

    const auto& events = runtime.GetEvents();
    return result && runtime.Now().ticks == 90 && runtime.LastDelta().ticks == 90 && !events.empty() &&
           events.front().kind == TimeEventKind::TimeJumped;
}

bool TestCalendarConversion()
{
    TimeRuntime runtime;
    const auto skip_result = runtime.Skip(GameDuration{(24 * 60 * 60) + (61 * 60)});
    if (!skip_result)
    {
        return false;
    }

    const auto date = runtime.GetSnapshot().calendar;
    const auto point = runtime.ToGameTimePoint(date);
    return date.year == 1 && date.month == 1 && date.day == 2 && date.hour == 1 && date.minute == 1 && point &&
           point.Value().ticks == runtime.Now().ticks;
}

bool TestDateValidation()
{
    TimeRuntime runtime;
    const auto invalid_month = runtime.ToGameTimePoint(CalendarDate{1, 13, 1, 0, 0});
    const auto invalid_minute = runtime.ToGameTimePoint(CalendarDate{1, 1, 1, 0, 60});

    return !invalid_month && invalid_month.GetError().HasCode("time.invalid_date") && !invalid_minute;
}

bool TestDayAndPhaseTransitions()
{
    TimeRuntime runtime;
    const auto to_dawn = runtime.Skip(GameDuration{5 * 60 * 60});
    if (!to_dawn || runtime.GetSnapshot().day_phase != DayPhase::Dawn)
    {
        return false;
    }

    const auto to_day = runtime.Skip(GameDuration{3 * 60 * 60});
    if (!to_day || runtime.GetSnapshot().day_phase != DayPhase::Day)
    {
        return false;
    }

    const auto to_next_day = runtime.Skip(GameDuration{16 * 60 * 60});
    if (!to_next_day)
    {
        return false;
    }

    bool saw_day = false;
    bool saw_phase = false;
    for (const auto& event : runtime.GetEvents())
    {
        saw_day = saw_day || event.kind == TimeEventKind::DayChanged;
        saw_phase = saw_phase || event.kind == TimeEventKind::DayPhaseChanged;
    }

    return saw_day && saw_phase;
}

bool TestRevisionIncrementsOnlyOnChanges()
{
    TimeRuntime runtime;
    const auto initial = runtime.GetSnapshot().revision;
    const auto idle = runtime.Advance(std::chrono::microseconds(0));
    const auto paused = runtime.Pause();
    const auto duplicate_pause = runtime.Pause();

    return idle && paused && duplicate_pause && idle.Value().current.revision == initial &&
           runtime.GetSnapshot().revision == initial + 1;
}

bool TestFactoryCreatesSplitServices()
{
    const auto services = CreateTimeServices({});
    if (!services)
    {
        return false;
    }

    return services.Value().clock != nullptr && services.Value().runtime != nullptr;
}

bool TestFactoryRejectsInvalidOptions()
{
    TimeOptions invalid_rate{};
    invalid_rate.game_ticks_per_real_second = 0;

    TimeOptions invalid_boundary{};
    invalid_boundary.phase_boundaries = {
        PhaseBoundary{24 * 60, DayPhase::Day},
    };

    const auto rate_result = CreateTimeServices(invalid_rate);
    const auto boundary_result = CreateTimeServices(invalid_boundary);
    return !rate_result && rate_result.GetError().HasCode("time.invalid_options") && !boundary_result &&
           boundary_result.GetError().HasCode("time.invalid_phase_boundary");
}

bool TestConfigurablePhaseBoundaries()
{
    TimeOptions options{};
    options.phase_boundaries = {
        PhaseBoundary{0, DayPhase::Night},
        PhaseBoundary{1, DayPhase::Day},
    };

    TimeRuntime runtime(options);
    const auto advanced = runtime.Skip(GameDuration{60});
    return advanced && runtime.GetSnapshot().day_phase == DayPhase::Day;
}
} // namespace

int main()
{
    static_assert(std::is_trivially_copyable_v<GameTimePoint>);
    static_assert(std::is_trivially_copyable_v<GameDuration>);
    static_assert(std::is_trivially_copyable_v<CalendarDate>);
    static_assert(std::has_virtual_destructor_v<IGameClock>);
    static_assert(std::has_virtual_destructor_v<ITimeRuntime>);

    if (!TestGameTimeArithmeticWorks())
    {
        return 1;
    }
    if (!TestInitialSnapshotIsStable())
    {
        return 2;
    }
    if (!TestDeterministicAccumulationKeepsFractionalRemainder())
    {
        return 3;
    }
    if (!TestDifferentFrameSplitsProduceSameGameTime())
    {
        return 4;
    }
    if (!TestPauseResume())
    {
        return 5;
    }
    if (!TestInvalidTimeScaleIsRejected())
    {
        return 6;
    }
    if (!TestTimeScaleChangesDeltaMultiplier())
    {
        return 7;
    }
    if (!TestTimeSkipProducesJumpEvent())
    {
        return 8;
    }
    if (!TestCalendarConversion())
    {
        return 9;
    }
    if (!TestDateValidation())
    {
        return 10;
    }
    if (!TestDayAndPhaseTransitions())
    {
        return 11;
    }
    if (!TestRevisionIncrementsOnlyOnChanges())
    {
        return 12;
    }
    if (!TestFactoryCreatesSplitServices())
    {
        return 13;
    }
    if (!TestFactoryRejectsInvalidOptions())
    {
        return 14;
    }
    if (!TestConfigurablePhaseBoundaries())
    {
        return 15;
    }

    return 0;
}
