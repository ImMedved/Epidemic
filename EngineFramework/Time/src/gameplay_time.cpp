#include "Epidemic/GameFramework/Time/gameplay_time.h"

#include <algorithm>
#include <limits>

namespace epidemic::gameplay::time
{
namespace
{
constexpr std::int64_t kSecondsPerMinute = 60;
constexpr std::int64_t kSecondsPerHour = 3600;

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

foundation::Result<ClockId> GameplayTimeService::RegisterClock(
    std::string_view canonical_name,
    std::optional<CalendarDefinition> calendar)
{
    if (frozen_)
    {
        return foundation::Result<ClockId>::Failure(
            foundation::Error::Create("gameplay.registry_frozen", "gameplay clock registry is frozen", std::string(canonical_name)));
    }
    if (canonical_name.empty())
    {
        return foundation::Result<ClockId>::Failure(
            foundation::Error::Create("gameplay.invalid_clock", "clock canonical name must not be empty"));
    }
    if (calendar.has_value() &&
        (calendar->hours_per_day == 0 || calendar->days_per_month == 0 || calendar->months_per_year == 0))
    {
        return foundation::Result<ClockId>::Failure(
            foundation::Error::Create("gameplay.invalid_calendar", "calendar units must be non-zero"));
    }

    const ClockId id = ClockId::FromString(canonical_name);
    const auto found = clock_definitions_.find(id);
    if (found != clock_definitions_.end())
    {
        if (found->second.canonical_name != canonical_name)
        {
            return foundation::Result<ClockId>::Failure(
                foundation::Error::Create("gameplay.id_collision", "clock id collision", std::string(canonical_name)));
        }
        return foundation::Result<ClockId>::Failure(
            foundation::Error::Create("gameplay.already_registered", "clock already registered", std::string(canonical_name)));
    }

    clock_definitions_.emplace(id, ClockDefinition{id, std::string(canonical_name), calendar});
    clocks_.emplace(id, ClockState{id, {}, {}});
    schedule_index_.emplace(id, std::set<ScheduleKey>{});
    return foundation::Result<ClockId>::Success(id);
}

foundation::Result<ActionTypeId> GameplayTimeService::RegisterAction(std::string_view canonical_name, GameplayDomainId owner_domain)
{
    if (frozen_)
    {
        return foundation::Result<ActionTypeId>::Failure(
            foundation::Error::Create("gameplay.registry_frozen", "schedule action registry is frozen", std::string(canonical_name)));
    }
    if (canonical_name.empty() || !owner_domain.IsValid())
    {
        return foundation::Result<ActionTypeId>::Failure(
            foundation::Error::Create("gameplay.invalid_action", "action canonical name and owner domain must be valid"));
    }

    const ActionTypeId id = ActionTypeId::FromString(canonical_name);
    const auto found = action_types_.find(id);
    if (found != action_types_.end())
    {
        if (found->second.canonical_name != canonical_name)
        {
            return foundation::Result<ActionTypeId>::Failure(
                foundation::Error::Create("gameplay.id_collision", "action type id collision", std::string(canonical_name)));
        }
        return foundation::Result<ActionTypeId>::Failure(
            foundation::Error::Create("gameplay.already_registered", "schedule action already registered", std::string(canonical_name)));
    }

    action_types_.emplace(id, ActionTypeInfo{id, std::string(canonical_name), owner_domain});
    return foundation::Result<ActionTypeId>::Success(id);
}

foundation::Result<void> GameplayTimeService::SynchronizeClock(ClockId clock, GameplayTimePoint now, Revision source_revision)
{
    const auto found = clocks_.find(clock);
    if (found == clocks_.end())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.clock_unknown", "gameplay clock is not registered"));
    }
    if (now < found->second.now)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.clock_rewind", "authoritative gameplay clock cannot move backwards outside restore"));
    }
    if (source_revision < found->second.source_revision)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.clock_stale_revision", "clock synchronization revision is stale"));
    }

    found->second.now = now;
    found->second.source_revision = source_revision;
    return foundation::Result<void>::Success();
}

