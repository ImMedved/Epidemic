#include "Epidemic/GameFramework/Time/gameplay_time.h"

#include <algorithm>
#include <limits>
#include <set>
#include <unordered_set>

namespace epidemic::gameplay::time
{
namespace
{
[[nodiscard]] std::optional<std::int64_t> CheckedMultiply(std::int64_t left, std::int64_t right)
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
} // namespace

foundation::Result<ClockId> GameplayTimeService::RegisterClock(std::string_view canonical_name,
                                                               std::optional<CalendarDefinition> calendar)
{
    if (frozen_)
        return foundation::Result<ClockId>::Failure(foundation::Error::Create(
            "gameplay.registry_frozen", "gameplay clock registry is frozen", std::string(canonical_name)));
    if (canonical_name.empty())
        return foundation::Result<ClockId>::Failure(
            foundation::Error::Create("gameplay.invalid_clock", "clock canonical name must not be empty"));
    if (calendar.has_value() &&
        (calendar->hours_per_day == 0 || calendar->days_per_month == 0 || calendar->months_per_year == 0))
        return foundation::Result<ClockId>::Failure(
            foundation::Error::Create("gameplay.invalid_calendar", "calendar units must be non-zero"));
    const ClockId id = ClockId::FromString(canonical_name);
    const auto found = clock_definitions_.find(id);
    if (found != clock_definitions_.end())
    {
        if (found->second.canonical_name != canonical_name)
            return foundation::Result<ClockId>::Failure(
                foundation::Error::Create("gameplay.id_collision", "clock id collision", std::string(canonical_name)));
        return foundation::Result<ClockId>::Failure(foundation::Error::Create(
            "gameplay.already_registered", "clock already registered", std::string(canonical_name)));
    }
    auto staged_definitions = clock_definitions_;
    auto staged_clocks = clocks_;
    auto staged_index = schedule_index_;
    staged_definitions.emplace(id, ClockDefinition{id, std::string(canonical_name), calendar});
    staged_clocks.emplace(id, ClockState{id, {}, {}});
    staged_index.emplace(id, std::set<ScheduleKey>{});
    clock_definitions_.swap(staged_definitions);
    clocks_.swap(staged_clocks);
    schedule_index_.swap(staged_index);
    return foundation::Result<ClockId>::Success(id);
}

foundation::Result<ActionTypeId> GameplayTimeService::RegisterAction(std::string_view canonical_name,
                                                                     GameplayDomainId owner_domain)
{
    if (frozen_)
    {
        return foundation::Result<ActionTypeId>::Failure(foundation::Error::Create(
            "gameplay.registry_frozen", "schedule action registry is frozen", std::string(canonical_name)));
    }
    if (canonical_name.empty() || !owner_domain.IsValid())
    {
        return foundation::Result<ActionTypeId>::Failure(foundation::Error::Create(
            "gameplay.invalid_action", "action canonical name and owner domain must be valid"));
    }

    const ActionTypeId id = ActionTypeId::FromString(canonical_name);
    const auto found = action_types_.find(id);
    if (found != action_types_.end())
    {
        if (found->second.canonical_name != canonical_name)
        {
            return foundation::Result<ActionTypeId>::Failure(foundation::Error::Create(
                "gameplay.id_collision", "action type id collision", std::string(canonical_name)));
        }
        return foundation::Result<ActionTypeId>::Failure(foundation::Error::Create(
            "gameplay.already_registered", "schedule action already registered", std::string(canonical_name)));
    }

    action_types_.emplace(id, ActionTypeInfo{id, std::string(canonical_name), owner_domain});
    return foundation::Result<ActionTypeId>::Success(id);
}

foundation::Result<void> GameplayTimeService::AdvanceClock(ClockId clock, GameplayDuration real_delta)
{
    auto found = clocks_.find(clock);
    if (found == clocks_.end())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.clock_unknown", "gameplay clock is not registered"));
    }
    if (real_delta.ticks < 0)
    {
        return foundation::Result<void>::Failure(foundation::Error::Create(
            "gameplay.clock_negative_delta", "gameplay clock cannot advance by negative delta"));
    }
    if (found->second.paused || real_delta.ticks == 0 || found->second.time_scale_milli == 0)
    {
        return foundation::Result<void>::Success();
    }

    const auto scale = static_cast<std::int64_t>(found->second.time_scale_milli);
    const auto whole_real = real_delta.ticks / 1000;
    const auto remainder_real = real_delta.ticks % 1000;
    if (whole_real != 0 && scale > std::numeric_limits<std::int64_t>::max() / whole_real)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.time_overflow", "scaled clock delta overflows gameplay time"));
    }
    const auto scaled_whole = whole_real * scale;
    const auto fractional_numerator =
        remainder_real * scale + static_cast<std::int64_t>(found->second.fractional_milli);
    const auto scaled_fraction = fractional_numerator / 1000;
    if (scaled_whole > std::numeric_limits<std::int64_t>::max() - scaled_fraction)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.time_overflow", "scaled clock delta overflows gameplay time"));
    }
    const GameplayDuration scaled{scaled_whole + scaled_fraction};
    const auto new_fractional = static_cast<std::uint32_t>(fractional_numerator % 1000);
    const auto advanced = CheckedAdd(found->second.now, scaled);
    if (!advanced)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.time_overflow", "clock advance overflows gameplay time"));
    }
    if (*advanced == found->second.now && new_fractional == found->second.fractional_milli)
    {
        return foundation::Result<void>::Success();
    }
    const auto next_revision = CheckedNext(found->second.revision);
    if (!next_revision)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.revision_exhausted", "clock revision is exhausted"));
    }
    found->second.now = *advanced;
    found->second.fractional_milli = new_fractional;
    found->second.revision = *next_revision;
    return foundation::Result<void>::Success();
}

