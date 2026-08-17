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
    const auto* recurring = service.FindSchedule(schedule.Value());
    if (recurring == nullptr || recurring->due.ticks != 40)
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
    const auto* each_remaining = policy_service.FindSchedule(each.Value());
    if (each_remaining == nullptr || each_remaining->due.ticks != 55)
    {
        return 16;
    }
    const auto each_rest = policy_service.CollectDue(policy_clock.Value(), SchedulerBudget{10, 100});
    if (!each_rest || each_rest.Value().size() != 2 || policy_service.FindSchedule(each.Value())->due.ticks != 65)
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
    if (!skip_due || !skip_due.Value().empty() || policy_service.FindSchedule(skipped.Value())->due.ticks != 65)
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
    if (!once_due || once_due.Value().size() != 1 || policy_service.FindSchedule(once.Value())->due.ticks != 65)
    {
        return 21;
    }

    return 0;
}
