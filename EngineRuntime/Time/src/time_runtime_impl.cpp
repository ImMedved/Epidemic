#include "time_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>

namespace epidemic::runtime
{
namespace
{
constexpr std::int64_t kMinutesPerDay = 24 * 60;
constexpr std::int64_t kDaysPerYear = 365;
}

TimeRuntime::TimeRuntime()
{
    RefreshSnapshot();
}

GameTime TimeRuntime::Now() const
{
    return now_;
}

GameDuration TimeRuntime::LastDelta() const
{
    return last_delta_;
}

float TimeRuntime::GetTimeScale() const
{
    return time_scale_;
}

bool TimeRuntime::IsPaused() const
{
    return paused_;
}

CalendarDate TimeRuntime::GetCalendarDate() const
{
    return snapshot_.date;
}

DayPhase TimeRuntime::GetDayPhase() const
{
    return snapshot_.phase;
}

void TimeRuntime::SetTimeScale(float scale)
{
    const float clamped_scale = std::max(0.0f, scale);
    if (time_scale_ == clamped_scale)
    {
        return;
    }

    ClearEvents();
    time_scale_ = clamped_scale;
    state_ = TimeRuntimeState::TimeScaleChanged;
    RefreshSnapshot();
    PushEvent(TimeEventKind::TimeScaleChanged);
}

void TimeRuntime::Pause()
{
    if (paused_)
    {
        return;
    }

    ClearEvents();
    paused_ = true;
    last_delta_ = GameDuration{};
    state_ = TimeRuntimeState::Paused;
    RefreshSnapshot();
    PushEvent(TimeEventKind::Paused);
}

void TimeRuntime::Resume()
{
    if (!paused_)
    {
        return;
    }

    ClearEvents();
    paused_ = false;
    state_ = TimeRuntimeState::Running;
    RefreshSnapshot();
    PushEvent(TimeEventKind::Resumed);
}

foundation::Result<void> TimeRuntime::Skip(GameDuration duration)
{
    if (duration.ticks < 0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("time.invalid_skip", "time skip duration must not be negative"));
    }

    ClearEvents();

    const CalendarDate previous_date = snapshot_.date;
    const DayPhase previous_phase = snapshot_.phase;

    now_ = now_ + duration;
    last_delta_ = duration;
    state_ = TimeRuntimeState::Skipping;
    RefreshSnapshot();
    PushEvent(TimeEventKind::TimeSkipped);
    AppendBoundaryEvents(previous_date, previous_phase);
    state_ = TimeRuntimeState::TimeJumped;
    return foundation::Result<void>::Success();
}

void TimeRuntime::Update(GameDuration real_delta)
{
    ClearEvents();

    const CalendarDate previous_date = snapshot_.date;
    const DayPhase previous_phase = snapshot_.phase;

    if (paused_ || real_delta.ticks <= 0)
    {
        last_delta_ = GameDuration{};
        state_ = paused_ ? TimeRuntimeState::Paused : TimeRuntimeState::Running;
        RefreshSnapshot();
        return;
    }

    const auto scaled_ticks = static_cast<std::int64_t>(static_cast<double>(real_delta.ticks) * static_cast<double>(time_scale_));
    last_delta_ = GameDuration{scaled_ticks};
    now_ = now_ + last_delta_;
    state_ = TimeRuntimeState::Running;
    RefreshSnapshot();

    if (!last_delta_.IsZero())
    {
        PushEvent(TimeEventKind::TimeAdvanced);
        AppendBoundaryEvents(previous_date, previous_phase);
    }
}

TimeSnapshot TimeRuntime::GetSnapshot() const
{
    return snapshot_;
}

const std::vector<TimeEvent>& TimeRuntime::GetEvents() const
{
    return events_;
}

CalendarDate TimeRuntime::ToCalendarDate(GameTime time) noexcept
{
    const std::int64_t clamped_ticks = std::max<std::int64_t>(0, time.ticks);
    const std::int64_t total_days = clamped_ticks / kMinutesPerDay;
    const std::int64_t minute_of_day = clamped_ticks % kMinutesPerDay;

    CalendarDate date{};
    date.year = static_cast<std::int32_t>(1 + (total_days / kDaysPerYear));
    date.day_of_year = static_cast<std::uint32_t>(1 + (total_days % kDaysPerYear));
    date.hour = static_cast<std::uint32_t>(minute_of_day / 60);
    date.minute = static_cast<std::uint32_t>(minute_of_day % 60);
    return date;
}

DayPhase TimeRuntime::DetermineDayPhase(const CalendarDate& date) noexcept
{
    if (date.hour >= 5 && date.hour < 8)
    {
        return DayPhase::Dawn;
    }

    if (date.hour >= 8 && date.hour < 12)
    {
        return DayPhase::Morning;
    }

    if (date.hour >= 12 && date.hour < 14)
    {
        return DayPhase::Noon;
    }

    if (date.hour >= 14 && date.hour < 18)
    {
        return DayPhase::Afternoon;
    }

    if (date.hour >= 18 && date.hour < 22)
    {
        return DayPhase::Evening;
    }

    return DayPhase::Night;
}

void TimeRuntime::RefreshSnapshot()
{
    snapshot_.now = now_;
    snapshot_.delta = last_delta_;
    snapshot_.date = ToCalendarDate(now_);
    snapshot_.phase = DetermineDayPhase(snapshot_.date);
    snapshot_.time_scale = time_scale_;
    snapshot_.paused = paused_;
}

void TimeRuntime::PushEvent(TimeEventKind kind)
{
    events_.push_back(TimeEvent{kind, snapshot_});
}

void TimeRuntime::AppendBoundaryEvents(CalendarDate previous_date, DayPhase previous_phase)
{
    if (snapshot_.date.year != previous_date.year || snapshot_.date.day_of_year != previous_date.day_of_year)
    {
        state_ = TimeRuntimeState::DayChanged;
        PushEvent(TimeEventKind::DayChanged);
    }

    if (snapshot_.phase != previous_phase)
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