foundation::Result<void> GameplayTimeService::AdvanceTo(ClockId clock, GameplayTimePoint now)
{
    auto found = clocks_.find(clock);
    if (found == clocks_.end())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.clock_unknown", "gameplay clock is not registered"));
    }
    if (now < found->second.now)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.clock_rewind", "gameplay clock cannot move backwards outside restore"));
    }
    if (now == found->second.now)
    {
        return foundation::Result<void>::Success();
    }
    const auto next_revision = CheckedNext(found->second.revision);
    if (!next_revision)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.revision_exhausted", "clock revision is exhausted"));
    }
    found->second.now = now;
    found->second.fractional_milli = 0;
    found->second.revision = *next_revision;
    return foundation::Result<void>::Success();
}

foundation::Result<void> GameplayTimeService::SetPaused(ClockId clock, bool paused)
{
    auto found = clocks_.find(clock);
    if (found == clocks_.end())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.clock_unknown", "gameplay clock is not registered"));
    }
    if (found->second.paused == paused)
    {
        return foundation::Result<void>::Success();
    }
    const auto next_revision = CheckedNext(found->second.revision);
    if (!next_revision)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.revision_exhausted", "clock revision is exhausted"));
    }
    found->second.paused = paused;
    found->second.revision = *next_revision;
    return foundation::Result<void>::Success();
}

foundation::Result<void> GameplayTimeService::SetTimeScale(ClockId clock, std::uint32_t milli_scale)
{
    auto found = clocks_.find(clock);
    if (found == clocks_.end())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.clock_unknown", "gameplay clock is not registered"));
    }
    if (milli_scale > 1000000)
    {
        return foundation::Result<void>::Failure(foundation::Error::Create(
            "gameplay.clock_invalid_scale", "gameplay time scale is outside supported range"));
    }
    if (found->second.time_scale_milli == milli_scale)
    {
        return foundation::Result<void>::Success();
    }
    const auto next_revision = CheckedNext(found->second.revision);
    if (!next_revision)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.revision_exhausted", "clock revision is exhausted"));
    }
    found->second.time_scale_milli = milli_scale;
    found->second.revision = *next_revision;
    return foundation::Result<void>::Success();
}

foundation::Result<void> GameplayTimeService::SynchronizeClock(ClockId clock, GameplayTimePoint now,
                                                               Revision source_revision)
{
    if (!source_revision.value)
        return foundation::Result<void>::Failure(foundation::Error::Create(
            "gameplay.clock_sync_revision_invalid", "clock synchronization requires a non-zero source revision"));
    const auto current = clocks_.find(clock);
    if (current == clocks_.end())
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.clock_unknown", "gameplay clock is not registered"));
    const auto previous = synchronization_revisions_.find(clock);
    if (previous != synchronization_revisions_.end() && source_revision <= previous->second)
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.clock_sync_stale", "clock synchronization source revision is stale"));
    if (now < current->second.now)
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.clock_rewind", "gameplay clock cannot move backwards outside restore"));
    auto staged_clocks = clocks_;
    auto staged_sync = synchronization_revisions_;
    auto staged_rebind = synchronization_rebind_required_;
    auto &staged_clock = staged_clocks.at(clock);
    if (now != staged_clock.now)
    {
        const auto next_revision = CheckedNext(staged_clock.revision);
        if (!next_revision)
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.revision_exhausted", "clock revision is exhausted"));
        staged_clock.now = now;
        staged_clock.fractional_milli = 0;
        staged_clock.revision = *next_revision;
    }
    staged_sync.insert_or_assign(clock, source_revision);
    staged_rebind.erase(clock);
    clocks_.swap(staged_clocks);
    synchronization_revisions_.swap(staged_sync);
    synchronization_rebind_required_.swap(staged_rebind);
    return foundation::Result<void>::Success();
}

bool GameplayTimeService::NeedsSynchronizationRebind(ClockId clock) const noexcept
{
    return synchronization_rebind_required_.contains(clock);
}

std::vector<ClockId> GameplayTimeService::ClocksRequiringSynchronizationRebind() const
{
    return std::vector<ClockId>(synchronization_rebind_required_.begin(), synchronization_rebind_required_.end());
}

std::optional<ClockState> GameplayTimeService::GetClock(ClockId clock) const noexcept
{
    const auto found = clocks_.find(clock);
    return found == clocks_.end() ? std::nullopt : std::optional<ClockState>{found->second};
}
std::optional<ClockDefinition> GameplayTimeService::GetClockDefinition(ClockId clock) const noexcept
{
    const auto found = clock_definitions_.find(clock);
    return found == clock_definitions_.end() ? std::nullopt : std::optional<ClockDefinition>{found->second};
}
const ClockDefinition *GameplayTimeService::FindClockDefinition(ClockId clock) const noexcept
{
    const auto found = clock_definitions_.find(clock);
    return found == clock_definitions_.end() ? nullptr : &found->second;
}

