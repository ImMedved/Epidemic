#include "time_runtime_impl.h"

// File note:
// Implementation file for the surrounding runtime type or test fixture. The comments
// below describe responsibilities, data flow and relationships between local helpers.
#include "Epidemic/Foundation/error.h"

#include <algorithm>

namespace epidemic::runtime
{
namespace
{
constexpr std::int64_t kMinutesPerDay = 24 * 60;
constexpr std::int64_t kDaysPerYear = 365;
}

// Function note: Handles time runtime.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
TimeRuntime::TimeRuntime()
{
    // Function note: Handles refresh snapshot.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    RefreshSnapshot();
}

// Function note: Handles now.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
GameTime TimeRuntime::Now() const
{
    return now_;
}

// Function note: Handles last delta.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
GameDuration TimeRuntime::LastDelta() const
{
    return last_delta_;
}

// Function note: Gets time scale.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
float TimeRuntime::GetTimeScale() const
{
    return time_scale_;
}

// Function note: Checks paused.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
bool TimeRuntime::IsPaused() const
{
    return paused_;
}

// Function note: Gets calendar date.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
CalendarDate TimeRuntime::GetCalendarDate() const
{
    return snapshot_.date;
}

// Function note: Gets day phase.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
DayPhase TimeRuntime::GetDayPhase() const
{
    return snapshot_.phase;
}

// Function note: Sets time scale.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void TimeRuntime::SetTimeScale(float scale)
{
    // Function note: Handles max.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const float clamped_scale = std::max(0.0f, scale);
    if (time_scale_ == clamped_scale)
    {
        return;
    }

    // Function note: Clears events.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ClearEvents();
    time_scale_ = clamped_scale;
    state_ = TimeRuntimeState::TimeScaleChanged;
    // Function note: Handles refresh snapshot.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    RefreshSnapshot();
    // Function note: Pushes event.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    PushEvent(TimeEventKind::TimeScaleChanged);
}

// Function note: Pauses the associated runtime state.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void TimeRuntime::Pause()
{
    if (paused_)
    {
        return;
    }

    // Function note: Clears events.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ClearEvents();
    paused_ = true;
    last_delta_ = GameDuration{};
    state_ = TimeRuntimeState::Paused;
    // Function note: Handles refresh snapshot.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    RefreshSnapshot();
    // Function note: Pushes event.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    PushEvent(TimeEventKind::Paused);
}

// Function note: Resumes the associated runtime state.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void TimeRuntime::Resume()
{
    if (!paused_)
    {
        return;
    }

    // Function note: Clears events.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ClearEvents();
    paused_ = false;
    state_ = TimeRuntimeState::Running;
    // Function note: Handles refresh snapshot.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    RefreshSnapshot();
    // Function note: Pushes event.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    PushEvent(TimeEventKind::Resumed);
}

// Function note: Skips the associated runtime state.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> TimeRuntime::Skip(GameDuration duration)
{
    if (duration.ticks < 0)
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("time.invalid_skip", "time skip duration must not be negative"));
    }

    // Function note: Clears events.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ClearEvents();

    const CalendarDate previous_date = snapshot_.date;
    const DayPhase previous_phase = snapshot_.phase;

    now_ = now_ + duration;
    last_delta_ = duration;
    state_ = TimeRuntimeState::Skipping;
    // Function note: Handles refresh snapshot.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    RefreshSnapshot();
    // Function note: Pushes event.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    PushEvent(TimeEventKind::TimeSkipped);
    // Function note: Appends boundary events.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    AppendBoundaryEvents(previous_date, previous_phase);
    state_ = TimeRuntimeState::TimeJumped;
    return foundation::Result<void>::Success();
}