const ClockState* GameplayTimeService::FindClock(ClockId clock) const noexcept
{
    const auto found = clocks_.find(clock);
    return found == clocks_.end() ? nullptr : &found->second;
}

const ClockDefinition* GameplayTimeService::FindClockDefinition(ClockId clock) const noexcept
{
    const auto found = clock_definitions_.find(clock);
    return found == clock_definitions_.end() ? nullptr : &found->second;
}

foundation::Result<void> GameplayTimeService::ValidateRecurrence(ClockId clock, const RecurrenceRule& recurrence) const
{
    switch (recurrence.kind)
    {
    case RecurrenceKind::Once:
        return foundation::Result<void>::Success();
    case RecurrenceKind::FixedInterval:
        if (recurrence.interval.ticks <= 0)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.schedule_invalid_interval", "fixed recurrence interval must be positive"));
        }
        return foundation::Result<void>::Success();
    case RecurrenceKind::CalendarPattern:
    {
        const auto* definition = FindClockDefinition(clock);
        if (definition == nullptr || !definition->calendar.has_value())
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.schedule_calendar_unavailable", "calendar recurrence requires a clock with calendar definition"));
        }
        const auto& calendar = *definition->calendar;
        if (recurrence.calendar.every_days == 0 || recurrence.calendar.hour >= calendar.hours_per_day ||
            recurrence.calendar.minute >= 60 || recurrence.calendar.second >= 60)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.schedule_invalid_calendar_pattern", "calendar recurrence pattern is invalid"));
        }
        return foundation::Result<void>::Success();
    }
    }
    return foundation::Result<void>::Failure(
        foundation::Error::Create("gameplay.schedule_invalid_recurrence", "unknown recurrence kind"));
}

foundation::Result<ScheduleId> GameplayTimeService::Schedule(
    ClockId clock,
    GameplayTimePoint due,
    GameplayObjectRef owner,
    ActionTypeId action,
    RecurrenceRule recurrence,
    CatchUpPolicy catch_up,
    SchedulePersistence persistence)
{
    if (!clock_definitions_.contains(clock))
    {
        return foundation::Result<ScheduleId>::Failure(foundation::Error::Create("gameplay.clock_unknown", "schedule clock is not registered"));
    }
    const auto action_info = action_types_.find(action);
    if (action_info == action_types_.end())
    {
        return foundation::Result<ScheduleId>::Failure(foundation::Error::Create("gameplay.action_unknown", "schedule action is not registered"));
    }
    if (!owner.IsValid() || owner.domain != action_info->second.owner_domain)
    {
        return foundation::Result<ScheduleId>::Failure(
            foundation::Error::Create("gameplay.schedule_owner_mismatch", "schedule owner must be valid and match action owner domain"));
    }
    const auto recurrence_result = ValidateRecurrence(clock, recurrence);
    if (!recurrence_result)
    {
        return foundation::Result<ScheduleId>::Failure(recurrence_result.GetError());
    }

    const ScheduleId id = schedule_ids_.Next();
    if (!id.IsValid())
    {
        return foundation::Result<ScheduleId>::Failure(
            foundation::Error::Create("gameplay.schedule_id_exhausted", "schedule id generator is exhausted"));
    }

    ScheduleEntry entry{id, clock, due, owner, action, recurrence, catch_up, persistence};
    schedules_.emplace(id, entry);
    InsertScheduleIndex(entry);
    return foundation::Result<ScheduleId>::Success(id);
}