foundation::Result<void> GameplayTimeService::ValidateCalendarDate(ClockId clock, const CalendarDate &date) const
{
    const auto *definition = FindClockDefinition(clock);
    if (definition == nullptr || !definition->calendar.has_value())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.calendar_unavailable", "clock has no calendar definition"));
    }
    const auto &calendar = *definition->calendar;
    if (date.year < 1 || date.month == 0 || date.month > calendar.months_per_year || date.day == 0 ||
        date.day > calendar.days_per_month || date.hour >= calendar.hours_per_day || date.minute >= 60 ||
        date.second >= 60)
    {
        return foundation::Result<void>::Failure(foundation::Error::Create(
            "gameplay.calendar_date_invalid", "calendar date is outside the clock calendar range"));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<GameplayTimePoint> GameplayTimeService::ToGameplayTime(ClockId clock, const CalendarDate &date) const
{
    const auto valid = ValidateCalendarDate(clock, date);
    if (!valid)
    {
        return foundation::Result<GameplayTimePoint>::Failure(valid.GetError());
    }
    const auto &calendar = *FindClockDefinition(clock)->calendar;
    const auto seconds_per_day =
        CheckedMultiply(static_cast<std::int64_t>(calendar.hours_per_day), kGameplayTicksPerHour);
    const auto days_per_year = CheckedMultiply(static_cast<std::int64_t>(calendar.days_per_month),
                                               static_cast<std::int64_t>(calendar.months_per_year));
    if (!seconds_per_day || !days_per_year)
    {
        return foundation::Result<GameplayTimePoint>::Failure(
            foundation::Error::Create("gameplay.time_overflow", "calendar dimensions overflow gameplay time"));
    }
    const auto year_index = date.year - 1;
    if (year_index != 0 && *days_per_year > std::numeric_limits<std::int64_t>::max() / year_index)
    {
        return foundation::Result<GameplayTimePoint>::Failure(
            foundation::Error::Create("gameplay.time_overflow", "calendar year overflows gameplay time"));
    }
    auto day_index = year_index * *days_per_year;
    const auto month_days =
        static_cast<std::int64_t>(date.month - 1) * static_cast<std::int64_t>(calendar.days_per_month);
    if (day_index > std::numeric_limits<std::int64_t>::max() - month_days - static_cast<std::int64_t>(date.day - 1))
    {
        return foundation::Result<GameplayTimePoint>::Failure(
            foundation::Error::Create("gameplay.time_overflow", "calendar date overflows gameplay time"));
    }
    day_index += month_days + static_cast<std::int64_t>(date.day - 1);
    if (day_index != 0 && *seconds_per_day > std::numeric_limits<std::int64_t>::max() / day_index)
    {
        return foundation::Result<GameplayTimePoint>::Failure(
            foundation::Error::Create("gameplay.time_overflow", "calendar date overflows gameplay time"));
    }
    const auto second_of_day = static_cast<std::int64_t>(date.hour) * kGameplayTicksPerHour +
                               static_cast<std::int64_t>(date.minute) * kGameplayTicksPerMinute + date.second;
    const auto base = day_index * *seconds_per_day;
    if (base > std::numeric_limits<std::int64_t>::max() - second_of_day)
    {
        return foundation::Result<GameplayTimePoint>::Failure(
            foundation::Error::Create("gameplay.time_overflow", "calendar date overflows gameplay time"));
    }
    return foundation::Result<GameplayTimePoint>::Success(GameplayTimePoint{base + second_of_day});
}

foundation::Result<CalendarDate> GameplayTimeService::ToCalendarDate(ClockId clock, GameplayTimePoint time) const
{
    const auto *definition = FindClockDefinition(clock);
    if (definition == nullptr || !definition->calendar.has_value())
    {
        return foundation::Result<CalendarDate>::Failure(
            foundation::Error::Create("gameplay.calendar_unavailable", "clock has no calendar definition"));
    }
    if (time.ticks < 0)
    {
        return foundation::Result<CalendarDate>::Failure(foundation::Error::Create(
            "gameplay.calendar_time_invalid", "negative gameplay time cannot be represented by this calendar"));
    }
    const auto &calendar = *definition->calendar;
    const auto seconds_per_day =
        CheckedMultiply(static_cast<std::int64_t>(calendar.hours_per_day), kGameplayTicksPerHour);
    const auto days_per_year = CheckedMultiply(static_cast<std::int64_t>(calendar.days_per_month),
                                               static_cast<std::int64_t>(calendar.months_per_year));
    if (!seconds_per_day || !days_per_year)
    {
        return foundation::Result<CalendarDate>::Failure(
            foundation::Error::Create("gameplay.time_overflow", "calendar dimensions overflow gameplay time"));
    }
    const auto day_index = time.ticks / *seconds_per_day;
    const auto second_of_day = time.ticks % *seconds_per_day;
    const auto year_index = day_index / *days_per_year;
    if (year_index == std::numeric_limits<std::int64_t>::max())
    {
        return foundation::Result<CalendarDate>::Failure(
            foundation::Error::Create("gameplay.time_overflow", "calendar year cannot be represented"));
    }
    const auto day_of_year = day_index % *days_per_year;
    CalendarDate date;
    date.year = year_index + 1;
    date.month = static_cast<std::uint32_t>(day_of_year / calendar.days_per_month) + 1;
    date.day = static_cast<std::uint32_t>(day_of_year % calendar.days_per_month) + 1;
    date.hour = static_cast<std::uint32_t>(second_of_day / kGameplayTicksPerHour);
    const auto within_hour = second_of_day % kGameplayTicksPerHour;
    date.minute = static_cast<std::uint32_t>(within_hour / kGameplayTicksPerMinute);
    date.second = static_cast<std::uint32_t>(within_hour % kGameplayTicksPerMinute);
    return foundation::Result<CalendarDate>::Success(date);
}

foundation::Result<void> GameplayTimeService::ValidateRecurrence(ClockId clock, const RecurrenceRule &recurrence) const
{
    switch (recurrence.kind)
    {
    case RecurrenceKind::Once:
        return foundation::Result<void>::Success();
    case RecurrenceKind::FixedInterval:
        if (recurrence.interval.ticks <= 0)
        {
            return foundation::Result<void>::Failure(foundation::Error::Create(
                "gameplay.schedule_invalid_interval", "fixed recurrence interval must be positive"));
        }
        return foundation::Result<void>::Success();
    case RecurrenceKind::CalendarPattern: {
        const auto *definition = FindClockDefinition(clock);
        if (definition == nullptr || !definition->calendar.has_value())
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.schedule_calendar_unavailable",
                                          "calendar recurrence requires a clock with calendar definition"));
        }
        const auto &calendar = *definition->calendar;
        if (recurrence.calendar.every_days == 0 || recurrence.calendar.hour >= calendar.hours_per_day ||
            recurrence.calendar.minute >= 60 || recurrence.calendar.second >= 60)
        {
            return foundation::Result<void>::Failure(foundation::Error::Create(
                "gameplay.schedule_invalid_calendar_pattern", "calendar recurrence pattern is invalid"));
        }
        return foundation::Result<void>::Success();
    }
    }
    return foundation::Result<void>::Failure(
        foundation::Error::Create("gameplay.schedule_invalid_recurrence", "unknown recurrence kind"));
}