// Function note: Updates the associated runtime state.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void TimeRuntime::Update(GameDuration real_delta)
{
    // Function note: Clears events.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ClearEvents();

    const CalendarDate previous_date = snapshot_.date;
    const DayPhase previous_phase = snapshot_.phase;

    if (paused_ || real_delta.ticks <= 0)
    {
        last_delta_ = GameDuration{};
        state_ = paused_ ? TimeRuntimeState::Paused : TimeRuntimeState::Running;
        // Function note: Handles refresh snapshot.
        // Inputs/outputs: see the signature; the method consumes caller-provided values and
        // returns either a value, status flag or Result according to the surrounding API.
        // Relations: this member is part of the local runtime workflow and pairs with
        // neighboring query/update helpers defined in the same class or file.
        RefreshSnapshot();
        return;
    }

    const auto scaled_ticks = static_cast<std::int64_t>(static_cast<double>(real_delta.ticks) * static_cast<double>(time_scale_));
    last_delta_ = GameDuration{scaled_ticks};
    now_ = now_ + last_delta_;
    state_ = TimeRuntimeState::Running;
    // Function note: Handles refresh snapshot.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    RefreshSnapshot();

    if (!last_delta_.IsZero())
    {
        // Function note: Pushes event.
        // Inputs/outputs: see the signature; the method consumes caller-provided values and
        // returns either a value, status flag or Result according to the surrounding API.
        // Relations: this member is part of the local runtime workflow and pairs with
        // neighboring query/update helpers defined in the same class or file.
        PushEvent(TimeEventKind::TimeAdvanced);
        // Function note: Appends boundary events.
        // Inputs/outputs: see the signature; the method consumes caller-provided values and
        // returns either a value, status flag or Result according to the surrounding API.
        // Relations: this member is part of the local runtime workflow and pairs with
        // neighboring query/update helpers defined in the same class or file.
        AppendBoundaryEvents(previous_date, previous_phase);
    }
}

// Function note: Gets snapshot.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
TimeSnapshot TimeRuntime::GetSnapshot() const
{
    return snapshot_;
}

// Function note: Gets events.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
const std::vector<TimeEvent>& TimeRuntime::GetEvents() const
{
    return events_;
}

// Function note: Handles to calendar date.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
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

// Function note: Handles determine day phase.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
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

// Function note: Handles refresh snapshot.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void TimeRuntime::RefreshSnapshot()
{
    snapshot_.now = now_;
    snapshot_.delta = last_delta_;
    // Function note: Handles to calendar date.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    snapshot_.date = ToCalendarDate(now_);
    // Function note: Handles determine day phase.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    snapshot_.phase = DetermineDayPhase(snapshot_.date);
    snapshot_.time_scale = time_scale_;
    snapshot_.paused = paused_;
}

// Function note: Pushes event.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void TimeRuntime::PushEvent(TimeEventKind kind)
{
    events_.push_back(TimeEvent{kind, snapshot_});
}

// Function note: Appends boundary events.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void TimeRuntime::AppendBoundaryEvents(CalendarDate previous_date, DayPhase previous_phase)
{
    if (snapshot_.date.year != previous_date.year || snapshot_.date.day_of_year != previous_date.day_of_year)
    {
        state_ = TimeRuntimeState::DayChanged;
        // Function note: Pushes event.
        // Inputs/outputs: see the signature; the method consumes caller-provided values and
        // returns either a value, status flag or Result according to the surrounding API.
        // Relations: this member is part of the local runtime workflow and pairs with
        // neighboring query/update helpers defined in the same class or file.
        PushEvent(TimeEventKind::DayChanged);
    }

    if (snapshot_.phase != previous_phase)
    {
        state_ = TimeRuntimeState::PhaseChanged;
        // Function note: Pushes event.
        // Inputs/outputs: see the signature; the method consumes caller-provided values and
        // returns either a value, status flag or Result according to the surrounding API.
        // Relations: this member is part of the local runtime workflow and pairs with
        // neighboring query/update helpers defined in the same class or file.
        PushEvent(TimeEventKind::DayPhaseChanged);
    }
}

// Function note: Clears events.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void TimeRuntime::ClearEvents()
{
    events_.clear();
}
} 
