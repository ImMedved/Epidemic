#include "Epidemic/GameFramework/Facts/gameplay_facts.h"

#include <algorithm>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace epidemic;
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::facts;

namespace
{
struct TestEvent
{
    int value = 0;
};
} // namespace

int main()
{
    GameplayFactsService service;
    const auto owner = GameplayDomainId::FromString("framework.test");
    const auto other = GameplayDomainId::FromString("framework.other");
    const auto event_type = service.RegisterEventType<TestEvent>("framework.test.event", owner, HistoryPolicy::Persistent);
    const auto fact_type = service.RegisterFactType<int>("framework.test.fact", owner);
    if (!event_type || !fact_type)
    {
        return 1;
    }

    std::vector<int> observed;
    const auto sub_b = service.Subscribe<TestEvent>(
        event_type.Value(), SubscriberId::FromString("test.sub.b"), 10,
        [&observed](const EventEnvelope&, const TestEvent& event) { observed.push_back(event.value + 100); });
    const auto sub_a = service.Subscribe<TestEvent>(
        event_type.Value(), SubscriberId::FromString("test.sub.a"), 0,
        [&service, &observed, type = event_type.Value()](const EventEnvelope& envelope, const TestEvent& event) {
            observed.push_back(event.value);
            if (event.value == 1)
            {
                GameplayContext follow = envelope.context;
                [[maybe_unused]] const auto queued = service.Publish<TestEvent>(type, follow, envelope.subject, TestEvent{2});
            }
        });
    if (!sub_a || !sub_b)
    {
        return 2;
    }
    service.Freeze();

    const GameplayObjectRef subject{owner, GameplayObjectId::FromString("test.object")};
    GameplayContext context;
    context.tick = GameplayTickId{1};
    context.time = GameplayTimePoint{10};
    context.operation = OperationId::FromString("test.operation");
    const auto published = service.Publish<TestEvent>(event_type.Value(), context, subject, TestEvent{1});
    if (!published)
    {
        return 3;
    }
    const auto dispatch = service.Dispatch();
    if (!dispatch || dispatch.Value() != 2 || observed != std::vector<int>({1, 101, 2, 102}))
    {
        return 4;
    }
    if (service.HistoryCount() != 2)
    {
        return 5;
    }

    auto denied = service.BeginTransaction(other);
    denied.Set<int>(fact_type.Value(), subject, {}, 5, FactPersistence::Persistent);
    const auto denied_result = service.Commit(std::move(denied), context);
    if (denied_result || !denied_result.GetError().HasCode("gameplay.fact_owner_violation"))
    {
        return 6;
    }

    auto transaction = service.BeginTransaction(owner);
    transaction.Set<int>(fact_type.Value(), subject, {}, 42, FactPersistence::Persistent);
    const auto committed = service.Commit(std::move(transaction), context, service.FindHistory(event_type.Value()).front().envelope.id);
    if (!committed || committed.Value().size() != 1)
    {
        return 7;
    }
    const FactKey key{fact_type.Value(), subject, {}};
    const auto* value = service.FindFactValue<int>(key);
    if (value == nullptr || *value != 42)
    {
        return 8;
    }

    const auto fact_change_dispatch = service.Dispatch();
    if (!fact_change_dispatch || fact_change_dispatch.Value() != 1)
    {
        return 9;
    }

    const auto snapshot = service.CaptureSnapshot();
    auto remove = service.BeginTransaction(owner);
    remove.Remove(key);
    if (!service.Commit(std::move(remove), context))
    {
        return 10;
    }
    if (!snapshot || !service.RestoreSnapshot(snapshot.Value()))
    {
        return 11;
    }
    value = service.FindFactValue<int>(key);
    if (value == nullptr || *value != 42 || service.HistoryCount() != 2)
    {
        return 12;
    }

    GameplayEventBatch batch_a = service.CreateBatch(ProducerId::FromString("producer.a"), 0);
    GameplayEventBatch batch_b = service.CreateBatch(ProducerId::FromString("producer.b"), 0);
    batch_b.Publish(event_type.Value(), context, subject, TestEvent{20});
    batch_a.Publish(event_type.Value(), context, subject, TestEvent{10});
    if (!service.SubmitBatch(std::move(batch_b)) || !service.SubmitBatch(std::move(batch_a)))
    {
        return 13;
    }
    observed.clear();
    const auto batch_dispatch = service.Dispatch();
    const auto producer_a = ProducerId::FromString("producer.a");
    const auto producer_b = ProducerId::FromString("producer.b");
    const std::vector<int> expected = producer_a.Raw() < producer_b.Raw()
        ? std::vector<int>({10, 110, 20, 120})
        : std::vector<int>({20, 120, 10, 110});
    if (!batch_dispatch || observed != expected)
    {
        return 14;
    }

    // Worker batches may be submitted concurrently; dispatch order is independent
    // from completion order and follows stable producer ids.
    observed.clear();
    std::vector<std::pair<ProducerId, int>> producer_values;
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i)
    {
        const auto producer = ProducerId::FromString(std::string("producer.thread.") + std::to_string(i));
        producer_values.emplace_back(producer, 1000 + i);
        workers.emplace_back([&service, producer, value = 1000 + i, type = event_type.Value(), context, subject]() mutable {
            auto batch = service.CreateBatch(producer, 0);
            batch.Publish(type, context, subject, TestEvent{value});
            const auto submitted = service.SubmitBatch(std::move(batch));
            if (!submitted)
            {
                std::terminate();
            }
        });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }
    std::sort(producer_values.begin(), producer_values.end(), [](const auto& left, const auto& right) {
        return left.first.Raw() < right.first.Raw();
    });
    const auto threaded_dispatch = service.Dispatch();
    if (!threaded_dispatch || threaded_dispatch.Value() != producer_values.size())
    {
        return 15;
    }
    std::vector<int> threaded_expected;
    for (const auto& [_, producer_value] : producer_values)
    {
        threaded_expected.push_back(producer_value);
        threaded_expected.push_back(producer_value + 100);
    }
    if (observed != threaded_expected)
    {
        return 16;
    }

    // Timed facts expire against gameplay time, not wall clock.
    const GameplayObjectRef timed_subject{owner, GameplayObjectId::FromString("test.timed.object")};
    GameplayContext timed_context = context;
    timed_context.time = GameplayTimePoint{40};
    auto timed = service.BeginTransaction(owner);
    timed.Set<int>(fact_type.Value(), timed_subject, {}, 9, FactPersistence::Timed, GameplayTimePoint{50});
    if (!service.Commit(std::move(timed), timed_context))
    {
        return 17;
    }
    const FactKey timed_key{fact_type.Value(), timed_subject, {}};
    if (!service.FindFact(timed_key))
    {
        return 18;
    }
    timed_context.time = GameplayTimePoint{49};
    const auto not_expired = service.ExpireDueFacts(timed_context.time, timed_context);
    if (!not_expired || not_expired.Value() != 0 || !service.FindFact(timed_key))
    {
        return 19;
    }
    timed_context.time = GameplayTimePoint{50};
    const auto expired = service.ExpireDueFacts(timed_context.time, timed_context);
    if (!expired || expired.Value() != 1 || service.FindFact(timed_key) != nullptr)
    {
        return 20;
    }


    // No-op remove must not advance authoritative fact revision.
    const auto revision_before_noop = service.FactRevision();
    auto noop_remove = service.BeginTransaction(owner);
    noop_remove.Remove(FactKey{fact_type.Value(), GameplayObjectRef{owner, GameplayObjectId::FromString("missing.fact")}, {}});
    const auto noop_result = service.Commit(std::move(noop_remove), context);
    if (!noop_result || !noop_result.Value().empty() || service.FactRevision() != revision_before_noop)
    {
        return 21;
    }

    // Only timed facts may carry expiration.
    auto invalid_expiration = service.BeginTransaction(owner);
    invalid_expiration.Set<int>(fact_type.Value(), subject, {}, 7, FactPersistence::Persistent, GameplayTimePoint{99});
    const auto invalid_expiration_result = service.Commit(std::move(invalid_expiration), context);
    if (invalid_expiration_result || !invalid_expiration_result.GetError().HasCode("gameplay.fact_unexpected_expiration"))
    {
        return 22;
    }

    // Persistent snapshots are allowed only on a quiescent event boundary.
    if (!service.Publish<TestEvent>(event_type.Value(), context, subject, TestEvent{33}))
    {
        return 23;
    }
    const auto busy_snapshot = service.CaptureSnapshot();
    if (busy_snapshot || !busy_snapshot.GetError().HasCode("gameplay.facts_not_quiescent"))
    {
        return 24;
    }
    if (!service.Dispatch())
    {
        return 25;
    }
    const auto clean_snapshot = service.CaptureSnapshot();
    if (!clean_snapshot)
    {
        return 26;
    }

    auto duplicate_fact_snapshot = clean_snapshot.Value();
    if (!duplicate_fact_snapshot.facts.empty())
    {
        duplicate_fact_snapshot.facts.push_back(duplicate_fact_snapshot.facts.front());
        const auto duplicate_restore = service.RestoreSnapshot(std::move(duplicate_fact_snapshot));
        if (duplicate_restore || !duplicate_restore.GetError().HasCode("gameplay.fact_snapshot_invalid"))
        {
            return 27;
        }
    }

    GameplayFactsService registration_service;
    const auto bad_recent = registration_service.RegisterEventType<TestEvent>("framework.test.bad_recent", owner, HistoryPolicy::Recent, 0);
    if (bad_recent || !bad_recent.GetError().HasCode("gameplay.history_recent_limit_invalid"))
    {
        return 28;
    }

    struct DuplicateCompactor final : IHistoryCompactor
    {
        [[nodiscard]] std::vector<EventRecord> Compact(std::span<const EventRecord> records) const override
        {
            if (records.empty())
            {
                return {};
            }
            return {records.front(), records.front()};
        }
    };

    GameplayFactsService compact_service;
    const auto compact_type = compact_service.RegisterEventType<TestEvent>(
        "framework.test.compactable", owner, HistoryPolicy::PersistentCompactable);
    if (!compact_type)
    {
        return 29;
    }
    compact_service.Freeze();
    if (!compact_service.Publish<TestEvent>(compact_type.Value(), context, subject, TestEvent{1}) ||
        !compact_service.Publish<TestEvent>(compact_type.Value(), context, subject, TestEvent{2}) || !compact_service.Dispatch())
    {
        return 30;
    }
    const DuplicateCompactor duplicate_compactor;
    const auto compacted = compact_service.CompactHistory(compact_type.Value(), duplicate_compactor);
    if (compacted || !compacted.GetError().HasCode("gameplay.history_snapshot_invalid"))
    {
        return 31;
    }
    return 0;
}