foundation::Result<ScheduleId> GameplayTimeService::Schedule(ClockId clock, GameplayTimePoint due,
                                                             GameplayObjectRef owner, ActionTypeId action,
                                                             RecurrenceRule recurrence, CatchUpPolicy catch_up,
                                                             SchedulePersistence persistence)
{
    if (!clock_definitions_.contains(clock))
        return foundation::Result<ScheduleId>::Failure(
            foundation::Error::Create("gameplay.clock_unknown", "schedule clock is not registered"));
    const auto action_info = action_types_.find(action);
    if (action_info == action_types_.end())
        return foundation::Result<ScheduleId>::Failure(
            foundation::Error::Create("gameplay.action_unknown", "schedule action is not registered"));
    if (!owner.IsValid() || owner.domain != action_info->second.owner_domain)
        return foundation::Result<ScheduleId>::Failure(foundation::Error::Create(
            "gameplay.schedule_owner_mismatch", "schedule owner must be valid and match action owner domain"));
    const auto recurrence_result = ValidateRecurrence(clock, recurrence);
    if (!recurrence_result)
        return foundation::Result<ScheduleId>::Failure(recurrence_result.GetError());
    const auto next_scheduler_revision = CheckedNext(scheduler_revision_);
    if (!next_scheduler_revision)
        return foundation::Result<ScheduleId>::Failure(
            foundation::Error::Create("gameplay.schedule_revision_exhausted", "scheduler revision is exhausted"));
    auto staged_ids = schedule_ids_;
    const ScheduleId id = staged_ids.Next();
    if (!id.IsValid())
        return foundation::Result<ScheduleId>::Failure(
            foundation::Error::Create("gameplay.schedule_id_exhausted", "schedule id generator is exhausted"));
    ScheduleEntry entry{id, clock, due, owner, action, recurrence, catch_up, persistence, Revision{1}};
    bool schedule_inserted = false;
    try
    {
        const auto [schedule_it, inserted] = schedules_.emplace(id, entry);
        if (!inserted)
            return foundation::Result<ScheduleId>::Failure(
                foundation::Error::Create("gameplay.schedule_id_collision", "schedule id already exists"));
        schedule_inserted = true;
        const auto [index_it, index_inserted] = schedule_index_.at(clock).insert(ScheduleKey{entry.due, entry.id});
        (void)index_it;
        if (!index_inserted)
        {
            schedules_.erase(schedule_it);
            return foundation::Result<ScheduleId>::Failure(
                foundation::Error::Create("gameplay.schedule_index_collision", "schedule index entry already exists"));
        }
    }
    catch (const std::bad_alloc &)
    {
        if (schedule_inserted)
            schedules_.erase(id);
        return foundation::Result<ScheduleId>::Failure(
            foundation::Error::Create("gameplay.schedule_allocation_failed", "failed to publish schedule"));
    }
    (void)schedule_ids_.Restore(staged_ids.GetSnapshot());
    scheduler_revision_ = *next_scheduler_revision;
    return foundation::Result<ScheduleId>::Success(id);
}

foundation::Result<GameplayTimePoint> GameplayTimeService::NextCalendarDue(ClockId clock, CalendarPattern pattern) const
{
    const auto state = GetClock(clock);
    const auto *definition = FindClockDefinition(clock);
    if (!state || definition == nullptr || !definition->calendar.has_value())
    {
        return foundation::Result<GameplayTimePoint>::Failure(foundation::Error::Create(
            "gameplay.schedule_calendar_unavailable", "calendar scheduling requires synchronized clock and calendar"));
    }
    const auto &calendar = *definition->calendar;
    if (pattern.every_days == 0 || pattern.hour >= calendar.hours_per_day || pattern.minute >= 60 ||
        pattern.second >= 60)
    {
        return foundation::Result<GameplayTimePoint>::Failure(foundation::Error::Create(
            "gameplay.schedule_invalid_calendar_pattern", "calendar schedule pattern is invalid"));
    }

    const auto seconds_per_day =
        CheckedMultiply(static_cast<std::int64_t>(calendar.hours_per_day), kGameplayTicksPerHour);
    if (!seconds_per_day)
    {
        return foundation::Result<GameplayTimePoint>::Failure(
            foundation::Error::Create("gameplay.time_overflow", "calendar day length overflows gameplay time"));
    }
    const auto target_second = static_cast<std::int64_t>(pattern.hour) * kGameplayTicksPerHour +
                               static_cast<std::int64_t>(pattern.minute) * kGameplayTicksPerMinute + pattern.second;
    const auto now = std::max<std::int64_t>(0, state->now.ticks);
    const auto day = now / *seconds_per_day;
    auto candidate = day * *seconds_per_day + target_second;
    if (candidate <= now)
    {
        const auto days_to_add = static_cast<std::int64_t>(pattern.every_days);
        if (day > (std::numeric_limits<std::int64_t>::max() / *seconds_per_day) - days_to_add)
        {
            return foundation::Result<GameplayTimePoint>::Failure(
                foundation::Error::Create("gameplay.time_overflow", "next calendar schedule overflows gameplay time"));
        }
        candidate = (day + days_to_add) * *seconds_per_day + target_second;
    }
    return foundation::Result<GameplayTimePoint>::Success(GameplayTimePoint{candidate});
}