foundation::Result<GameplayTimePoint> GameplayTimeService::NextCalendarDue(ClockId clock, CalendarPattern pattern) const
{
    const auto* state = FindClock(clock);
    const auto* definition = FindClockDefinition(clock);
    if (state == nullptr || definition == nullptr || !definition->calendar.has_value())
    {
        return foundation::Result<GameplayTimePoint>::Failure(
            foundation::Error::Create("gameplay.schedule_calendar_unavailable", "calendar scheduling requires synchronized clock and calendar"));
    }
    const auto& calendar = *definition->calendar;
    if (pattern.every_days == 0 || pattern.hour >= calendar.hours_per_day || pattern.minute >= 60 || pattern.second >= 60)
    {
        return foundation::Result<GameplayTimePoint>::Failure(
            foundation::Error::Create("gameplay.schedule_invalid_calendar_pattern", "calendar schedule pattern is invalid"));
    }

    const auto seconds_per_day = CheckedMultiply(static_cast<std::int64_t>(calendar.hours_per_day), kSecondsPerHour);
    if (!seconds_per_day)
    {
        return foundation::Result<GameplayTimePoint>::Failure(
            foundation::Error::Create("gameplay.time_overflow", "calendar day length overflows gameplay time"));
    }
    const auto target_second = static_cast<std::int64_t>(pattern.hour) * kSecondsPerHour +
                               static_cast<std::int64_t>(pattern.minute) * kSecondsPerMinute + pattern.second;
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

foundation::Result<ScheduleId> GameplayTimeService::ScheduleCalendar(
    ClockId clock,
    GameplayObjectRef owner,
    ActionTypeId action,
    CalendarPattern pattern,
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

void GameplayTimeService::InsertScheduleIndex(const ScheduleEntry& entry)
{
    schedule_index_[entry.clock].insert(ScheduleKey{entry.due, entry.id});
}

void GameplayTimeService::RemoveScheduleIndex(const ScheduleEntry& entry)
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
    {
        return foundation::Result<void>::Failure(foundation::Error::Create("gameplay.schedule_unknown", "schedule does not exist"));
    }
    RemoveScheduleIndex(found->second);
    schedules_.erase(found);
    ++cancelled_schedules_;
    return foundation::Result<void>::Success();
}

std::uint64_t GameplayTimeService::CancelOwnedBy(GameplayObjectRef owner)
{
    std::vector<ScheduleId> ids;
    for (const auto& [id, entry] : schedules_)
    {
        if (entry.owner == owner)
        {
            ids.push_back(id);
        }
    }
    for (const auto id : ids)
    {
        [[maybe_unused]] const auto result = Cancel(id);
    }
    return ids.size();
}

foundation::Result<void> GameplayTimeService::Reschedule(ScheduleId schedule, GameplayTimePoint new_due)
{
    const auto found = schedules_.find(schedule);
    if (found == schedules_.end())
    {
        return foundation::Result<void>::Failure(foundation::Error::Create("gameplay.schedule_unknown", "schedule does not exist"));
    }
    RemoveScheduleIndex(found->second);
    found->second.due = new_due;
    InsertScheduleIndex(found->second);
    return foundation::Result<void>::Success();
}

bool GameplayTimeService::HasSchedule(ScheduleId schedule) const noexcept
{
    return schedules_.contains(schedule);
}

const ScheduleEntry* GameplayTimeService::FindSchedule(ScheduleId schedule) const noexcept
{
    const auto found = schedules_.find(schedule);
    return found == schedules_.end() ? nullptr : &found->second;
}

std::uint64_t GameplayTimeService::CalculateOccurrences(const ScheduleEntry& entry, GameplayTimePoint now) const noexcept
{
    if (entry.due > now)
    {
        return 0;
    }
    if (entry.recurrence.kind == RecurrenceKind::Once)
    {
        return 1;
    }

    std::int64_t interval = entry.recurrence.interval.ticks;
    if (entry.recurrence.kind == RecurrenceKind::CalendarPattern)
    {
        const auto* definition = FindClockDefinition(entry.clock);
        if (definition == nullptr || !definition->calendar.has_value())
        {
            return 1;
        }
        const auto seconds_per_day = static_cast<std::int64_t>(definition->calendar->hours_per_day) * kSecondsPerHour;
        interval = seconds_per_day * static_cast<std::int64_t>(entry.recurrence.calendar.every_days);
    }
    if (interval <= 0)
    {
        return 1;
    }
    const auto elapsed = now.ticks - entry.due.ticks;
    return static_cast<std::uint64_t>(elapsed / interval) + 1;
}

GameplayTimePoint GameplayTimeService::AdvanceDue(const ScheduleEntry& entry, std::uint64_t occurrences) const noexcept
{
    if (entry.recurrence.kind == RecurrenceKind::Once || occurrences == 0)
    {
        return entry.due;
    }
    std::int64_t interval = entry.recurrence.interval.ticks;
    if (entry.recurrence.kind == RecurrenceKind::CalendarPattern)
    {
        const auto* definition = FindClockDefinition(entry.clock);
        if (definition == nullptr || !definition->calendar.has_value())
        {
            return entry.due;
        }
        interval = static_cast<std::int64_t>(definition->calendar->hours_per_day) * kSecondsPerHour *
                   static_cast<std::int64_t>(entry.recurrence.calendar.every_days);
    }
    if (interval <= 0 || occurrences > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() / interval))
    {
        return GameplayTimePoint{std::numeric_limits<std::int64_t>::max()};
    }
    return ::epidemic::gameplay::SaturatingAdd(entry.due, GameplayDuration{interval * static_cast<std::int64_t>(occurrences)});
}

