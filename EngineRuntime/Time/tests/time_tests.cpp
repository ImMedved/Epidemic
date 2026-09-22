#include "time_runtime_impl.h"

#include "Epidemic/Runtime/Time/time_runtime.h"

#include <chrono>
#include <limits>
#include <stdexcept>
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
using epidemic::runtime::TimeScale;
using epidemic::runtime::TimeEventKind;
using epidemic::runtime::TimeCheckpoint;
using epidemic::runtime::TimeOptions;
using epidemic::runtime::TimeRuntime;
using epidemic::runtime::ValidateTimeOptions;

bool SameCheckpoint(const TimeCheckpoint& left, const TimeCheckpoint& right)
{
    if (left.now != right.now || left.time_scale != right.time_scale || left.paused != right.paused ||
        left.tick_remainder_numerator != right.tick_remainder_numerator || left.revision != right.revision ||
        left.game_ticks_per_real_second != right.game_ticks_per_real_second ||
        left.calendar.hours_per_day != right.calendar.hours_per_day ||
        left.calendar.days_per_month != right.calendar.days_per_month ||
        left.calendar.months_per_year != right.calendar.months_per_year ||
        left.phase_boundaries.size() != right.phase_boundaries.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < left.phase_boundaries.size(); ++index)
    {
        if (left.phase_boundaries[index].start_minute != right.phase_boundaries[index].start_minute ||
            left.phase_boundaries[index].phase != right.phase_boundaries[index].phase)
        {
            return false;
        }
    }
    return true;
}

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

bool TestNegativeAdvancePreservesObservableState()
{
    TimeRuntime runtime;
    const auto seeded = runtime.Skip(GameDuration{90});
    if (!seeded)
    {
        return false;
    }

    const auto before_snapshot = runtime.GetSnapshot();
    const auto before_checkpoint = runtime.CaptureCheckpoint();
    const auto before_events = runtime.GetEvents();
    const auto result = runtime.Advance(std::chrono::microseconds{-1});

    return !result && result.GetError().HasCode("time.invalid_delta") && runtime.GetSnapshot() == before_snapshot &&
           SameCheckpoint(runtime.CaptureCheckpoint(), before_checkpoint) && runtime.GetEvents() == before_events;
}

bool TestRationalAccumulatorAvoidsFloatingDrift()
{
    TimeOptions options{};
    options.initial_time_scale = TimeScale{1, 3};
    TimeRuntime runtime(options);

    const auto first = runtime.Advance(std::chrono::seconds(1));
    const auto second = runtime.Advance(std::chrono::seconds(1));
    const auto third = runtime.Advance(std::chrono::seconds(1));

    return first && second && third && first.Value().current.now.ticks == 0 &&
           second.Value().current.now.ticks == 0 && third.Value().current.now.ticks == 1 &&
           runtime.Now().ticks == 1;
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
    const auto negative = runtime.SetTimeScale(TimeScale{-1, 1});
    const auto zero_numerator = runtime.SetTimeScale(TimeScale{0, 1});
    const auto zero_denominator = runtime.SetTimeScale(TimeScale{1, 0});

    return !negative && negative.GetError().HasCode("time.invalid_scale") && !zero_numerator &&
           !zero_denominator && runtime.GetSnapshot().time_scale == TimeScale{};
}

bool TestTimeScaleChangesDeltaMultiplier()
{
    TimeRuntime runtime;
    const auto scale = runtime.SetTimeScale(TimeScale{5, 2});
    const auto advanced = runtime.Advance(std::chrono::seconds(2));

    const auto& events = runtime.GetEvents();
    return scale && advanced && runtime.LastDelta().ticks == 5 && runtime.Now().ticks == 5 &&
           runtime.GetSnapshot().time_scale == TimeScale{5, 2} && !events.empty() &&
           events.front().kind == TimeEventKind::TimeAdvanced;
}

bool TestTimeScaleIsNormalized()
{
    TimeRuntime runtime;
    const auto scale = runtime.SetTimeScale(TimeScale{10, 4});

    return scale && runtime.GetSnapshot().time_scale == TimeScale{5, 2};
}

