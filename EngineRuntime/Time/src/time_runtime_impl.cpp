#include "time_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace epidemic::runtime
{
namespace
{
constexpr std::int64_t kSecondsPerMinute = 60;
constexpr std::int64_t kSecondsPerHour = 60 * kSecondsPerMinute;

[[nodiscard]] foundation::Error MakeTimeError(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}

template <typename TValue>
[[nodiscard]] foundation::Result<TValue> TimeFailure(std::string_view code, std::string_view message)
{
    return foundation::Result<TValue>::Failure(MakeTimeError(code, message));
}

[[nodiscard]] std::int64_t MinutesPerDay(const CalendarDefinition& calendar)
{
    return static_cast<std::int64_t>(calendar.hours_per_day) * 60;
}

[[nodiscard]] std::int64_t DaysPerYear(const CalendarDefinition& calendar)
{
    return static_cast<std::int64_t>(calendar.days_per_month) * calendar.months_per_year;
}

[[nodiscard]] std::int64_t SecondsPerDay(const CalendarDefinition& calendar)
{
    return MinutesPerDay(calendar) * kSecondsPerMinute;
}

[[nodiscard]] TimeScale NormalizeTimeScale(TimeScale scale)
{
    const auto divisor = std::gcd(scale.numerator, scale.denominator);
    return TimeScale{scale.numerator / divisor, scale.denominator / divisor};
}

[[nodiscard]] bool IsValidTimeScale(TimeScale scale)
{
    return scale.numerator > 0 && scale.denominator > 0;
}

[[nodiscard]] std::optional<std::int64_t> CheckedAddInt(std::int64_t left, std::int64_t right)
{
    if (right > 0 && left > std::numeric_limits<std::int64_t>::max() - right)
    {
        return std::nullopt;
    }
    if (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)
    {
        return std::nullopt;
    }
    return left + right;
}

[[nodiscard]] std::optional<std::int64_t> CheckedMultiplyNonNegative(std::int64_t left, std::int64_t right)
{
    if (left < 0 || right < 0)
    {
        return std::nullopt;
    }
    if (left != 0 && right > std::numeric_limits<std::int64_t>::max() / left)
    {
        return std::nullopt;
    }
    return left * right;
}

[[nodiscard]] std::optional<std::int64_t> ScaledTickDenominator(TimeScale scale)
{
    return CheckedMultiplyNonNegative(1000000, scale.denominator);
}

[[nodiscard]] std::optional<std::int64_t> ScaledTickNumerator(std::int64_t real_microseconds, std::int64_t game_ticks_per_real_second,
                                                             TimeScale scale)
{
    const auto real_ticks = CheckedMultiplyNonNegative(real_microseconds, game_ticks_per_real_second);
    return real_ticks ? CheckedMultiplyNonNegative(*real_ticks, scale.numerator) : std::nullopt;
}

[[nodiscard]] std::vector<PhaseBoundary> DefaultPhaseBoundaries()
{
    return {
        PhaseBoundary{0, DayPhase::Night},
        PhaseBoundary{5 * 60, DayPhase::Dawn},
        PhaseBoundary{8 * 60, DayPhase::Day},
        PhaseBoundary{18 * 60, DayPhase::Dusk},
        PhaseBoundary{21 * 60, DayPhase::Night},
    };
}

[[nodiscard]] CalendarDate ToCalendarDateForOptions(const TimeOptions& options, GameTimePoint time)
{
    const auto clamped_ticks = std::max<std::int64_t>(0, time.ticks);
    const auto seconds_per_day = SecondsPerDay(options.calendar);
    const auto total_days = clamped_ticks / seconds_per_day;
    const auto second_of_day = clamped_ticks % seconds_per_day;
    const auto days_per_year = DaysPerYear(options.calendar);
    const auto day_in_year = total_days % days_per_year;

    CalendarDate date{};
    date.year = 1 + (total_days / days_per_year);
    date.month = 1 + static_cast<std::uint32_t>(day_in_year / options.calendar.days_per_month);
    date.day = 1 + static_cast<std::uint32_t>(day_in_year % options.calendar.days_per_month);
    date.hour = static_cast<std::uint32_t>(second_of_day / kSecondsPerHour);
    date.minute = static_cast<std::uint32_t>((second_of_day % kSecondsPerHour) / kSecondsPerMinute);
    date.second = static_cast<std::uint32_t>(second_of_day % kSecondsPerMinute);
    return date;
}

[[nodiscard]] DayPhase DetermineDayPhaseForOptions(const TimeOptions& options, const CalendarDate& date)
{
    const auto minute_of_day = (date.hour * 60) + date.minute;
    DayPhase phase = DayPhase::Night;
    for (const auto& boundary : options.phase_boundaries)
    {
        if (boundary.start_minute <= minute_of_day)
        {
            phase = boundary.phase;
        }
    }

    return phase;
}
} // namespace

TimeRuntime::TimeRuntime(TimeOptions options) : options_(std::move(options))
{
    if (IsValidTimeScale(options_.initial_time_scale))
    {
        time_scale_ = NormalizeTimeScale(options_.initial_time_scale);
    }
    else
    {
        time_scale_ = options_.initial_time_scale;
    }

    if (options_.phase_boundaries.empty())
    {
        options_.phase_boundaries = DefaultPhaseBoundaries();
    }

    std::sort(options_.phase_boundaries.begin(),
              options_.phase_boundaries.end(),
              [](const PhaseBoundary& left, const PhaseBoundary& right) {
                  return left.start_minute < right.start_minute;
              });

    const auto valid = ValidateOptions();
    if (!valid)
    {
        throw std::invalid_argument(valid.GetError().code + ": " + valid.GetError().message);
    }
    RefreshSnapshot(false);
}

GameTimePoint TimeRuntime::Now() const
{
    return now_;
}

GameDuration TimeRuntime::LastDelta() const
{
    return last_delta_;
}

TimeSnapshot TimeRuntime::GetSnapshot() const
{
    return snapshot_;
}

foundation::Result<TimeAdvanceResult> TimeRuntime::Advance(std::chrono::microseconds real_delta)
{
    const auto options_result = ValidateOptions();
    if (!options_result)
    {
        return foundation::Result<TimeAdvanceResult>::Failure(options_result.GetError());
    }

    if (real_delta.count() < 0)
    {
        return foundation::Result<TimeAdvanceResult>::Failure(
            MakeTimeError("time.invalid_delta", "real delta must not be negative"));
    }

    const TimeSnapshot previous = snapshot_;
    TimeMutableState candidate{options_, now_, last_delta_, time_scale_, paused_, state_, snapshot_, {}, tick_remainder_numerator_, revision_};

    if (paused_ || real_delta.count() == 0)
    {
        candidate.last_delta = GameDuration{};
        const auto snapshot = BuildSnapshot(candidate, false);
        if (!snapshot)
        {
            return foundation::Result<TimeAdvanceResult>::Failure(snapshot.GetError());
        }
        candidate.snapshot = snapshot.Value();
        Commit(std::move(candidate));
        return foundation::Result<TimeAdvanceResult>::Success(MakeResult(previous));
    }

    const auto denominator = ScaledTickDenominator(time_scale_);
    const auto scaled_numerator = ScaledTickNumerator(real_delta.count(), options_.game_ticks_per_real_second, time_scale_);
    const auto candidate_numerator =
        (denominator && scaled_numerator) ? CheckedAddInt(*scaled_numerator, tick_remainder_numerator_) : std::nullopt;
    if (!denominator || !scaled_numerator || !candidate_numerator)
    {
        return foundation::Result<TimeAdvanceResult>::Failure(
            MakeTimeError("time.overflow", "scaled time delta overflows game duration"));
    }

    const auto whole_ticks = *candidate_numerator / *denominator;
    const auto remaining_remainder = *candidate_numerator % *denominator;
    candidate.last_delta = GameDuration{whole_ticks};
    candidate.tick_remainder_numerator = remaining_remainder;

    if (whole_ticks > 0)
    {
        if (auto revision = PreflightRevision(true); !revision)
        {
            return foundation::Result<TimeAdvanceResult>::Failure(revision.GetError());
        }
        const auto next = CheckedAdd(now_, candidate.last_delta);
        if (!next)
        {
            return foundation::Result<TimeAdvanceResult>::Failure(
                MakeTimeError("time.overflow", "advancing time overflows game time"));
        }
        candidate.now = *next;
        candidate.state = TimeRuntimeState::Running;
        candidate.events.reserve(3);
        const auto snapshot = BuildSnapshot(candidate, true);
        if (!snapshot)
        {
            return foundation::Result<TimeAdvanceResult>::Failure(snapshot.GetError());
        }
        candidate.snapshot = snapshot.Value();
        if (auto event = PushEvent(candidate, TimeEventKind::TimeAdvanced); !event)
        {
            return foundation::Result<TimeAdvanceResult>::Failure(event.GetError());
        }
        if (auto boundaries = AppendBoundaryEvents(candidate, previous); !boundaries)
        {
            return foundation::Result<TimeAdvanceResult>::Failure(boundaries.GetError());
        }
    }
    else
    {
        const auto snapshot = BuildSnapshot(candidate, false);
        if (!snapshot)
        {
            return foundation::Result<TimeAdvanceResult>::Failure(snapshot.GetError());
        }
        candidate.snapshot = snapshot.Value();
    }

    Commit(std::move(candidate));
    return foundation::Result<TimeAdvanceResult>::Success(MakeResult(previous));
}

foundation::Result<void> TimeRuntime::Pause()
{
    const auto options_result = ValidateOptions();
    if (!options_result)
    {
        return options_result;
    }
    if (paused_)
    {
        return foundation::Result<void>::Success();
    }
    if (auto revision = PreflightRevision(true); !revision)
    {
        return revision;
    }

    TimeMutableState candidate{options_, now_, GameDuration{}, time_scale_, true, TimeRuntimeState::Paused, snapshot_, {}, tick_remainder_numerator_, revision_};
    candidate.events.reserve(1);
    const auto snapshot = BuildSnapshot(candidate, true);
    if (!snapshot)
    {
        return foundation::Result<void>::Failure(snapshot.GetError());
    }
    candidate.snapshot = snapshot.Value();
    if (auto event = PushEvent(candidate, TimeEventKind::Paused); !event)
    {
        return event;
    }
    Commit(std::move(candidate));
    return foundation::Result<void>::Success();
}

foundation::Result<void> TimeRuntime::Resume()
{
    const auto options_result = ValidateOptions();
    if (!options_result)
    {
        return options_result;
    }
    if (!paused_)
    {
        return foundation::Result<void>::Success();
    }
    if (auto revision = PreflightRevision(true); !revision)
    {
        return revision;
    }

    TimeMutableState candidate{options_, now_, last_delta_, time_scale_, false, TimeRuntimeState::Running, snapshot_, {}, tick_remainder_numerator_, revision_};
    candidate.events.reserve(1);
    const auto snapshot = BuildSnapshot(candidate, true);
    if (!snapshot)
    {
        return foundation::Result<void>::Failure(snapshot.GetError());
    }
    candidate.snapshot = snapshot.Value();
    if (auto event = PushEvent(candidate, TimeEventKind::Resumed); !event)
    {
        return event;
    }
    Commit(std::move(candidate));
    return foundation::Result<void>::Success();
}

foundation::Result<void> TimeRuntime::SetTimeScale(TimeScale scale)
{
    if (!IsValidTimeScale(scale))
    {
        return foundation::Result<void>::Failure(
            MakeTimeError("time.invalid_scale", "time scale numerator and denominator must be positive"));
    }

    scale = NormalizeTimeScale(scale);
    if (time_scale_ == scale)
    {
        return foundation::Result<void>::Success();
    }
    if (auto revision = PreflightRevision(true); !revision)
    {
        return revision;
    }

    TimeMutableState candidate{options_, now_, last_delta_, scale, paused_, TimeRuntimeState::TimeScaleChanged, snapshot_, {}, 0, revision_};
    candidate.events.reserve(1);
    const auto snapshot = BuildSnapshot(candidate, true);
    if (!snapshot)
    {
        return foundation::Result<void>::Failure(snapshot.GetError());
    }
    candidate.snapshot = snapshot.Value();
    if (auto event = PushEvent(candidate, TimeEventKind::TimeScaleChanged); !event)
    {
        return event;
    }
    Commit(std::move(candidate));
    return foundation::Result<void>::Success();
}

foundation::Result<TimeAdvanceResult> TimeRuntime::Skip(GameDuration duration)
{
    const auto options_result = ValidateOptions();
    if (!options_result)
    {
        return foundation::Result<TimeAdvanceResult>::Failure(options_result.GetError());
    }
    if (duration.ticks < 0)
    {
        return foundation::Result<TimeAdvanceResult>::Failure(
            MakeTimeError("time.invalid_skip", "time skip duration must not be negative"));
    }

    const TimeSnapshot previous = snapshot_;
    TimeMutableState candidate{options_, now_, GameDuration{}, time_scale_, paused_, state_, snapshot_, {}, tick_remainder_numerator_, revision_};
    if (duration.ticks == 0)
    {
        const auto snapshot = BuildSnapshot(candidate, false);
        if (!snapshot)
        {
            return foundation::Result<TimeAdvanceResult>::Failure(snapshot.GetError());
        }
        candidate.snapshot = snapshot.Value();
        Commit(std::move(candidate));
        return foundation::Result<TimeAdvanceResult>::Success(MakeResult(previous));
    }

    if (auto revision = PreflightRevision(true); !revision)
    {
        return foundation::Result<TimeAdvanceResult>::Failure(revision.GetError());
    }
    const auto next = CheckedAdd(now_, duration);
    if (!next)
    {
        return foundation::Result<TimeAdvanceResult>::Failure(MakeTimeError("time.overflow", "skipping time overflows game time"));
    }

    candidate.now = *next;
    candidate.last_delta = duration;
    candidate.tick_remainder_numerator = 0;
    candidate.state = TimeRuntimeState::TimeJumped;
    candidate.events.reserve(3);
    const auto snapshot = BuildSnapshot(candidate, true);
    if (!snapshot)
    {
        return foundation::Result<TimeAdvanceResult>::Failure(snapshot.GetError());
    }
    candidate.snapshot = snapshot.Value();
    if (auto event = PushEvent(candidate, TimeEventKind::TimeJumped); !event)
    {
        return foundation::Result<TimeAdvanceResult>::Failure(event.GetError());
    }
    if (auto boundaries = AppendBoundaryEvents(candidate, previous); !boundaries)
    {
        return foundation::Result<TimeAdvanceResult>::Failure(boundaries.GetError());
    }
    Commit(std::move(candidate));
    return foundation::Result<TimeAdvanceResult>::Success(MakeResult(previous));
}

const std::vector<TimeEvent>& TimeRuntime::GetEvents() const
{
    return events_;
}

foundation::Result<GameTimePoint> TimeRuntime::ToGameTimePoint(CalendarDate date) const
{
    const auto valid = ValidateDate(date);
    if (!valid)
    {
        return foundation::Result<GameTimePoint>::Failure(valid.GetError());
    }

    const auto years = date.year - 1;
    const auto months = static_cast<std::int64_t>(date.month - 1);
    const auto days = static_cast<std::int64_t>(date.day - 1);
    const auto days_per_year = CheckedMultiplyNonNegative(
        static_cast<std::int64_t>(options_.calendar.days_per_month),
        static_cast<std::int64_t>(options_.calendar.months_per_year));
    const auto seconds_per_day = CheckedMultiplyNonNegative(
        static_cast<std::int64_t>(options_.calendar.hours_per_day), kSecondsPerHour);
    const auto years_as_days = days_per_year ? CheckedMultiplyNonNegative(years, *days_per_year) : std::nullopt;
    const auto months_as_days = CheckedMultiplyNonNegative(months,
                                                           static_cast<std::int64_t>(options_.calendar.days_per_month));
    const auto total_days = (years_as_days && months_as_days) ? CheckedAddInt(*years_as_days, *months_as_days)
                                                              : std::nullopt;
    const auto total_days_with_day = total_days ? CheckedAddInt(*total_days, days) : std::nullopt;
    const auto day_ticks =
        (total_days_with_day && seconds_per_day) ? CheckedMultiplyNonNegative(*total_days_with_day, *seconds_per_day)
                                                 : std::nullopt;
    const auto hour_ticks = CheckedMultiplyNonNegative(static_cast<std::int64_t>(date.hour), kSecondsPerHour);
    const auto minute_ticks = CheckedMultiplyNonNegative(static_cast<std::int64_t>(date.minute), kSecondsPerMinute);
    const auto with_hours = (day_ticks && hour_ticks) ? CheckedAddInt(*day_ticks, *hour_ticks) : std::nullopt;
    const auto with_minutes = (with_hours && minute_ticks) ? CheckedAddInt(*with_hours, *minute_ticks) : std::nullopt;
    const auto ticks = with_minutes ? CheckedAddInt(*with_minutes, static_cast<std::int64_t>(date.second))
                                    : std::nullopt;
    if (!ticks)
    {
        return foundation::Result<GameTimePoint>::Failure(
            MakeTimeError("time.overflow", "calendar date overflows game time"));
    }
    return foundation::Result<GameTimePoint>::Success(GameTimePoint{*ticks});
}

CalendarDate TimeRuntime::ToCalendarDate(GameTimePoint time) const
{
    return ToCalendarDateForOptions(options_, time);
}

foundation::Result<void> TimeRuntime::ValidateOptions() const
{
    if (options_.game_ticks_per_real_second <= 0)
    {
        return foundation::Result<void>::Failure(
            MakeTimeError("time.invalid_options", "game ticks per real second must be positive"));
    }

    if (!IsValidTimeScale(time_scale_))
    {
        return foundation::Result<void>::Failure(
            MakeTimeError("time.invalid_scale", "initial time scale numerator and denominator must be positive"));
    }

    if (options_.calendar.hours_per_day == 0 || options_.calendar.days_per_month == 0 ||
        options_.calendar.months_per_year == 0)
    {
        return foundation::Result<void>::Failure(
            MakeTimeError("time.invalid_calendar", "calendar units must be positive"));
    }

    if (options_.calendar.hours_per_day > (std::numeric_limits<std::uint32_t>::max() / 60))
    {
        return foundation::Result<void>::Failure(
            MakeTimeError("time.overflow", "calendar day length overflows phase boundary validation"));
    }

    const auto seconds_per_day =
        CheckedMultiplyNonNegative(static_cast<std::int64_t>(options_.calendar.hours_per_day), kSecondsPerHour);
    const auto days_per_year = CheckedMultiplyNonNegative(
        static_cast<std::int64_t>(options_.calendar.days_per_month),
        static_cast<std::int64_t>(options_.calendar.months_per_year));
    if (!seconds_per_day || !days_per_year)
    {
        return foundation::Result<void>::Failure(
            MakeTimeError("time.overflow", "calendar definition overflows time conversion"));
    }

    const std::uint32_t minutes_per_day = options_.calendar.hours_per_day * 60;
    for (std::size_t index = 0; index < options_.phase_boundaries.size(); ++index)
    {
        const auto& boundary = options_.phase_boundaries[index];
        if (!IsValidDayPhase(boundary.phase))
        {
            return foundation::Result<void>::Failure(
                MakeTimeError("time.invalid_phase", "day phase boundary phase is outside the DayPhase enum domain"));
        }
        if (boundary.start_minute >= minutes_per_day)
        {
            return foundation::Result<void>::Failure(
                MakeTimeError("time.invalid_phase_boundary", "day phase boundary is outside the configured day"));
        }

        if (index > 0 && options_.phase_boundaries[index - 1].start_minute == boundary.start_minute)
        {
            return foundation::Result<void>::Failure(
                MakeTimeError("time.duplicate_phase_boundary", "day phase boundaries must have unique start minutes"));
        }
    }

    return foundation::Result<void>::Success();
}

foundation::Result<void> TimeRuntime::ValidateDate(CalendarDate date) const
{
    const auto options_result = ValidateOptions();
    if (!options_result)
    {
        return foundation::Result<void>::Failure(options_result.GetError());
    }

    if (date.year < 1 || date.month == 0 || date.month > options_.calendar.months_per_year || date.day == 0 ||
        date.day > options_.calendar.days_per_month || date.hour >= options_.calendar.hours_per_day ||
        date.minute >= 60 || date.second >= 60)
    {
        return foundation::Result<void>::Failure(
            MakeTimeError("time.invalid_date", "calendar date is outside the configured calendar"));
    }

    return foundation::Result<void>::Success();
}

DayPhase TimeRuntime::DetermineDayPhase(const CalendarDate& date) const
{
    return DetermineDayPhaseForOptions(options_, date);
}

TimeAdvanceResult TimeRuntime::MakeResult(TimeSnapshot previous) const
{
    return TimeAdvanceResult{previous, snapshot_, events_};
}

foundation::Result<TimeSnapshot> TimeRuntime::BuildSnapshot(const TimeMutableState& state, bool changed) const
{
    if (changed && state.revision == std::numeric_limits<std::uint64_t>::max())
    {
        return TimeFailure<TimeSnapshot>("time.revision_exhausted", "time snapshot revision is exhausted");
    }
    TimeSnapshot snapshot{};
    snapshot.now = state.now;
    snapshot.last_delta = state.last_delta;
    snapshot.time_scale = state.time_scale;
    snapshot.paused = state.paused;
    snapshot.calendar = ToCalendarDateForOptions(state.options, state.now);
    snapshot.day_phase = DetermineDayPhaseForOptions(state.options, snapshot.calendar);
    snapshot.revision = changed ? NextRevision(state.revision) : state.revision;
    return foundation::Result<TimeSnapshot>::Success(snapshot);
}

foundation::Result<void> TimeRuntime::PreflightRevision(bool changed) const
{
    if (changed && revision_ == std::numeric_limits<std::uint64_t>::max())
    {
        return foundation::Result<void>::Failure(MakeTimeError("time.revision_exhausted", "time snapshot revision is exhausted"));
    }
    return foundation::Result<void>::Success();
}

bool TimeRuntime::IsValidDayPhase(DayPhase phase) noexcept
{
    switch (phase)
    {
    case DayPhase::Dawn:
    case DayPhase::Day:
    case DayPhase::Dusk:
    case DayPhase::Night:
        return true;
    }
    return false;
}

std::uint64_t TimeRuntime::NextRevision(std::uint64_t current) noexcept
{
    return current + 1;
}

foundation::Result<void> TimeRuntime::PushEvent(TimeMutableState& candidate, TimeEventKind kind)
{
    try
    {
        candidate.events.push_back(TimeEvent{kind, candidate.snapshot});
        return foundation::Result<void>::Success();
    }
    catch (const std::bad_alloc&)
    {
        return foundation::Result<void>::Failure(MakeTimeError("time.out_of_memory", "time event buffer allocation failed"));
    }
}

foundation::Result<void> TimeRuntime::AppendBoundaryEvents(TimeMutableState& candidate, const TimeSnapshot& previous)
{
    if (candidate.snapshot.calendar.year != previous.calendar.year || candidate.snapshot.calendar.month != previous.calendar.month ||
        candidate.snapshot.calendar.day != previous.calendar.day)
    {
        candidate.state = TimeRuntimeState::DayChanged;
        if (auto event = PushEvent(candidate, TimeEventKind::DayChanged); !event)
        {
            return event;
        }
    }

    if (candidate.snapshot.day_phase != previous.day_phase)
    {
        candidate.state = TimeRuntimeState::PhaseChanged;
        if (auto event = PushEvent(candidate, TimeEventKind::DayPhaseChanged); !event)
        {
            return event;
        }
    }
    return foundation::Result<void>::Success();
}

void TimeRuntime::Commit(TimeMutableState candidate) noexcept
{
    options_ = std::move(candidate.options);
    now_ = candidate.now;
    last_delta_ = candidate.last_delta;
    time_scale_ = candidate.time_scale;
    paused_ = candidate.paused;
    state_ = candidate.state;
    snapshot_ = candidate.snapshot;
    events_ = std::move(candidate.events);
    tick_remainder_numerator_ = candidate.tick_remainder_numerator;
    revision_ = candidate.snapshot.revision;
}

void TimeRuntime::RefreshSnapshot(bool changed)
{
    if (changed)
    {
        ++revision_;
    }

    snapshot_.now = now_;
    snapshot_.last_delta = last_delta_;
    snapshot_.time_scale = time_scale_;
    snapshot_.paused = paused_;
    snapshot_.calendar = ToCalendarDate(now_);
    snapshot_.day_phase = DetermineDayPhase(snapshot_.calendar);
    snapshot_.revision = revision_;
}

void TimeRuntime::PushEvent(TimeEventKind kind)
{
    events_.push_back(TimeEvent{kind, snapshot_});
}

void TimeRuntime::AppendBoundaryEvents(const TimeSnapshot& previous)
{
    if (snapshot_.calendar.year != previous.calendar.year || snapshot_.calendar.month != previous.calendar.month ||
        snapshot_.calendar.day != previous.calendar.day)
    {
        state_ = TimeRuntimeState::DayChanged;
        PushEvent(TimeEventKind::DayChanged);
    }

    if (snapshot_.day_phase != previous.day_phase)
    {
        state_ = TimeRuntimeState::PhaseChanged;
        PushEvent(TimeEventKind::DayPhaseChanged);
    }
}

void TimeRuntime::ClearEvents()
{
    events_.clear();
}
} // namespace epidemic::runtime
