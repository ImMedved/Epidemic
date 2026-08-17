#include "Epidemic/GameFramework/Integration/core_adapters.h"

#include <memory>
#include <vector>

using namespace epidemic;
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::facts;
using namespace epidemic::gameplay::integration;
using namespace epidemic::gameplay::queries;
using namespace epidemic::gameplay::time;

namespace
{
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
    if (!fact_type || !clock || !action)
    {
        return 1;
    }

    FactsQueryAdapter facts_query(facts, queries);
    if (!facts_query.RegisterProviders())
    {
        return 2;
    }
    TimeFactsAdapter time_facts(time, facts);
    const auto due_event = time_facts.RegisterContracts();
    if (!due_event)
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
    if (!scheduled)
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
    const auto triggers = time_facts.CollectAndPublish(clock.Value(), context);
    if (!triggers || triggers.Value().size() != 1)
    {
        return 10;
    }
    if (!facts.Dispatch() || due_count != 1)
    {
        return 11;
    }

    return 0;
}
