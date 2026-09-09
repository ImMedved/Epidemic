#include "Epidemic/GameFramework/Time/gameplay_time.h"

using namespace epidemic;
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::time;

int main()
{
    GameplayTimeService service;
    const auto domain = GameplayDomainId::FromString("framework.test");
    const auto clock = service.RegisterClock("framework.clock.world", CalendarDefinition{});
    const auto action = service.RegisterAction("framework.test.tick", domain);
    if (!clock || !action)
    {
        return 1;
    }
    service.Freeze();

    if (!service.SynchronizeClock(clock.Value(), GameplayTimePoint{0}, Revision{1}))
    {
        return 2;
    }
    const GameplayObjectRef owner{domain, GameplayObjectId::FromString("test.owner")};
    RecurrenceRule recurrence;
    recurrence.kind = RecurrenceKind::FixedInterval;
    recurrence.interval = GameplayDuration{10};
    const auto schedule = service.Schedule(
        clock.Value(), GameplayTimePoint{10}, owner, action.Value(), recurrence, CatchUpPolicy::Aggregate, SchedulePersistence::Persistent);
    if (!schedule)
    {
        return 3;
    }

    if (!service.SynchronizeClock(clock.Value(), GameplayTimePoint{35}, Revision{2}))
    {
        return 4;
    }
    const auto due = service.CollectDue(clock.Value());
    if (!due || due.Value().size() != 1 || due.Value().front().occurrence_count != 3)
    {
        return 5;
    }
    const auto recurring = service.GetSchedule(schedule.Value());
    if (!recurring || recurring->due.ticks != 40)
    {
        return 6;
    }

    const auto calendar_schedule = service.ScheduleCalendar(
        clock.Value(), owner, action.Value(), CalendarPattern{1, 0, 1, 0}, CatchUpPolicy::FireOnce, SchedulePersistence::Persistent);
    if (!calendar_schedule)
    {
        return 7;
    }

    const auto snapshot = service.CaptureSnapshot();
    if (snapshot.schedules.size() != 2)
    {
        return 8;
    }
    if (!service.Cancel(schedule.Value()))
    {
        return 9;
    }
    if (!service.RestoreSnapshot(snapshot) || !service.HasSchedule(schedule.Value()))
    {
        return 10;
    }

    const auto stale_sync = service.SynchronizeClock(clock.Value(), GameplayTimePoint{34}, Revision{3});
    if (stale_sync || !stale_sync.GetError().HasCode("gameplay.clock_rewind"))
    {
        return 11;
    }

    // Catch-up policies are explicit so large time jumps do not force frame-by-frame replay.
    GameplayTimeService policy_service;
    const auto policy_clock = policy_service.RegisterClock("framework.clock.policy", CalendarDefinition{});
    const auto policy_action = policy_service.RegisterAction("framework.test.policy", domain);
    if (!policy_clock || !policy_action)
    {
        return 12;
    }
    policy_service.Freeze();
    if (!policy_service.SynchronizeClock(policy_clock.Value(), GameplayTimePoint{0}, Revision{1}))
    {
        return 13;
    }

    RecurrenceRule each_rule;
    each_rule.kind = RecurrenceKind::FixedInterval;
    each_rule.interval = GameplayDuration{5};
    const GameplayObjectRef owner_each{domain, GameplayObjectId::FromString("test.owner.each")};
    const auto each = policy_service.Schedule(policy_clock.Value(), GameplayTimePoint{40}, owner_each, policy_action.Value(), each_rule, CatchUpPolicy::FireEach);
    if (!each || !policy_service.SynchronizeClock(policy_clock.Value(), GameplayTimePoint{60}, Revision{2}))
    {
        return 14;
    }
    const auto each_due = policy_service.CollectDue(policy_clock.Value(), SchedulerBudget{3, 100});
    if (!each_due || each_due.Value().size() != 3)
    {
        return 15;
    }
    const auto each_remaining = policy_service.GetSchedule(each.Value());
    if (!each_remaining || each_remaining->due.ticks != 55)
    {
        return 16;
    }
    const auto each_rest = policy_service.CollectDue(policy_clock.Value(), SchedulerBudget{10, 100});
    if (!each_rest || each_rest.Value().size() != 2 || (!policy_service.GetSchedule(each.Value()) || policy_service.GetSchedule(each.Value())->due.ticks != 65))
    {
        return 17;
    }

    const GameplayObjectRef owner_skip{domain, GameplayObjectId::FromString("test.owner.skip")};
    const auto skipped = policy_service.Schedule(policy_clock.Value(), GameplayTimePoint{50}, owner_skip, policy_action.Value(), each_rule, CatchUpPolicy::SkipMissed);
    if (!skipped)
    {
        return 18;
    }
    const auto skip_due = policy_service.CollectDue(policy_clock.Value());
    if (!skip_due || !skip_due.Value().empty() || (!policy_service.GetSchedule(skipped.Value()) || policy_service.GetSchedule(skipped.Value())->due.ticks != 65))
    {
        return 19;
    }

    const GameplayObjectRef owner_once{domain, GameplayObjectId::FromString("test.owner.once")};
    const auto once = policy_service.Schedule(policy_clock.Value(), GameplayTimePoint{55}, owner_once, policy_action.Value(), each_rule, CatchUpPolicy::FireOnce);
    if (!once)
    {
        return 20;
    }
    const auto once_due = policy_service.CollectDue(policy_clock.Value());
    if (!once_due || once_due.Value().size() != 1 || (!policy_service.GetSchedule(once.Value()) || policy_service.GetSchedule(once.Value())->due.ticks != 65))
    {
        return 21;
    }

    // Fractional time scales keep their sub-tick remainder across advances.
    GameplayTimeService fractional_service;
    const auto fractional_clock = fractional_service.RegisterClock("framework.clock.fractional", CalendarDefinition{});
    if (!fractional_clock)
    {
        return 22;
    }
    fractional_service.Freeze();
    if (!fractional_service.SetTimeScale(fractional_clock.Value(), 500))
    {
        return 23;
    }
    for (int i = 0; i < 1000; ++i)
    {
        if (!fractional_service.AdvanceClock(fractional_clock.Value(), GameplayDuration{1}))
        {
            return 24;
        }
    }
    const auto fractional_state = fractional_service.GetClock(fractional_clock.Value());
    if (!fractional_state || fractional_state->now.ticks != 500 || fractional_state->fractional_milli != 0)
    {
        return 25;
    }

    // Analytical catch-up is O(1) and is not rejected by the FireEach materialization budget.
    GameplayTimeService aggregate_service;
    const auto aggregate_clock = aggregate_service.RegisterClock("framework.clock.aggregate", CalendarDefinition{});
    const auto aggregate_action = aggregate_service.RegisterAction("framework.test.aggregate", domain);
    if (!aggregate_clock || !aggregate_action)
    {
        return 26;
    }
    aggregate_service.Freeze();
    RecurrenceRule aggregate_rule;
    aggregate_rule.kind = RecurrenceKind::FixedInterval;
    aggregate_rule.interval = GameplayDuration{1};
    const auto aggregate_schedule = aggregate_service.Schedule(
        aggregate_clock.Value(), GameplayTimePoint{1}, owner, aggregate_action.Value(), aggregate_rule, CatchUpPolicy::Aggregate);
    if (!aggregate_schedule || !aggregate_service.AdvanceTo(aggregate_clock.Value(), GameplayTimePoint{10'000'000}))
    {
        return 27;
    }
    const auto aggregate_due = aggregate_service.CollectDue(aggregate_clock.Value(), SchedulerBudget{1, 8});
    if (!aggregate_due || aggregate_due.Value().size() != 1 || aggregate_due.Value().front().occurrence_count != 10'000'000)
    {
        return 28;
    }

    // Calendar round-trips use the registered fixed calendar dimensions.
    GameplayTimeService calendar_service;
    CalendarDefinition custom_calendar;
    custom_calendar.hours_per_day = 20;
    custom_calendar.days_per_month = 10;
    custom_calendar.months_per_year = 8;
    const auto custom_clock = calendar_service.RegisterClock("framework.clock.calendar", custom_calendar);
    if (!custom_clock)
    {
        return 29;
    }
    calendar_service.Freeze();
    const CalendarDate expected_date{3, 4, 7, 12, 34, 56};
    const auto as_time = calendar_service.ToGameplayTime(custom_clock.Value(), expected_date);
    const auto round_trip = as_time ? calendar_service.ToCalendarDate(custom_clock.Value(), as_time.Value())
                                    : foundation::Result<CalendarDate>::Failure(as_time.GetError());
    if (!as_time || !round_trip || round_trip.Value() != expected_date)
    {
        return 30;
    }
    if (calendar_service.ValidateCalendarDate(custom_clock.Value(), CalendarDate{1, 9, 1, 0, 0, 0}))
    {
        return 31;
    }
    if (kGameplayTicksPerSecond != 1 || kGameplayTicksPerMinute != 60 || kGameplayTicksPerHour != 3600)
    {
        return 32;
    }

    // External synchronization revisions are transient. The snapshot preserves only
    // which clocks require a synchronization source to be rebound after restore.
    const auto synchronized_snapshot = service.CaptureSnapshot();
    if (synchronized_snapshot.externally_synchronized_clocks.size() != 1 ||
        synchronized_snapshot.externally_synchronized_clocks.front() != clock.Value())
    {
        return 33;
    }
    if (!service.RestoreSnapshot(synchronized_snapshot) || !service.NeedsSynchronizationRebind(clock.Value()))
    {
        return 34;
    }
    const auto rebind_clocks = service.ClocksRequiringSynchronizationRebind();
    if (rebind_clocks.size() != 1 || rebind_clocks.front() != clock.Value())
    {
        return 35;
    }
    if (!service.SynchronizeClock(clock.Value(), service.GetClock(clock.Value())->now, Revision{1}) ||
        service.NeedsSynchronizationRebind(clock.Value()))
    {
        return 36;
    }

    // Corrupt restore is atomic and a generator behind restored IDs is rejected.
    const auto good_snapshot = service.CaptureSnapshot();
    auto duplicate_snapshot = good_snapshot;
    duplicate_snapshot.schedules.push_back(duplicate_snapshot.schedules.front());
    if (service.RestoreSnapshot(duplicate_snapshot) || !service.HasSchedule(schedule.Value()))
    {
        return 37;
    }
    auto behind_snapshot = good_snapshot;
    if (!behind_snapshot.schedules.empty())
    {
        behind_snapshot.schedule_ids.next = behind_snapshot.schedules.front().id.Low();
        if (behind_snapshot.schedule_ids.next == 0)
        {
            behind_snapshot.schedule_ids.next = 1;
        }
        if (service.RestoreSnapshot(behind_snapshot))
        {
            return 38;
        }
    }

    auto invalid_sync_snapshot = good_snapshot;
    invalid_sync_snapshot.externally_synchronized_clocks = {clock.Value(), clock.Value()};
    if (service.RestoreSnapshot(invalid_sync_snapshot) || !service.HasSchedule(schedule.Value()))
    {
        return 39;
    }

    return 0;
}