foundation::Result<ScheduleId> GameplayTimeService::ScheduleCalendar(ClockId clock, GameplayObjectRef owner,
                                                                     ActionTypeId action, CalendarPattern pattern,
                                                                     CatchUpPolicy catch_up,
                                                                     SchedulePersistence persistence)
{
    const auto due = NextCalendarDue(clock, pattern);
    if (!due)
    {
        return foundation::Result<ScheduleId>::Failure(due.GetError());
    }
    RecurrenceRule recurrence;
    recurrence.kind = RecurrenceKind::CalendarPattern;
    recurrence.calendar = pattern;
    return Schedule(clock, due.Value(), owner, action, recurrence, catch_up, persistence);
}

void GameplayTimeService::InsertScheduleIndex(const ScheduleEntry &entry)
{
    schedule_index_[entry.clock].insert(ScheduleKey{entry.due, entry.id});
}

void GameplayTimeService::RemoveScheduleIndex(const ScheduleEntry &entry)
{
    const auto found = schedule_index_.find(entry.clock);
    if (found != schedule_index_.end())
    {
        found->second.erase(ScheduleKey{entry.due, entry.id});
    }
}

foundation::Result<void> GameplayTimeService::Cancel(ScheduleId schedule)
{
    const auto found = schedules_.find(schedule);
    if (found == schedules_.end())
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.schedule_unknown", "schedule does not exist"));
    const auto next_scheduler_revision = CheckedNext(scheduler_revision_);
    if (!next_scheduler_revision)
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.schedule_revision_exhausted", "scheduler revision is exhausted"));
    auto staged_schedules = schedules_;
    auto staged_index = schedule_index_;
    const auto staged_found = staged_schedules.find(schedule);
    staged_index.at(staged_found->second.clock).erase(ScheduleKey{staged_found->second.due, staged_found->second.id});
    staged_schedules.erase(staged_found);
    schedules_.swap(staged_schedules);
    schedule_index_.swap(staged_index);
    scheduler_revision_ = *next_scheduler_revision;
    if (cancelled_schedules_ != std::numeric_limits<std::uint64_t>::max())
        ++cancelled_schedules_;
    return foundation::Result<void>::Success();
}
std::uint64_t GameplayTimeService::CancelOwnedBy(GameplayObjectRef owner)
{
    std::vector<ScheduleId> ids;
    for (const auto &[id, entry] : schedules_)
        if (entry.owner == owner)
            ids.push_back(id);
    if (ids.empty())
        return 0;
    const auto next_scheduler_revision = CheckedNext(scheduler_revision_);
    if (!next_scheduler_revision)
        return 0;
    auto staged_schedules = schedules_;
    auto staged_index = schedule_index_;
    for (const auto id : ids)
    {
        const auto found = staged_schedules.find(id);
        if (found != staged_schedules.end())
        {
            staged_index.at(found->second.clock).erase(ScheduleKey{found->second.due, found->second.id});
            staged_schedules.erase(found);
        }
    }
    schedules_.swap(staged_schedules);
    schedule_index_.swap(staged_index);
    scheduler_revision_ = *next_scheduler_revision;
    const auto room = std::numeric_limits<std::uint64_t>::max() - cancelled_schedules_;
    cancelled_schedules_ += std::min<std::uint64_t>(room, ids.size());
    return ids.size();
}
foundation::Result<void> GameplayTimeService::Reschedule(ScheduleId schedule, GameplayTimePoint new_due)
{
    const auto found = schedules_.find(schedule);
    if (found == schedules_.end())
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.schedule_unknown", "schedule does not exist"));
    if (found->second.due == new_due)
        return foundation::Result<void>::Success();
    const auto next_scheduler_revision = CheckedNext(scheduler_revision_);
    const auto next_entry_revision = CheckedNext(found->second.revision);
    if (!next_scheduler_revision || !next_entry_revision)
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.schedule_revision_exhausted", "schedule revision is exhausted"));
    auto staged_schedules = schedules_;
    auto staged_index = schedule_index_;
    auto &staged = staged_schedules.at(schedule);
    staged_index.at(staged.clock).insert(ScheduleKey{new_due, staged.id});
    staged_index.at(staged.clock).erase(ScheduleKey{staged.due, staged.id});
    staged.due = new_due;
    staged.revision = *next_entry_revision;
    schedules_.swap(staged_schedules);
    schedule_index_.swap(staged_index);
    scheduler_revision_ = *next_scheduler_revision;
    return foundation::Result<void>::Success();
}
bool GameplayTimeService::HasSchedule(ScheduleId schedule) const noexcept
{
    return schedules_.contains(schedule);
}
std::optional<ScheduleEntry> GameplayTimeService::GetSchedule(ScheduleId schedule) const noexcept
{
    const auto found = schedules_.find(schedule);
    return found == schedules_.end() ? std::nullopt : std::optional<ScheduleEntry>{found->second};
}

