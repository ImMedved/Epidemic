#include "time_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
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

    ClearEvents();
    const TimeSnapshot previous = snapshot_;

    if (paused_ || real_delta.count() == 0)
    {
        last_delta_ = GameDuration{};
        RefreshSnapshot(false);
        return foundation::Result<TimeAdvanceResult>::Success(MakeResult(previous));
    }

    const long double scaled_ticks =
        (static_cast<long double>(real_delta.count()) * static_cast<long double>(options_.game_ticks_per_real_second) *
         static_cast<long double>(time_scale_.numerator)) /
        (1000000.0L * static_cast<long double>(time_scale_.denominator));
    if (!std::isfinite(scaled_ticks))
    {
        return foundation::Result<TimeAdvanceResult>::Failure(
            MakeTimeError("time.overflow", "scaled time delta is not finite"));
    }

    const long double candidate_remainder = tick_remainder_ + scaled_ticks;
    if (!std::isfinite(candidate_remainder) ||
        candidate_remainder > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
    {
        return foundation::Result<TimeAdvanceResult>::Failure(
            MakeTimeError("time.overflow", "scaled time delta overflows game duration"));
    }

    const auto whole_ticks = static_cast<std::int64_t>(std::floor(candidate_remainder));
    const long double remaining_remainder = candidate_remainder - static_cast<long double>(whole_ticks);

    last_delta_ = GameDuration{whole_ticks};
    if (whole_ticks > 0)
    {
        const auto next = CheckedAdd(now_, last_delta_);
        if (!next)
        {
            last_delta_ = GameDuration{};
            return foundation::Result<TimeAdvanceResult>::Failure(
                MakeTimeError("time.overflow", "advancing time overflows game time"));
        }

        now_ = *next;
        tick_remainder_ = remaining_remainder;
        state_ = TimeRuntimeState::Running;
        RefreshSnapshot(true);
        PushEvent(TimeEventKind::TimeAdvanced);
        AppendBoundaryEvents(previous);
    }
    else
    {
        tick_remainder_ = remaining_remainder;
        RefreshSnapshot(false);
    }

    return foundation::Result<TimeAdvanceResult>::Success(MakeResult(previous));
}

foundation::Result<void> TimeRuntime::Pause()
{
    ClearEvents();
    if (paused_)
    {
        return foundation::Result<void>::Success();
    }

    paused_ = true;
    last_delta_ = GameDuration{};
    state_ = TimeRuntimeState::Paused;
    RefreshSnapshot(true);
    PushEvent(TimeEventKind::Paused);
    return foundation::Result<void>::Success();
}

foundation::Result<void> TimeRuntime::Resume()
{
    ClearEvents();
    if (!paused_)
    {
        return foundation::Result<void>::Success();
    }

    paused_ = false;
    state_ = TimeRuntimeState::Running;
    RefreshSnapshot(true);
    PushEvent(TimeEventKind::Resumed);
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
    ClearEvents();
    if (time_scale_ == scale)
    {
        return foundation::Result<void>::Success();
    }

    time_scale_ = scale;
    state_ = TimeRuntimeState::TimeScaleChanged;
    RefreshSnapshot(true);
    PushEvent(TimeEventKind::TimeScaleChanged);
    return foundation::Result<void>::Success();
}

foundation::Result<void> TimeRuntime::Skip(GameDuration duration)
{
    if (duration.ticks < 0)
    {
        return foundation::Result<void>::Failure(
            MakeTimeError("time.invalid_skip", "time skip duration must not be negative"));
    }

    ClearEvents();
    const TimeSnapshot previous = snapshot_;
    if (duration.ticks == 0)
    {
        last_delta_ = GameDuration{};
        RefreshSnapshot(false);
        return foundation::Result<void>::Success();
    }

    const auto next = CheckedAdd(now_, duration);
    if (!next)
    {
        return foundation::Result<void>::Failure(MakeTimeError("time.overflow", "skipping time overflows game time"));
    }

    now_ = *next;
    last_delta_ = duration;
    state_ = TimeRuntimeState::TimeJumped;
    RefreshSnapshot(true);
    PushEvent(TimeEventKind::TimeJumped);
    AppendBoundaryEvents(previous);
    return foundation::Result<void>::Success();
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
    const auto clamped_ticks = std::max<std::int64_t>(0, time.ticks);
    const auto seconds_per_day = SecondsPerDay(options_.calendar);
    const auto total_days = clamped_ticks / seconds_per_day;
    const auto second_of_day = clamped_ticks % seconds_per_day;
    const auto days_per_year = DaysPerYear(options_.calendar);
    const auto day_in_year = total_days % days_per_year;

    CalendarDate date{};
    date.year = 1 + (total_days / days_per_year);
    date.month = 1 + static_cast<std::uint32_t>(day_in_year / options_.calendar.days_per_month);
    date.day = 1 + static_cast<std::uint32_t>(day_in_year % options_.calendar.days_per_month);
    date.hour = static_cast<std::uint32_t>(second_of_day / kSecondsPerHour);
    date.minute = static_cast<std::uint32_t>((second_of_day % kSecondsPerHour) / kSecondsPerMinute);
    date.second = static_cast<std::uint32_t>(second_of_day % kSecondsPerMinute);
    return date;
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
    const auto minute_of_day = (date.hour * 60) + date.minute;
    DayPhase phase = DayPhase::Night;
    for (const auto& boundary : options_.phase_boundaries)
    {
        if (boundary.start_minute <= minute_of_day)
        {
            phase = boundary.phase;
        }
    }

    return phase;
}

TimeAdvanceResult TimeRuntime::MakeResult(TimeSnapshot previous) const
{
    return TimeAdvanceResult{previous, snapshot_, events_};
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