foundation::Result<std::vector<ScheduledTrigger>> GameplayTimeService::CollectDue(ClockId clock, SchedulerBudget budget)
{
    const auto state = clocks_.find(clock);
    const auto index = schedule_index_.find(clock);
    if (state == clocks_.end() || index == schedule_index_.end())
    {
        return foundation::Result<std::vector<ScheduledTrigger>>::Failure(
            foundation::Error::Create("gameplay.clock_unknown", "cannot collect schedules for unknown clock"));
    }
    if (budget.max_triggers == 0 || budget.max_catch_up_occurrences == 0)
    {
        return foundation::Result<std::vector<ScheduledTrigger>>::Failure(
            foundation::Error::Create("gameplay.schedule_invalid_budget", "scheduler budgets must be non-zero"));
    }

    std::vector<ScheduledTrigger> triggers;
    std::uint64_t consumed_occurrences = 0;

    while (!index->second.empty())
    {
        const auto key = *index->second.begin();
        if (key.due > state->second.now)
        {
            break;
        }
        if (triggers.size() >= budget.max_triggers)
        {
            ++budget_exhaustions_;
            return foundation::Result<std::vector<ScheduledTrigger>>::Failure(
                foundation::Error::Create("gameplay.schedule_budget_exceeded", "scheduler trigger budget exhausted"));
        }

        const auto found_entry = schedules_.find(key.id);
        if (found_entry == schedules_.end())
        {
            index->second.erase(index->second.begin());
            continue;
        }

        ScheduleEntry entry = found_entry->second;
        const auto occurrences = CalculateOccurrences(entry, state->second.now);
        if (occurrences == 0)
        {
            break;
        }
        if (occurrences > budget.max_catch_up_occurrences - consumed_occurrences)
        {
            ++budget_exhaustions_;
            return foundation::Result<std::vector<ScheduledTrigger>>::Failure(
                foundation::Error::Create("gameplay.schedule_catchup_budget_exceeded", "scheduler catch-up budget exhausted"));
        }
        consumed_occurrences += occurrences;
        catch_up_occurrences_ += occurrences > 1 ? occurrences - 1 : 0;

        RemoveScheduleIndex(found_entry->second);

        if (entry.recurrence.kind == RecurrenceKind::Once)
        {
            triggers.push_back(ScheduledTrigger{entry.id, entry.clock, entry.owner, entry.action, entry.due, state->second.now, 1});
            schedules_.erase(found_entry);
            continue;
        }

        switch (entry.catch_up)
        {
        case CatchUpPolicy::FireEach:
        {
            const auto remaining_trigger_budget = budget.max_triggers - triggers.size();
            const auto to_fire = std::min<std::uint64_t>(occurrences, remaining_trigger_budget);
            for (std::uint64_t i = 0; i < to_fire; ++i)
            {
                triggers.push_back(ScheduledTrigger{entry.id,
                                                     entry.clock,
                                                     entry.owner,
                                                     entry.action,
                                                     AdvanceDue(entry, i),
                                                     state->second.now,
                                                     1});
            }
            found_entry->second.due = AdvanceDue(entry, to_fire);
            InsertScheduleIndex(found_entry->second);
            if (to_fire < occurrences)
            {
                ++budget_exhaustions_;
                emitted_triggers_ += triggers.size();
                return foundation::Result<std::vector<ScheduledTrigger>>::Success(std::move(triggers));
            }
            break;
        }
        case CatchUpPolicy::FireOnce:
            triggers.push_back(ScheduledTrigger{entry.id, entry.clock, entry.owner, entry.action, entry.due, state->second.now, 1});
            found_entry->second.due = AdvanceDue(entry, occurrences);
            InsertScheduleIndex(found_entry->second);
            break;
        case CatchUpPolicy::SkipMissed:
            if (occurrences == 1 && entry.due == state->second.now)
            {
                triggers.push_back(ScheduledTrigger{entry.id, entry.clock, entry.owner, entry.action, entry.due, state->second.now, 1});
            }
            found_entry->second.due = AdvanceDue(entry, occurrences);
            InsertScheduleIndex(found_entry->second);
            break;
        case CatchUpPolicy::Aggregate:
            triggers.push_back(ScheduledTrigger{entry.id, entry.clock, entry.owner, entry.action, entry.due, state->second.now, occurrences});
            found_entry->second.due = AdvanceDue(entry, occurrences);
            InsertScheduleIndex(found_entry->second);
            break;
        }
    }

    emitted_triggers_ += triggers.size();
    return foundation::Result<std::vector<ScheduledTrigger>>::Success(std::move(triggers));
}

