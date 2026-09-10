#include "Epidemic/GameFramework/Facts/gameplay_facts.h"
#include "allocation_fault_injection.h"

#include <algorithm>
#include <string>
#include <stdexcept>
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

foundation::Result<std::vector<std::byte>> EncodeInt(int value)
{
    const auto bits = static_cast<std::uint32_t>(value);
    return foundation::Result<std::vector<std::byte>>::Success({
        static_cast<std::byte>(bits & 0xffu),
        static_cast<std::byte>((bits >> 8u) & 0xffu),
        static_cast<std::byte>((bits >> 16u) & 0xffu),
        static_cast<std::byte>((bits >> 24u) & 0xffu)});
}
foundation::Result<int> DecodeInt(std::span<const std::byte> bytes, std::uint32_t version)
{
    if (version != 1 || bytes.size() != 4)
    {
        return foundation::Result<int>::Failure(foundation::Error::Create("test.codec.invalid", "invalid integer payload"));
    }
    const auto bits = std::to_integer<std::uint32_t>(bytes[0]) |
                      (std::to_integer<std::uint32_t>(bytes[1]) << 8u) |
                      (std::to_integer<std::uint32_t>(bytes[2]) << 16u) |
                      (std::to_integer<std::uint32_t>(bytes[3]) << 24u);
    return foundation::Result<int>::Success(static_cast<int>(bits));
}
foundation::Result<std::vector<std::byte>> EncodeEvent(const TestEvent& value) { return EncodeInt(value.value); }
foundation::Result<TestEvent> DecodeEvent(std::span<const std::byte> bytes, std::uint32_t version)
{
    auto value = DecodeInt(bytes, version);
    if (!value) return foundation::Result<TestEvent>::Failure(value.GetError());
    return foundation::Result<TestEvent>::Success(TestEvent{value.Value()});
}
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
    if (!service.RegisterEventCodec<TestEvent>(event_type.Value(), "framework.test.event.v1", 1, EncodeEvent, DecodeEvent) ||
        !service.RegisterFactCodec<int>(fact_type.Value(), "framework.test.fact.v1", 1, EncodeInt, DecodeInt))
    {
        return 101;
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
    auto value = service.FindFactValueCopy<int>(key);
    if (!value || *value != 42)
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
    value = service.FindFactValueCopy<int>(key);
    if (!value || *value != 42 || service.HistoryCount() != 2)
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
    if (!expired || expired.Value() != 1 || service.FindFact(timed_key).has_value())
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

    const auto disk_snapshot = service.CapturePersistenceSnapshot();
    if (!disk_snapshot) return 261;
    GameplayFactsService disk_restored;
    const auto disk_event = disk_restored.RegisterEventType<TestEvent>("framework.test.event", owner, HistoryPolicy::Persistent);
    const auto disk_fact = disk_restored.RegisterFactType<int>("framework.test.fact", owner);
    if (!disk_event || !disk_fact ||
        !disk_restored.RegisterEventCodec<TestEvent>(disk_event.Value(), "framework.test.event.v1", 1, EncodeEvent, DecodeEvent) ||
        !disk_restored.RegisterFactCodec<int>(disk_fact.Value(), "framework.test.fact.v1", 1, EncodeInt, DecodeInt))
        return 262;
    disk_restored.Freeze();
    if (!disk_restored.RestorePersistenceSnapshot(disk_snapshot.Value())) return 263;
    const auto disk_value = disk_restored.FindFactValueCopy<int>(key);
    if (!disk_value || *disk_value != 42 || disk_restored.HistoryCount() != service.HistoryCount()) return 264;
    auto bad_schema = disk_snapshot.Value();
    if (!bad_schema.facts.empty())
    {
        bad_schema.facts.front().payload_schema = TypeId::FromString("framework.test.wrong_schema");
        const auto bad_restore = disk_restored.RestorePersistenceSnapshot(std::move(bad_schema));
        if (bad_restore || !bad_restore.GetError().HasCode("gameplay.fact_codec_mismatch")) return 265;
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

    // Event scope is preserved by direct and batched publication and is queryable from history.
    GameplayFactsService scoped_service;
    const auto scoped_type = scoped_service.RegisterEventType<TestEvent>(
        "framework.test.scoped", owner, HistoryPolicy::Persistent);
    if (!scoped_type)
    {
        return 32;
    }
    scoped_service.Freeze();
    const GameplayObjectRef scope_a{owner, GameplayObjectId::FromString("test.scope.a")};
    const GameplayObjectRef scope_b{owner, GameplayObjectId::FromString("test.scope.b")};
    if (!scoped_service.Publish<TestEvent>(scoped_type.Value(), context, scope_a, subject, TestEvent{41}))
    {
        return 33;
    }
    auto scoped_batch = scoped_service.CreateBatch(ProducerId::FromString("producer.scoped"), 0);
    scoped_batch.Publish(scoped_type.Value(), context, scope_b, subject, TestEvent{42});
    if (!scoped_service.SubmitBatch(std::move(scoped_batch)) || !scoped_service.Dispatch())
    {
        return 34;
    }
    HistoryQuery scope_a_query;
    scope_a_query.type = scoped_type.Value();
    scope_a_query.scope = scope_a;
    const auto scope_a_history = scoped_service.FindHistory(scope_a_query);
    HistoryQuery scope_b_query;
    scope_b_query.type = scoped_type.Value();
    scope_b_query.scope = scope_b;
    const auto scope_b_history = scoped_service.FindHistory(scope_b_query);
    if (scope_a_history.size() != 1 || scope_b_history.size() != 1 ||
        scope_a_history.front().envelope.scope != scope_a || scope_b_history.front().envelope.scope != scope_b ||
        std::any_cast<TestEvent>(scope_a_history.front().payload).value != 41 ||
        std::any_cast<TestEvent>(scope_b_history.front().payload).value != 42)
    {
        return 35;
    }

    // A failing subscriber is isolated after the event has committed to history.
    GameplayFactsService subscriber_service;
    const auto subscriber_type = subscriber_service.RegisterEventType<TestEvent>(
        "framework.test.throwing_subscriber", owner, HistoryPolicy::Persistent);
    if (!subscriber_type)
    {
        return 36;
    }
    int delivered_after_failure = 0;
    if (!subscriber_service.Subscribe<TestEvent>(
            subscriber_type.Value(), SubscriberId::FromString("test.throwing_subscriber"), 0,
            [](const EventEnvelope&, const TestEvent&) { throw std::runtime_error("subscriber failure"); }) ||
        !subscriber_service.Subscribe<TestEvent>(
            subscriber_type.Value(), SubscriberId::FromString("test.healthy_subscriber"), 10,
            [&delivered_after_failure](const EventEnvelope&, const TestEvent&) { ++delivered_after_failure; }))
    {
        return 37;
    }
    subscriber_service.Freeze();
    if (!subscriber_service.Publish<TestEvent>(subscriber_type.Value(), context, subject, TestEvent{7}))
    {
        return 38;
    }
    const auto subscriber_dispatch = subscriber_service.Dispatch();
    const auto subscriber_diagnostics = subscriber_service.GetDiagnostics();
    if (!subscriber_dispatch || subscriber_dispatch.Value() != 1 || delivered_after_failure != 1 ||
        subscriber_service.HistoryCount() != 1 || subscriber_diagnostics.subscriber_failures != 1)
    {
        return 39;
    }

    struct ThrowingCompactor final : IHistoryCompactor
    {
        [[nodiscard]] std::vector<EventRecord> Compact(std::span<const EventRecord>) const override
        {
            throw std::runtime_error("compactor failure");
        }
    };
    const auto history_before_throwing_compaction = compact_service.HistoryCount();
    const ThrowingCompactor throwing_compactor;
    const auto throwing_compaction = compact_service.CompactHistory(compact_type.Value(), throwing_compactor);
    if (throwing_compaction || !throwing_compaction.GetError().HasCode("gameplay.history_compactor_exception") ||
        compact_service.HistoryCount() != history_before_throwing_compaction)
    {
        return 40;
    }


    // Service-owned allocation failures must not partially commit a fact transaction.
    bool commit_fault_observed = false;
    for (long long fail_after = 0; fail_after < 96 && !commit_fault_observed; ++fail_after)
    {
        GameplayFactsService fault_service;
        const auto fault_fact = fault_service.RegisterFactType<int>("framework.test.fault_fact", owner);
        if (!fault_fact)
            return 41;
        fault_service.Freeze();
        const GameplayObjectRef fault_subject{owner, GameplayObjectId::FromString("test.fault.subject")};
        const FactKey fault_key{fault_fact.Value(), fault_subject, {}};
        auto tx = fault_service.BeginTransaction(owner);
        tx.Set<int>(fault_fact.Value(), fault_subject, {}, 77, FactPersistence::Persistent);
        const auto before = fault_service.CaptureSnapshot();
        if (!before)
            return 42;
        bool threw = false;
        {
            epidemic::tests::allocation_fault::FailAfter fault(fail_after);
            try
            {
                (void)fault_service.Commit(std::move(tx), context);
            }
            catch (const std::bad_alloc &)
            {
                threw = true;
            }
        }
        if (threw)
        {
            commit_fault_observed = true;
            const auto after = fault_service.CaptureSnapshot();
            if (!after || fault_service.FindFact(fault_key).has_value() ||
                after.Value().fact_revision != before.Value().fact_revision ||
                after.Value().fact_ids.scope != before.Value().fact_ids.scope ||
                after.Value().fact_ids.next != before.Value().fact_ids.next)
                return 43;
        }
    }
    if (!commit_fault_observed)
        return 44;

    // Direct publish ordering and accepted-event ownership are unchanged on allocation failure.
    bool publish_fault_observed = false;
    for (long long fail_after = 0; fail_after < 64 && !publish_fault_observed; ++fail_after)
    {
        GameplayFactsService fault_service;
        const auto type = fault_service.RegisterEventType<TestEvent>(
            "framework.test.fault_event", owner, HistoryPolicy::Persistent);
        if (!type)
            return 45;
        fault_service.Freeze();
        const auto before_diag = fault_service.GetDiagnostics();
        bool threw = false;
        {
            epidemic::tests::allocation_fault::FailAfter fault(fail_after);
            try
            {
                (void)fault_service.Publish<TestEvent>(type.Value(), context, subject, TestEvent{91});
            }
            catch (const std::bad_alloc &)
            {
                threw = true;
            }
        }
        if (threw)
        {
            publish_fault_observed = true;
            const auto after_diag = fault_service.GetDiagnostics();
            if (after_diag.published_events != before_diag.published_events || fault_service.HistoryCount() != 0)
                return 46;
            if (!fault_service.Publish<TestEvent>(type.Value(), context, subject, TestEvent{92}))
                return 47;
            const auto retry = fault_service.Dispatch();
            if (!retry || retry.Value() != 1 || fault_service.HistoryCount() != 1)
                return 48;
        }
    }
    if (!publish_fault_observed)
        return 49;

    // Submitted batches remain owned by the service if merge/dispatch staging throws.
    bool merge_fault_observed = false;
    for (long long fail_after = 0; fail_after < 128 && !merge_fault_observed; ++fail_after)
    {
        GameplayFactsService fault_service;
        const auto type = fault_service.RegisterEventType<TestEvent>(
            "framework.test.batch_fault", owner, HistoryPolicy::Persistent);
        if (!type)
            return 50;
        int delivered = 0;
        if (!fault_service.Subscribe<TestEvent>(type.Value(), SubscriberId::FromString("fault.batch.sub"), 0,
                                                 [&delivered](const EventEnvelope &, const TestEvent &) { ++delivered; }))
            return 51;
        fault_service.Freeze();
        auto batch = fault_service.CreateBatch(ProducerId::FromString("fault.batch.producer"), 0);
        batch.Publish(type.Value(), context, subject, TestEvent{1});
        if (!fault_service.SubmitBatch(std::move(batch)))
            return 52;
        bool threw = false;
        {
            epidemic::tests::allocation_fault::FailAfter fault(fail_after);
            try
            {
                (void)fault_service.Dispatch();
            }
            catch (const std::bad_alloc &)
            {
                threw = true;
            }
        }
        if (threw)
        {
            merge_fault_observed = true;
            const auto retry = fault_service.Dispatch();
            if (!retry || retry.Value() != 1 || delivered != 1 || fault_service.HistoryCount() != 1)
                return 53;
        }
    }
    if (!merge_fault_observed)
        return 54;


    return 0;
}