bool TestTimeScaleChangeDropsFractionalRemainder()
{
    TimeOptions options{};
    options.initial_time_scale = TimeScale{1, 3};
    TimeRuntime runtime(options);

    const auto fractional = runtime.Advance(std::chrono::seconds(1));
    const auto scale = runtime.SetTimeScale(TimeScale{1, 2});
    const auto advanced = runtime.Advance(std::chrono::seconds(1));

    return fractional && fractional.Value().current.now.ticks == 0 &&
           scale && advanced && advanced.Value().current.now.ticks == 0 &&
           runtime.Now().ticks == 0;
}

bool TestSkipDropsFractionalRemainder()
{
    TimeOptions options{};
    options.initial_time_scale = TimeScale{1, 3};
    TimeRuntime runtime(options);

    const auto fractional = runtime.Advance(std::chrono::seconds(1));
    if (!fractional || runtime.CaptureCheckpoint().tick_remainder_numerator == 0)
    {
        return false;
    }

    const auto skipped = runtime.Skip(GameDuration{1});
    const auto after_skip = runtime.CaptureCheckpoint();
    const auto advanced = runtime.Advance(std::chrono::seconds(2));

    return skipped && after_skip.tick_remainder_numerator == 0 && advanced && runtime.Now().ticks == 1;
}

bool TestTimeSkipProducesJumpEvent()
{
    TimeRuntime runtime;
    const auto result = runtime.Skip(GameDuration{90});

    const auto& events = runtime.GetEvents();
    return result && runtime.Now().ticks == 90 && runtime.LastDelta().ticks == 90 && !events.empty() &&
           events.front().kind == TimeEventKind::TimeJumped && result.Value().previous.now.ticks == 0 &&
           result.Value().current.now.ticks == 90 && !result.Value().events.empty() &&
           result.Value().events.front().kind == TimeEventKind::TimeJumped;
}

bool TestNegativeSkipIsRejected()
{
    TimeRuntime runtime;
    const auto result = runtime.Skip(GameDuration{-1});

    return !result && result.GetError().HasCode("time.invalid_skip") && runtime.Now().ticks == 0;
}

bool TestSkipOverflowIsRejected()
{
    TimeRuntime runtime;
    const auto to_max = runtime.Skip(GameDuration{std::numeric_limits<std::int64_t>::max()});
    const auto overflow = runtime.Skip(GameDuration{1});

    return to_max && !overflow && overflow.GetError().HasCode("time.overflow") &&
           runtime.Now().ticks == std::numeric_limits<std::int64_t>::max();
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
    return date.year == 1 && date.month == 1 && date.day == 2 && date.hour == 1 && date.minute == 1 &&
           date.second == 0 && point &&
           point.Value().ticks == runtime.Now().ticks;
}

bool TestCalendarSecondsRoundTrip()
{
    TimeRuntime runtime;
    const auto skip_result = runtime.Skip(GameDuration{(24 * 60 * 60) + (61 * 60) + 42});
    if (!skip_result)
    {
        return false;
    }

    const auto date = runtime.GetSnapshot().calendar;
    const auto point = runtime.ToGameTimePoint(date);
    return date.year == 1 && date.month == 1 && date.day == 2 && date.hour == 1 && date.minute == 1 &&
           date.second == 42 && point && point.Value().ticks == runtime.Now().ticks;
}

bool TestDateValidation()
{
    TimeRuntime runtime;
    const auto invalid_month = runtime.ToGameTimePoint(CalendarDate{1, 13, 1, 0, 0, 0});
    const auto invalid_minute = runtime.ToGameTimePoint(CalendarDate{1, 1, 1, 0, 60, 0});
    const auto invalid_second = runtime.ToGameTimePoint(CalendarDate{1, 1, 1, 0, 0, 60});

    return !invalid_month && invalid_month.GetError().HasCode("time.invalid_date") && !invalid_minute && !invalid_second;
}