foundation::Result<std::int64_t> GameplayTimeService::RecurrenceIntervalTicks(const ScheduleEntry &entry) const
{
    if (entry.recurrence.kind == RecurrenceKind::Once)
    {
        return foundation::Result<std::int64_t>::Success(0);
    }
    if (entry.recurrence.kind == RecurrenceKind::FixedInterval)
    {
        if (entry.recurrence.interval.ticks <= 0)
        {
            return foundation::Result<std::int64_t>::Failure(foundation::Error::Create(
                "gameplay.schedule_invalid_interval", "recurrence interval must be positive"));
        }
        return foundation::Result<std::int64_t>::Success(entry.recurrence.interval.ticks);
    }
    const auto *definition = FindClockDefinition(entry.clock);
    if (definition == nullptr || !definition->calendar.has_value())
    {
        return foundation::Result<std::int64_t>::Failure(foundation::Error::Create(
            "gameplay.schedule_calendar_unavailable", "calendar recurrence requires a calendar clock"));
    }
    const auto seconds_per_day =
        CheckedMultiply(static_cast<std::int64_t>(definition->calendar->hours_per_day), kGameplayTicksPerHour);
    if (!seconds_per_day)
    {
        return foundation::Result<std::int64_t>::Failure(
            foundation::Error::Create("gameplay.time_overflow", "calendar day length overflows gameplay time"));
    }
    const auto interval =
        CheckedMultiply(*seconds_per_day, static_cast<std::int64_t>(entry.recurrence.calendar.every_days));
    if (!interval || *interval <= 0)
    {
        return foundation::Result<std::int64_t>::Failure(foundation::Error::Create(
            "gameplay.time_overflow", "calendar recurrence interval overflows gameplay time"));
    }
    return foundation::Result<std::int64_t>::Success(*interval);
}

foundation::Result<std::uint64_t> GameplayTimeService::CalculateOccurrences(const ScheduleEntry &entry,
                                                                            GameplayTimePoint now) const
{
    if (entry.due > now)
    {
        return foundation::Result<std::uint64_t>::Success(0);
    }
    if (entry.recurrence.kind == RecurrenceKind::Once)
    {
        return foundation::Result<std::uint64_t>::Success(1);
    }
    const auto interval = RecurrenceIntervalTicks(entry);
    if (!interval)
    {
        return foundation::Result<std::uint64_t>::Failure(interval.GetError());
    }
    const auto elapsed = static_cast<std::uint64_t>(now.ticks) - static_cast<std::uint64_t>(entry.due.ticks);
    const auto quotient = elapsed / static_cast<std::uint64_t>(interval.Value());
    if (quotient == std::numeric_limits<std::uint64_t>::max())
    {
        return foundation::Result<std::uint64_t>::Success(std::numeric_limits<std::uint64_t>::max());
    }
    return foundation::Result<std::uint64_t>::Success(quotient + 1);
}

foundation::Result<GameplayTimePoint> GameplayTimeService::AdvanceDue(const ScheduleEntry &entry,
                                                                      std::uint64_t occurrences) const
{
    if (entry.recurrence.kind == RecurrenceKind::Once || occurrences == 0)
    {
        return foundation::Result<GameplayTimePoint>::Success(entry.due);
    }
    const auto interval = RecurrenceIntervalTicks(entry);
    if (!interval)
    {
        return foundation::Result<GameplayTimePoint>::Failure(interval.GetError());
    }
    if (occurrences > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() / interval.Value()))
    {
        return foundation::Result<GameplayTimePoint>::Failure(foundation::Error::Create(
            "gameplay.time_overflow", "recurring schedule next due time overflows gameplay time"));
    }
    const auto delta = GameplayDuration{interval.Value() * static_cast<std::int64_t>(occurrences)};
    const auto advanced = CheckedAdd(entry.due, delta);
    if (!advanced)
    {
        return foundation::Result<GameplayTimePoint>::Failure(foundation::Error::Create(
            "gameplay.time_overflow", "recurring schedule next due time overflows gameplay time"));
    }
    return foundation::Result<GameplayTimePoint>::Success(*advanced);
}

