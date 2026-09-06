#include "Epidemic/GameFramework/Integration/core_adapters.h"

#include <memory>
#include <stdexcept>
#include <vector>

using namespace epidemic;
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::facts;
using namespace epidemic::gameplay::integration;
using namespace epidemic::gameplay::queries;
using namespace epidemic::gameplay::time;

namespace
{
class TestSnapshotCoordinator final : public IQuerySnapshotCoordinator
{
  public:
    [[nodiscard]] foundation::Result<QuerySnapshotReadEpoch> AcquireReadEpoch(GameplayTickId) const override
    {
        return foundation::Result<QuerySnapshotReadEpoch>::Success(
            QuerySnapshotReadEpoch{next_++, std::make_shared<int>(1)});
    }

  private:
    mutable std::uint64_t next_ = 1;
};

class FakeGameClock final : public runtime::IGameClock
{
  public:
    runtime::TimeSnapshot snapshot{};
    [[nodiscard]] runtime::GameTimePoint Now() const override { return snapshot.now; }
    [[nodiscard]] runtime::GameDuration LastDelta() const override { return snapshot.last_delta; }
    [[nodiscard]] runtime::TimeSnapshot GetSnapshot() const override { return snapshot; }
};
} // namespace

int main()
{
    GameplayFactsService facts;
    GameplayQueryService queries;
    GameplayTimeService time;

    const auto test_domain = GameplayDomainId::FromString("framework.test");
    const auto fact_type = facts.RegisterFactType<int>("framework.test.value", test_domain);
    const auto clock = time.RegisterClock("framework.clock.world", CalendarDefinition{});
    const auto action = time.RegisterAction("framework.test.scheduled", test_domain);
    const auto second_action = time.RegisterAction("framework.test.second_scheduled", test_domain);
    if (!fact_type || !clock || !action || !second_action)
    {
        return 1;
    }

    auto snapshot_coordinator = std::make_shared<TestSnapshotCoordinator>();
    if (!queries.SetSnapshotCoordinator(snapshot_coordinator))
    {
        return 21;
    }

    FactsQueryAdapter facts_query(facts, queries);
    if (!facts_query.RegisterProviders() || !facts_query.RegisterProviders() || !facts_query.IsRegistered())
    {
        return 2;
    }

    TimeFactsAdapter time_facts(facts);
    const auto due_event = time_facts.RegisterContracts();
    const auto due_event_again = time_facts.RegisterContracts();
    if (!due_event || !due_event_again || due_event.Value() != due_event_again.Value())
    {
        return 3;
    }

    int due_count = 0;
    if (!facts.Subscribe<ScheduledTrigger>(
            due_event.Value(), SubscriberId::FromString("framework.test.due"), 0,
            [&due_count](const EventEnvelope&, const ScheduledTrigger& trigger) {
                due_count += static_cast<int>(trigger.occurrence_count);
            }))
    {
        return 4;
    }

    ScheduledTriggerDispatcher dispatcher(time, ScheduledTriggerDispatcherPolicy{32, 32});
    if (!time_facts.RegisterWithDispatcher(dispatcher) || !time_facts.RegisterWithDispatcher(dispatcher))
    {
        return 41;
    }

    int primary_attempts = 0;
    if (!dispatcher.RegisterActionHandler(
            action.Value(),
            ScheduledTriggerHandlerId::FromString("framework.test.primary_handler"),
            [&primary_attempts](const ScheduledTrigger&, const GameplayContext&) {
                ++primary_attempts;
                if (primary_attempts == 1)
                {
                    return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::Retry);
                }
                return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::Ack);
            }))
    {
        return 42;
    }

    int second_deliveries = 0;
    if (!dispatcher.RegisterActionHandler(
            second_action.Value(),
            ScheduledTriggerHandlerId::FromString("framework.test.second_handler"),
            [&second_deliveries](const ScheduledTrigger&, const GameplayContext&) {
                ++second_deliveries;
                return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::Ack);
            }))
    {
        return 43;
    }

    dispatcher.Freeze();
    if (dispatcher.RegisterObserver(
            ScheduledTriggerHandlerId::FromString("framework.test.late_handler"), 0,
            [](const ScheduledTrigger&, const GameplayContext&) {
                return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::Ack);
            }))
    {
        return 44;
    }

    facts.Freeze();
    queries.Freeze();
    time.Freeze();

    const GameplayObjectRef object{test_domain, GameplayObjectId::FromString("framework.test.object")};
    GameplayContext context;
    context.tick = GameplayTickId{1};
    context.time = GameplayTimePoint{5};
    auto tx = facts.BeginTransaction(test_domain);
    tx.Set<int>(fact_type.Value(), object, {}, 77, FactPersistence::Persistent);
    if (!facts.Commit(std::move(tx), context))
    {
        return 5;
    }

    if (!facts.Dispatch())
    {
        return 51;
    }

    auto facts_snapshot = queries.AcquireSnapshot(QueryContext{});
    if (!facts_snapshot || !facts_snapshot.Value().IsValid())
    {
        return 52;
    }
    FindFactsQuery query{fact_type.Value(), object, {}};
    const auto queried = queries.Execute(query, QueryContext{});
    if (!queried || !queried.Value().value || queried.Value().value->size() != 1 || std::any_cast<int>(queried.Value().value->front().value) != 77)
    {
        return 6;
    }

    auto mutate = facts.BeginTransaction(test_domain);
    mutate.Set<int>(fact_type.Value(), object, {}, 88, FactPersistence::Persistent);
    if (!facts.Commit(std::move(mutate), context))
    {
        return 61;
    }
    const auto current_after_mutation = queries.Execute(query, QueryContext{});
    const auto snapshot_query = queries.Execute(query, facts_snapshot.Value());
    if (!current_after_mutation || !snapshot_query || !current_after_mutation.Value().value || !snapshot_query.Value().value ||
        std::any_cast<int>(current_after_mutation.Value().value->front().value) != 88 ||
        std::any_cast<int>(snapshot_query.Value().value->front().value) != 77)
    {
        return 62;
    }

    FakeGameClock runtime_clock;
    runtime_clock.snapshot.now = runtime::GameTimePoint{5};
    runtime_clock.snapshot.revision = 1;
    RuntimeTimeAdapter runtime_adapter(runtime_clock, time, clock.Value());
    if (!runtime_adapter.Synchronize())
    {
        return 7;
    }

    const auto scheduled = time.Schedule(clock.Value(), GameplayTimePoint{10}, object, action.Value());
    const auto scheduled_second = time.Schedule(clock.Value(), GameplayTimePoint{10}, object, second_action.Value());
    if (!scheduled || !scheduled_second)
    {
        return 8;
    }

    runtime_clock.snapshot.now = runtime::GameTimePoint{10};
    runtime_clock.snapshot.revision = 2;
    if (!runtime_adapter.Synchronize())
    {
        return 9;
    }
    context.tick = GameplayTickId{2};
    context.time = GameplayTimePoint{10};

    const auto first_pump = dispatcher.Pump(clock.Value(), context);
    if (!first_pump || first_pump.Value().collected != 2 || first_pump.Value().pending != 1 || primary_attempts != 1 ||
        second_deliveries != 1)
    {
        return 10;
    }
    if (!facts.Dispatch() || due_count != 2)
    {
        return 11;
    }

    // TimeFacts observer already Acked the primary trigger. Retry must execute only the failed action leg,
    // therefore the scheduled Facts event is not published twice.
    const auto pending_snapshot = dispatcher.CaptureSnapshot();
    if (pending_snapshot.pending.size() != 1 || pending_snapshot.pending.front().completed_handlers.size() != 1)
    {
        return 12;
    }

    ScheduledTriggerDispatcher restored_dispatcher(time, ScheduledTriggerDispatcherPolicy{32, 32});
    int restored_observer_calls = 0;
    int restored_action_calls = 0;
    if (!restored_dispatcher.RegisterObserver(
            ScheduledTriggerHandlerId::FromString("framework.integration.time_facts"), 0,
            [&restored_observer_calls](const ScheduledTrigger&, const GameplayContext&) {
                ++restored_observer_calls;
                return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::Ack);
            }) ||
        !restored_dispatcher.RegisterActionHandler(
            action.Value(), ScheduledTriggerHandlerId::FromString("framework.test.primary_handler"),
            [&restored_action_calls](const ScheduledTrigger&, const GameplayContext&) {
                ++restored_action_calls;
                return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::Ack);
            }))
    {
        return 121;
    }
    restored_dispatcher.Freeze();
    if (!restored_dispatcher.RestoreSnapshot(pending_snapshot))
    {
        return 122;
    }
    const auto restored_report = restored_dispatcher.DispatchPending();
    if (restored_report.pending != 0 || restored_observer_calls != 0 || restored_action_calls != 1)
    {
        return 123;
    }

    const auto retry_report = dispatcher.DispatchPending();
    if (retry_report.pending != 0 || retry_report.acknowledged != 1 || primary_attempts != 2)
    {
        return 13;
    }
    if (!facts.Dispatch() || due_count != 2)
    {
        return 14;
    }

    // A throwing action handler becomes Retry and never loses the collected occurrence.
    const auto throwing_action = ActionTypeId::FromString("framework.test.throwing");
    GameplayTimeService retry_time;
    const auto retry_clock = retry_time.RegisterClock("framework.clock.retry", CalendarDefinition{});
    const auto retry_action = retry_time.RegisterAction("framework.test.throwing", test_domain);
    if (!retry_clock || !retry_action || retry_action.Value() != throwing_action)
    {
        return 15;
    }
    ScheduledTriggerDispatcher retry_dispatcher(retry_time, ScheduledTriggerDispatcherPolicy{4, 4});
    int throw_attempts = 0;
    if (!retry_dispatcher.RegisterActionHandler(
            retry_action.Value(), ScheduledTriggerHandlerId::FromString("framework.test.throw_handler"),
            [&throw_attempts](const ScheduledTrigger&, const GameplayContext&) -> foundation::Result<ScheduledTriggerDisposition> {
                ++throw_attempts;
                if (throw_attempts == 1)
                {
                    throw std::runtime_error("injected");
                }
                return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::Ack);
            }))
    {
        return 16;
    }
    retry_dispatcher.Freeze();
    retry_time.Freeze();
    if (!retry_time.AdvanceTo(retry_clock.Value(), GameplayTimePoint{20}) ||
        !retry_time.Schedule(retry_clock.Value(), GameplayTimePoint{20}, object, retry_action.Value()))
    {
        return 17;
    }
    const auto retry_pump = retry_dispatcher.Pump(retry_clock.Value(), GameplayContext{});
    if (!retry_pump || retry_pump.Value().pending != 1 || retry_pump.Value().handler_failures != 1)
    {
        return 18;
    }
    const auto retry_snapshot = retry_dispatcher.CaptureSnapshot();
    if (retry_snapshot.pending.size() != 1)
    {
        return 19;
    }
    const auto final_retry = retry_dispatcher.DispatchPending();
    if (final_retry.pending != 0 || throw_attempts != 2)
    {
        return 20;
    }

    // Restore is transactional and rejects duplicate/corrupt pending occurrence without replacing current state.
    auto corrupt = retry_snapshot;
    corrupt.pending.push_back(corrupt.pending.front());
    if (retry_dispatcher.RestoreSnapshot(std::move(corrupt)))
    {
        return 22;
    }
    if (retry_dispatcher.PendingCount() != 0)
    {
        return 23;
    }

    return 0;
}