bool TestCalendarDateOverflowIsRejected()
{
    TimeRuntime runtime;
    const auto result = runtime.ToGameTimePoint(
        CalendarDate{std::numeric_limits<std::int64_t>::max(), 1, 1, 0, 0, 0});

    return !result && result.GetError().HasCode("time.overflow");
}

bool TestAdvanceOverflowIsRejected()
{
    TimeOptions options{};
    options.game_ticks_per_real_second = std::numeric_limits<std::int64_t>::max();
    options.initial_time_scale = TimeScale{std::numeric_limits<std::int64_t>::max(), 1};
    TimeRuntime runtime(options);

    const auto result = runtime.Advance(std::chrono::seconds(1));

    return !result && result.GetError().HasCode("time.overflow") && runtime.Now().ticks == 0;
}

bool TestScaleDenominatorOverflowIsRejected()
{
    TimeOptions options{};
    options.initial_time_scale = TimeScale{1, std::numeric_limits<std::int64_t>::max()};
    TimeRuntime runtime(options);
    const auto before = runtime.CaptureCheckpoint();

    const auto result = runtime.Advance(std::chrono::microseconds{1});

    return !result && result.GetError().HasCode("time.overflow") && SameCheckpoint(runtime.CaptureCheckpoint(), before);
}

bool TestLongRunStaysDeterministic()
{
    TimeOptions options{};
    options.game_ticks_per_real_second = 120;
    TimeRuntime runtime(options);

    bool ok = true;
    for (int index = 0; index < 1000; ++index)
    {
        ok = runtime.Advance(std::chrono::seconds(1)) && ok;
    }

    return ok && runtime.Now().ticks == 120000 && runtime.LastDelta().ticks == 120;
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

bool TestLargeSkipReportsCrossedPhaseBoundaryWhenFinalPhaseMatches()
{
    TimeRuntime runtime;
    const auto result = runtime.Skip(GameDuration{24 * 60 * 60});
    if (!result || runtime.GetSnapshot().day_phase != DayPhase::Night)
    {
        return false;
    }

    const auto& events = runtime.GetEvents();
    return events.size() == 3 && events[0].kind == TimeEventKind::TimeJumped &&
           events[1].kind == TimeEventKind::DayChanged && events[2].kind == TimeEventKind::DayPhaseChanged;
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

bool TestFractionalRemainderMutationIncrementsRevision()
{
    TimeRuntime runtime;
    const auto advanced = runtime.Advance(std::chrono::seconds(1));
    const auto revision_after_advance = runtime.GetSnapshot().revision;
    const auto fractional = runtime.Advance(std::chrono::microseconds(1));

    return advanced && fractional && advanced.Value().current.last_delta.ticks == 1 &&
           fractional.Value().current.last_delta.ticks == 0 &&
           fractional.Value().current.revision == revision_after_advance + 1 &&
           runtime.CaptureCheckpoint().tick_remainder_numerator != 0;
}

bool TestLastDeltaOnlyRefreshDoesNotIncrementRevision()
{
    TimeRuntime runtime;
    const auto advanced = runtime.Advance(std::chrono::seconds(1));
    if (!advanced)
    {
        return false;
    }

    const auto revision_after_advance = runtime.GetSnapshot().revision;
    const auto checkpoint_after_advance = runtime.CaptureCheckpoint();
    const auto idle = runtime.Advance(std::chrono::microseconds(0));

    return idle && idle.Value().current.last_delta.ticks == 0 &&
           idle.Value().current.revision == revision_after_advance &&
           runtime.CaptureCheckpoint().tick_remainder_numerator == checkpoint_after_advance.tick_remainder_numerator;
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

    TimeOptions duplicate_boundary{};
    duplicate_boundary.phase_boundaries = {
        PhaseBoundary{60, DayPhase::Dawn},
        PhaseBoundary{60, DayPhase::Day},
    };

    TimeOptions overflowing_calendar{};
    overflowing_calendar.calendar.days_per_month = std::numeric_limits<std::uint32_t>::max();
    overflowing_calendar.calendar.months_per_year = std::numeric_limits<std::uint32_t>::max();

    TimeOptions short_day_with_defaults{};
    short_day_with_defaults.calendar.hours_per_day = 12;

    const auto rate_result = CreateTimeServices(invalid_rate);
    const auto boundary_result = CreateTimeServices(invalid_boundary);
    const auto duplicate_result = ValidateTimeOptions(duplicate_boundary);
    const auto calendar_result = ValidateTimeOptions(overflowing_calendar);
    const auto default_boundary_result = CreateTimeServices(short_day_with_defaults);
    return !rate_result && rate_result.GetError().HasCode("time.invalid_options") && !boundary_result &&
           boundary_result.GetError().HasCode("time.invalid_phase_boundary") && !duplicate_result &&
           duplicate_result.GetError().HasCode("time.duplicate_phase_boundary") && !calendar_result &&
           calendar_result.GetError().HasCode("time.overflow") && !default_boundary_result &&
           default_boundary_result.GetError().HasCode("time.invalid_phase_boundary");
}

bool TestCheckpointCapturesCanonicalPersistentState()
{
    TimeOptions options{};
    options.game_ticks_per_real_second = 7;
    options.initial_time_scale = TimeScale{10, 4};
    options.phase_boundaries = {
        PhaseBoundary{120, DayPhase::Day},
        PhaseBoundary{0, DayPhase::Night},
    };
    TimeRuntime runtime(options);
    const auto advanced = runtime.Advance(std::chrono::milliseconds{100});
    if (!advanced)
    {
        return false;
    }

    const auto checkpoint = runtime.CaptureCheckpoint();
    return checkpoint.time_scale == TimeScale{5, 2} && checkpoint.tick_remainder_numerator != 0 &&
           checkpoint.game_ticks_per_real_second == 7 && checkpoint.calendar.hours_per_day == 24 &&
           checkpoint.phase_boundaries.size() == 2 && checkpoint.phase_boundaries[0].start_minute == 0 &&
           checkpoint.phase_boundaries[1].start_minute == 120;
}

bool TestCheckpointContinuationPreservesFractionalRemainder()
{
    TimeOptions options{};
    options.initial_time_scale = TimeScale{1, 3};

    TimeRuntime control(options);
    const auto seeded = control.Advance(std::chrono::seconds{1});
    if (!seeded)
    {
        return false;
    }
    const auto checkpoint = control.CaptureCheckpoint();
    if (checkpoint.tick_remainder_numerator == 0)
    {
        return false;
    }

    TimeRuntime restored(options);
    const auto restore = restored.RestoreCheckpoint(checkpoint);
    const auto control_next = control.Advance(std::chrono::seconds{2});
    const auto restored_next = restored.Advance(std::chrono::seconds{2});

    return restore && control_next && restored_next && control.GetSnapshot() == restored.GetSnapshot() &&
           SameCheckpoint(control.CaptureCheckpoint(), restored.CaptureCheckpoint()) &&
           control.GetEvents() == restored.GetEvents();
}

bool TestInvalidCheckpointPreservesLiveClock()
{
    TimeRuntime runtime;
    const auto seeded = runtime.Skip(GameDuration{90});
    if (!seeded)
    {
        return false;
    }

    const auto before_snapshot = runtime.GetSnapshot();
    const auto before_checkpoint = runtime.CaptureCheckpoint();
    const auto before_events = runtime.GetEvents();

    const auto rejects_without_mutation = [&](TimeCheckpoint invalid) {
        const auto restore = runtime.RestoreCheckpoint(invalid);
        return !restore && restore.GetError().HasCode("time.invalid_checkpoint") &&
               runtime.GetSnapshot() == before_snapshot &&
               SameCheckpoint(runtime.CaptureCheckpoint(), before_checkpoint) && runtime.GetEvents() == before_events;
    };

    auto invalid_scale = before_checkpoint;
    invalid_scale.time_scale = TimeScale{2, 2};
    auto invalid_time = before_checkpoint;
    invalid_time.now = GameTimePoint{-1};
    auto invalid_negative_remainder = before_checkpoint;
    invalid_negative_remainder.tick_remainder_numerator = -1;
    auto invalid_large_remainder = before_checkpoint;
    invalid_large_remainder.tick_remainder_numerator = invalid_large_remainder.time_scale.denominator * 1000000;
    auto unrepresentable_remainder_scale = before_checkpoint;
    unrepresentable_remainder_scale.time_scale = TimeScale{1, std::numeric_limits<std::int64_t>::max()};
    unrepresentable_remainder_scale.tick_remainder_numerator = 1;

    return rejects_without_mutation(invalid_scale) && rejects_without_mutation(invalid_time) &&
           rejects_without_mutation(invalid_negative_remainder) && rejects_without_mutation(invalid_large_remainder) &&
           rejects_without_mutation(unrepresentable_remainder_scale);
}

bool TestIncompatibleCheckpointPreservesLiveClock()
{
    TimeRuntime source;
    const auto source_advance = source.Skip(GameDuration{20});
    if (!source_advance)
    {
        return false;
    }
    const auto checkpoint = source.CaptureCheckpoint();

    TimeOptions destination_options{};
    destination_options.game_ticks_per_real_second = 2;
    TimeRuntime destination(destination_options);
    const auto destination_seed = destination.Skip(GameDuration{7});
    if (!destination_seed)
    {
        return false;
    }
    const auto before_snapshot = destination.GetSnapshot();
    const auto before_checkpoint = destination.CaptureCheckpoint();
    const auto before_events = destination.GetEvents();

    const auto rejects_without_mutation = [&](TimeCheckpoint incompatible) {
        const auto restore = destination.RestoreCheckpoint(incompatible);
        return !restore && restore.GetError().HasCode("time.incompatible_checkpoint") &&
               destination.GetSnapshot() == before_snapshot &&
               SameCheckpoint(destination.CaptureCheckpoint(), before_checkpoint) &&
               destination.GetEvents() == before_events;
    };

    auto incompatible_rate = checkpoint;
    auto incompatible_calendar = destination.CaptureCheckpoint();
    incompatible_calendar.calendar.days_per_month += 1;
    auto incompatible_phases = destination.CaptureCheckpoint();
    incompatible_phases.phase_boundaries[0].phase = DayPhase::Day;
    auto invalid_phase_enum = destination.CaptureCheckpoint();
    invalid_phase_enum.phase_boundaries[0].phase = static_cast<DayPhase>(99);

    return rejects_without_mutation(incompatible_rate) && rejects_without_mutation(incompatible_calendar) &&
           rejects_without_mutation(incompatible_phases) && rejects_without_mutation(invalid_phase_enum);
}

bool TestRestoreAllocationFailurePreservesLiveClock()
{
    TimeOptions options{};
    options.initial_time_scale = TimeScale{1, 3};

    TimeRuntime source(options);
    const auto source_advance = source.Advance(std::chrono::seconds{1});
    if (!source_advance)
    {
        return false;
    }
    const auto target = source.CaptureCheckpoint();

    TimeRuntime destination(options);
    const auto destination_seed = destination.Skip(GameDuration{11});
    if (!destination_seed)
    {
        return false;
    }
    const auto before_snapshot = destination.GetSnapshot();
    const auto before_checkpoint = destination.CaptureCheckpoint();
    const auto before_events = destination.GetEvents();

    destination.FailNextAllocationForTesting();
    bool saw_bad_alloc = false;
    try
    {
        (void)destination.RestoreCheckpoint(target);
    }
    catch (const std::bad_alloc&)
    {
        saw_bad_alloc = true;
    }

    if (!saw_bad_alloc || destination.GetSnapshot() != before_snapshot ||
        !SameCheckpoint(destination.CaptureCheckpoint(), before_checkpoint) || destination.GetEvents() != before_events)
    {
        return false;
    }

    const auto retry = destination.RestoreCheckpoint(target);
    return retry && SameCheckpoint(destination.CaptureCheckpoint(), target);
}

bool TestRestoreClearsTransientStateAndRestoresPause()
{
    TimeRuntime source;
    const auto source_advance = source.Skip(GameDuration{10});
    const auto pause = source.Pause();
    if (!source_advance || !pause)
    {
        return false;
    }
    const auto checkpoint = source.CaptureCheckpoint();

    TimeRuntime destination;
    const auto destination_advance = destination.Skip(GameDuration{99});
    const auto restore = destination.RestoreCheckpoint(checkpoint);
    const auto blocked = destination.Advance(std::chrono::seconds{1});

    return destination_advance && restore && blocked && destination.GetSnapshot().paused &&
           destination.Now() == checkpoint.now && destination.LastDelta() == GameDuration{} &&
           destination.GetEvents().empty();
}

bool TestRevisionExhaustionPreservesState()
{
    TimeRuntime runtime;
    auto checkpoint = runtime.CaptureCheckpoint();
    checkpoint.revision = std::numeric_limits<std::uint64_t>::max();
    const auto restore = runtime.RestoreCheckpoint(checkpoint);
    if (!restore)
    {
        return false;
    }

    const auto before_snapshot = runtime.GetSnapshot();
    const auto before_checkpoint = runtime.CaptureCheckpoint();
    const auto fractional = runtime.Advance(std::chrono::microseconds{1});
    if (fractional || !fractional.GetError().HasCode("time.revision_exhausted") ||
        runtime.GetSnapshot() != before_snapshot || !SameCheckpoint(runtime.CaptureCheckpoint(), before_checkpoint) ||
        !runtime.GetEvents().empty())
    {
        return false;
    }

    const auto failed = runtime.Skip(GameDuration{1});
    return !failed && failed.GetError().HasCode("time.revision_exhausted") &&
           runtime.GetSnapshot() == before_snapshot && SameCheckpoint(runtime.CaptureCheckpoint(), before_checkpoint) &&
           runtime.GetEvents().empty();
}

bool TestMutationAllocationFailurePreservesObservableState()
{
    TimeRuntime runtime;
    const auto seeded = runtime.Skip(GameDuration{4});
    if (!seeded)
    {
        return false;
    }
    const auto before_snapshot = runtime.GetSnapshot();
    const auto before_checkpoint = runtime.CaptureCheckpoint();
    const auto before_events = runtime.GetEvents();

    runtime.FailNextAllocationForTesting();
    bool saw_bad_alloc = false;
    try
    {
        (void)runtime.Advance(std::chrono::seconds{1});
    }
    catch (const std::bad_alloc&)
    {
        saw_bad_alloc = true;
    }

    return saw_bad_alloc && runtime.GetSnapshot() == before_snapshot &&
           SameCheckpoint(runtime.CaptureCheckpoint(), before_checkpoint) && runtime.GetEvents() == before_events;
}


bool TestOverflowFailuresPreserveObservableState()
{
    TimeRuntime runtime;
    const auto to_max = runtime.Skip(GameDuration{std::numeric_limits<std::int64_t>::max()});
    if (!to_max || runtime.GetEvents().empty())
    {
        return false;
    }
    const auto before_snapshot = runtime.GetSnapshot();
    const auto before_delta = runtime.LastDelta();
    const auto before_events = runtime.GetEvents();
    const auto overflow = runtime.Skip(GameDuration{1});
    return !overflow && overflow.GetError().HasCode("time.overflow") && runtime.GetSnapshot() == before_snapshot &&
           runtime.LastDelta() == before_delta && runtime.GetEvents() == before_events;
}

bool TestIdempotentMutationsDoNotClearEvents()
{
    TimeRuntime runtime;
    const auto pause = runtime.Pause();
    if (!pause || runtime.GetEvents().empty())
    {
        return false;
    }
    const auto pause_events = runtime.GetEvents();
    const auto second_pause = runtime.Pause();
    if (!second_pause || runtime.GetEvents() != pause_events)
    {
        return false;
    }
    const auto resume = runtime.Resume();
    if (!resume || runtime.GetEvents().empty() || runtime.GetEvents().front().kind != TimeEventKind::Resumed)
    {
        return false;
    }
    const auto resume_events = runtime.GetEvents();
    const auto second_resume = runtime.Resume();
    return second_resume && runtime.GetEvents() == resume_events;
}

bool TestInvalidPhaseAndDirectInvalidConstructionAreRejected()
{
    TimeOptions invalid_phase{};
    invalid_phase.phase_boundaries = {
        PhaseBoundary{0, static_cast<DayPhase>(99)},
    };
    const auto factory_result = CreateTimeServices(invalid_phase);
    if (factory_result || !factory_result.GetError().HasCode("time.invalid_phase"))
    {
        return false;
    }

    TimeOptions invalid_calendar{};
    invalid_calendar.calendar.hours_per_day = 0;
    try
    {
        TimeRuntime runtime(invalid_calendar);
        (void)runtime;
        return false;
    }
    catch (const std::invalid_argument&)
    {
        return true;
    }
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
    if (!TestNegativeAdvancePreservesObservableState())
    {
        return 35;
    }
    if (!TestRationalAccumulatorAvoidsFloatingDrift())
    {
        return 29;
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
    if (!TestTimeScaleIsNormalized())
    {
        return 17;
    }
    if (!TestTimeScaleChangeDropsFractionalRemainder())
    {
        return 31;
    }
    if (!TestSkipDropsFractionalRemainder())
    {
        return 36;
    }
    if (!TestTimeSkipProducesJumpEvent())
    {
        return 8;
    }
    if (!TestNegativeSkipIsRejected())
    {
        return 18;
    }
    if (!TestSkipOverflowIsRejected())
    {
        return 19;
    }
    if (!TestCalendarConversion())
    {
        return 9;
    }
    if (!TestCalendarSecondsRoundTrip())
    {
        return 10;
    }
    if (!TestDateValidation())
    {
        return 16;
    }
    if (!TestCalendarDateOverflowIsRejected())
    {
        return 20;
    }
    if (!TestAdvanceOverflowIsRejected())
    {
        return 21;
    }
    if (!TestScaleDenominatorOverflowIsRejected())
    {
        return 37;
    }
    if (!TestLongRunStaysDeterministic())
    {
        return 22;
    }
    if (!TestDayAndPhaseTransitions())
    {
        return 11;
    }
    if (!TestLargeSkipReportsCrossedPhaseBoundaryWhenFinalPhaseMatches())
    {
        return 38;
    }
    if (!TestRevisionIncrementsOnlyOnChanges())
    {
        return 12;
    }
    if (!TestFractionalRemainderMutationIncrementsRevision())
    {
        return 47;
    }
    if (!TestLastDeltaOnlyRefreshDoesNotIncrementRevision())
    {
        return 30;
    }
    if (!TestFactoryCreatesSplitServices())
    {
        return 13;
    }
    if (!TestFactoryRejectsInvalidOptions())
    {
        return 14;
    }
    if (!TestCheckpointCapturesCanonicalPersistentState())
    {
        return 39;
    }
    if (!TestCheckpointContinuationPreservesFractionalRemainder())
    {
        return 40;
    }
    if (!TestInvalidCheckpointPreservesLiveClock())
    {
        return 41;
    }
    if (!TestIncompatibleCheckpointPreservesLiveClock())
    {
        return 42;
    }
    if (!TestRestoreAllocationFailurePreservesLiveClock())
    {
        return 43;
    }
    if (!TestRestoreClearsTransientStateAndRestoresPause())
    {
        return 44;
    }
    if (!TestRevisionExhaustionPreservesState())
    {
        return 45;
    }
    if (!TestMutationAllocationFailurePreservesObservableState())
    {
        return 46;
    }
    if (!TestOverflowFailuresPreserveObservableState())
    {
        return 32;
    }
    if (!TestIdempotentMutationsDoNotClearEvents())
    {
        return 33;
    }
    if (!TestInvalidPhaseAndDirectInvalidConstructionAreRejected())
    {
        return 34;
    }
    if (!TestConfigurablePhaseBoundaries())
    {
        return 15;
    }

    return 0;
}