foundation::Result<std::vector<ScheduledTrigger>> GameplayTimeService::CollectDue(ClockId clock, SchedulerBudget budget)
{
    const auto state = clocks_.find(clock);
    const auto index = schedule_index_.find(clock);
    if (state == clocks_.end() || index == schedule_index_.end())
        return foundation::Result<std::vector<ScheduledTrigger>>::Failure(
            foundation::Error::Create("gameplay.clock_unknown", "cannot collect schedules for unknown clock"));
    if (budget.max_triggers == 0 || budget.max_catch_up_occurrences == 0)
        return foundation::Result<std::vector<ScheduledTrigger>>::Failure(
            foundation::Error::Create("gameplay.schedule_invalid_budget", "scheduler budgets must be non-zero"));
    auto staged_schedules = schedules_;
    auto staged_index = schedule_index_;
    auto staged_scheduler_revision = scheduler_revision_;
    auto staged_emitted = emitted_triggers_, staged_catchup = catch_up_occurrences_,
         staged_budget_exhaustions = budget_exhaustions_;
    std::vector<ScheduledTrigger> triggers;
    triggers.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(budget.max_triggers, staged_schedules.size())));
    std::uint64_t materialized_catch_up = 0;
    bool mutated = false;
    auto sat = [](std::uint64_t &target, std::uint64_t value) {
        if (value > std::numeric_limits<std::uint64_t>::max() - target)
            target = std::numeric_limits<std::uint64_t>::max();
        else
            target += value;
    };
    auto &staged_clock_index = staged_index.at(clock);
    while (!staged_clock_index.empty())
    {
        const auto key = *staged_clock_index.begin();
        if (key.due > state->second.now)
            break;
        if (triggers.size() >= budget.max_triggers)
        {
            sat(staged_budget_exhaustions, 1);
            break;
        }
        auto found_entry = staged_schedules.find(key.id);
        if (found_entry == staged_schedules.end())
        {
            staged_clock_index.erase(staged_clock_index.begin());
            mutated = true;
            continue;
        }
        const ScheduleEntry entry = found_entry->second;
        const auto occurrence_result = CalculateOccurrences(entry, state->second.now);
        if (!occurrence_result)
            return foundation::Result<std::vector<ScheduledTrigger>>::Failure(occurrence_result.GetError());
        const auto occurrences = occurrence_result.Value();
        if (occurrences == 0)
            break;
        sat(staged_catchup, occurrences > 1 ? occurrences - 1 : 0);
        const auto next_entry_revision = CheckedNext(found_entry->second.revision);
        if (entry.recurrence.kind != RecurrenceKind::Once && !next_entry_revision)
            return foundation::Result<std::vector<ScheduledTrigger>>::Failure(
                foundation::Error::Create("gameplay.schedule_revision_exhausted", "schedule revision is exhausted"));
        if (entry.recurrence.kind == RecurrenceKind::Once)
        {
            staged_clock_index.erase(ScheduleKey{entry.due, entry.id});
            triggers.push_back({entry.id, entry.clock, entry.owner, entry.action, entry.due, state->second.now, 1});
            staged_schedules.erase(found_entry);
            mutated = true;
            continue;
        }
        auto reindex = [&](GameplayTimePoint next_due) {
            staged_clock_index.insert(ScheduleKey{next_due, entry.id});
            staged_clock_index.erase(ScheduleKey{entry.due, entry.id});
            auto &e = staged_schedules.at(entry.id);
            e.due = next_due;
            e.revision = *next_entry_revision;
            mutated = true;
        };
        switch (entry.catch_up)
        {
        case CatchUpPolicy::FireEach: {
            auto trigger_room = budget.max_triggers - static_cast<std::uint64_t>(triggers.size());
            auto catchup_room = budget.max_catch_up_occurrences - materialized_catch_up;
            auto to_fire = std::min<std::uint64_t>(occurrences, std::min(trigger_room, catchup_room));
            if (to_fire == 0)
            {
                sat(staged_budget_exhaustions, 1);
                break;
            }
            std::vector<GameplayTimePoint> due_times;
            due_times.reserve(static_cast<std::size_t>(to_fire));
            for (std::uint64_t i = 0; i < to_fire; ++i)
            {
                auto due = AdvanceDue(entry, i);
                if (!due)
                    return foundation::Result<std::vector<ScheduledTrigger>>::Failure(due.GetError());
                due_times.push_back(due.Value());
            }
            auto next_due = AdvanceDue(entry, to_fire);
            if (!next_due)
                return foundation::Result<std::vector<ScheduledTrigger>>::Failure(next_due.GetError());
            for (auto due : due_times)
                triggers.push_back({entry.id, entry.clock, entry.owner, entry.action, due, state->second.now, 1});
            reindex(next_due.Value());
            materialized_catch_up += to_fire;
            if (to_fire < occurrences)
                sat(staged_budget_exhaustions, 1);
            break;
        }
        case CatchUpPolicy::FireOnce: {
            auto next_due = AdvanceDue(entry, occurrences);
            if (!next_due)
                return foundation::Result<std::vector<ScheduledTrigger>>::Failure(next_due.GetError());
            triggers.push_back({entry.id, entry.clock, entry.owner, entry.action, entry.due, state->second.now, 1});
            reindex(next_due.Value());
            break;
        }
        case CatchUpPolicy::SkipMissed: {
            auto next_due = AdvanceDue(entry, occurrences);
            if (!next_due)
                return foundation::Result<std::vector<ScheduledTrigger>>::Failure(next_due.GetError());
            if (occurrences == 1 && entry.due == state->second.now)
                triggers.push_back({entry.id, entry.clock, entry.owner, entry.action, entry.due, state->second.now, 1});
            reindex(next_due.Value());
            break;
        }
        case CatchUpPolicy::Aggregate: {
            auto next_due = AdvanceDue(entry, occurrences);
            if (!next_due)
                return foundation::Result<std::vector<ScheduledTrigger>>::Failure(next_due.GetError());
            triggers.push_back(
                {entry.id, entry.clock, entry.owner, entry.action, entry.due, state->second.now, occurrences});
            reindex(next_due.Value());
            break;
        }
        }
        if (entry.catch_up == CatchUpPolicy::FireEach && materialized_catch_up >= budget.max_catch_up_occurrences)
            break;
    }
    if (mutated)
    {
        const auto next_revision = CheckedNext(staged_scheduler_revision);
        if (!next_revision)
            return foundation::Result<std::vector<ScheduledTrigger>>::Failure(
                foundation::Error::Create("gameplay.schedule_revision_exhausted", "scheduler revision is exhausted"));
        staged_scheduler_revision = *next_revision;
    }
    sat(staged_emitted, triggers.size());
    schedules_.swap(staged_schedules);
    schedule_index_.swap(staged_index);
    scheduler_revision_ = staged_scheduler_revision;
    emitted_triggers_ = staged_emitted;
    catch_up_occurrences_ = staged_catchup;
    budget_exhaustions_ = staged_budget_exhaustions;
    return foundation::Result<std::vector<ScheduledTrigger>>::Success(std::move(triggers));
}