GameplayTimeSnapshot GameplayTimeService::CaptureSnapshot() const
{
    GameplayTimeSnapshot snapshot;
    snapshot.clocks.reserve(clocks_.size());
    for (const auto& [_, clock] : clocks_)
    {
        snapshot.clocks.push_back(clock);
    }
    std::sort(snapshot.clocks.begin(), snapshot.clocks.end(), [](const ClockState& left, const ClockState& right) {
        return left.id.Raw() < right.id.Raw();
    });

    for (const auto& [_, schedule] : schedules_)
    {
        if (schedule.persistence == SchedulePersistence::Persistent)
        {
            snapshot.schedules.push_back(schedule);
        }
    }
    std::sort(snapshot.schedules.begin(), snapshot.schedules.end(), [](const ScheduleEntry& left, const ScheduleEntry& right) {
        return left.id < right.id;
    });
    snapshot.schedule_ids = schedule_ids_.GetSnapshot();
    return snapshot;
}

foundation::Result<void> GameplayTimeService::RestoreSnapshot(GameplayTimeSnapshot snapshot)
{
    for (const auto& clock : snapshot.clocks)
    {
        if (!clock_definitions_.contains(clock.id))
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.clock_snapshot_invalid", "snapshot contains an unregistered clock"));
        }
    }
    for (const auto& schedule : snapshot.schedules)
    {
        const auto action = action_types_.find(schedule.action);
        if (!clock_definitions_.contains(schedule.clock) || action == action_types_.end() ||
            !schedule.owner.IsValid() || schedule.owner.domain != action->second.owner_domain)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.schedule_snapshot_invalid", "snapshot contains unknown clock/action or invalid owner"));
        }
        const auto recurrence = ValidateRecurrence(schedule.clock, schedule.recurrence);
        if (!recurrence)
        {
            return foundation::Result<void>::Failure(recurrence.GetError());
        }
    }

    for (auto& [_, index] : schedule_index_)
    {
        index.clear();
    }
    schedules_.clear();

    for (const auto& clock : snapshot.clocks)
    {
        clocks_[clock.id] = clock;
    }
    for (auto& schedule : snapshot.schedules)
    {
        schedules_.emplace(schedule.id, schedule);
        InsertScheduleIndex(schedule);
    }
    schedule_ids_.Restore(snapshot.schedule_ids);
    return foundation::Result<void>::Success();
}

GameplayTimeDiagnostics GameplayTimeService::GetDiagnostics() const noexcept
{
    std::uint64_t persistent = 0;
    for (const auto& [_, schedule] : schedules_)
    {
        if (schedule.persistence == SchedulePersistence::Persistent)
        {
            ++persistent;
        }
    }
    return GameplayTimeDiagnostics{schedules_.size(),
                                   persistent,
                                   emitted_triggers_,
                                   cancelled_schedules_,
                                   catch_up_occurrences_,
                                   budget_exhaustions_};
}
} // namespace epidemic::gameplay::time