GameplayTimeSnapshot GameplayTimeService::CaptureSnapshot() const
{
    GameplayTimeSnapshot snapshot;
    snapshot.clocks.reserve(clocks_.size());
    for (const auto &[_, clock] : clocks_)
    {
        snapshot.clocks.push_back(clock);
    }
    std::sort(snapshot.clocks.begin(), snapshot.clocks.end(),
              [](const ClockState &left, const ClockState &right) { return left.id.Raw() < right.id.Raw(); });

    for (const auto &[_, schedule] : schedules_)
    {
        if (schedule.persistence == SchedulePersistence::Persistent)
        {
            snapshot.schedules.push_back(schedule);
        }
    }
    std::sort(snapshot.schedules.begin(), snapshot.schedules.end(),
              [](const ScheduleEntry &left, const ScheduleEntry &right) { return left.id < right.id; });

    std::set<ClockId> synchronized_clocks = synchronization_rebind_required_;
    for (const auto &[clock, _] : synchronization_revisions_)
    {
        synchronized_clocks.insert(clock);
    }
    snapshot.externally_synchronized_clocks.assign(synchronized_clocks.begin(), synchronized_clocks.end());

    snapshot.schedule_ids = schedule_ids_.GetSnapshot();
    snapshot.scheduler_revision = scheduler_revision_;
    return snapshot;
}

foundation::Result<void> GameplayTimeService::RestoreSnapshot(GameplayTimeSnapshot snapshot)
{
    if (!MonotonicIdGenerator<ScheduleId>::IsValidSnapshot(snapshot.schedule_ids) ||
        snapshot.schedule_ids.scope != schedule_ids_.GetSnapshot().scope)
    {
        return foundation::Result<void>::Failure(foundation::Error::Create(
            "gameplay.schedule_snapshot_invalid", "snapshot contains an invalid schedule id generator"));
    }

    std::unordered_map<ClockId, ClockState> restored_clocks;
    restored_clocks.reserve(clock_definitions_.size());
    for (const auto &[id, _] : clock_definitions_)
    {
        restored_clocks.emplace(id, ClockState{id});
    }
    std::unordered_set<ClockId> seen_clocks;
    for (const auto &clock : snapshot.clocks)
    {
        if (!clock.id.IsValid() || !clock_definitions_.contains(clock.id) || !seen_clocks.insert(clock.id).second ||
            clock.fractional_milli >= 1000)
        {
            return foundation::Result<void>::Failure(foundation::Error::Create(
                "gameplay.clock_snapshot_invalid", "snapshot contains an invalid or duplicate clock"));
        }
        restored_clocks[clock.id] = clock;
    }

    std::set<ClockId> restored_synchronization_rebind_required;
    for (const auto clock : snapshot.externally_synchronized_clocks)
    {
        if (!clock.IsValid() || !clock_definitions_.contains(clock) ||
            !restored_synchronization_rebind_required.insert(clock).second)
        {
            return foundation::Result<void>::Failure(foundation::Error::Create(
                "gameplay.clock_snapshot_invalid", "snapshot contains an invalid or duplicate "
                                                   "externally synchronized clock"));
        }
    }

    std::unordered_map<ScheduleId, ScheduleEntry> restored_schedules;
    std::unordered_map<ClockId, std::set<ScheduleKey>> restored_index;
    for (const auto &[id, _] : clock_definitions_)
    {
        restored_index.emplace(id, std::set<ScheduleKey>{});
    }
    std::set<ScheduleId> seen_schedules;
    std::uint64_t max_schedule_low = 0;
    for (const auto &schedule : snapshot.schedules)
    {
        const auto action = action_types_.find(schedule.action);
        if (!schedule.id.IsValid() || !seen_schedules.insert(schedule.id).second ||
            !clock_definitions_.contains(schedule.clock) || action == action_types_.end() ||
            !schedule.owner.IsValid() || schedule.owner.domain != action->second.owner_domain ||
            schedule.persistence != SchedulePersistence::Persistent)
        {
            return foundation::Result<void>::Failure(foundation::Error::Create(
                "gameplay.schedule_snapshot_invalid", "snapshot contains an invalid or duplicate persistent schedule"));
        }
        const auto recurrence = ValidateRecurrence(schedule.clock, schedule.recurrence);
        if (!recurrence)
        {
            return foundation::Result<void>::Failure(recurrence.GetError());
        }
        if (schedule.id.High() == snapshot.schedule_ids.scope)
        {
            max_schedule_low = std::max(max_schedule_low, schedule.id.Low());
        }
        if (!schedule.revision.value)
            return foundation::Result<void>::Failure(foundation::Error::Create(
                "gameplay.schedule_snapshot_invalid", "persistent schedule revision must be non-zero"));
        restored_schedules.emplace(schedule.id, schedule);
        restored_index[schedule.clock].insert(ScheduleKey{schedule.due, schedule.id});
    }
    if (snapshot.schedule_ids.next != 0 && snapshot.schedule_ids.next <= max_schedule_low)
    {
        return foundation::Result<void>::Failure(foundation::Error::Create(
            "gameplay.schedule_snapshot_invalid", "schedule id generator is behind restored schedules"));
    }

    clocks_ = std::move(restored_clocks);
    schedules_ = std::move(restored_schedules);
    schedule_index_ = std::move(restored_index);
    (void)schedule_ids_.Restore(snapshot.schedule_ids);
    scheduler_revision_ = snapshot.scheduler_revision;
    synchronization_revisions_.clear();
    synchronization_rebind_required_ = std::move(restored_synchronization_rebind_required);
    return foundation::Result<void>::Success();
}

GameplayTimeDiagnostics GameplayTimeService::GetDiagnostics() const noexcept
{
    std::uint64_t persistent = 0;
    for (const auto &[_, schedule] : schedules_)
    {
        if (schedule.persistence == SchedulePersistence::Persistent)
        {
            ++persistent;
        }
    }
    return GameplayTimeDiagnostics{schedules_.size(),     persistent,         emitted_triggers_, cancelled_schedules_,
                                   catch_up_occurrences_, budget_exhaustions_};
}
} // namespace epidemic::gameplay::time
