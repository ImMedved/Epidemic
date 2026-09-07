#include "Epidemic/GameFramework/Integration/core_adapters.h"

#include <memory>
#include <stdexcept>
#include <vector>

using namespace epidemic;
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::facts;
using namespace epidemic::gameplay::integration;
using namespace epidemic::gameplay::queries;
using namespace epidemic::gameplay::savegame;
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

class TestSaveBarrierLease final : public ISaveBarrierLease
{
};

class TestSaveBarrier final : public ISaveBarrier
{
  public:
    [[nodiscard]] foundation::Result<std::unique_ptr<ISaveBarrierLease>> AcquireCaptureLease() override
    {
        return foundation::Result<std::unique_ptr<ISaveBarrierLease>>::Success(
            std::make_unique<TestSaveBarrierLease>());
    }

    [[nodiscard]] foundation::Result<std::unique_ptr<ISaveBarrierLease>> AcquireRestoreLease() override
    {
        return foundation::Result<std::unique_ptr<ISaveBarrierLease>>::Success(
            std::make_unique<TestSaveBarrierLease>());
    }
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

    if (!dispatcher.Freeze())
    {
        return 44;
    }
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
    if (!restored_dispatcher.Freeze())
    {
        return 122;
    }
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
    if (!retry_dispatcher.Freeze())
    {
        return 17;
    }
    retry_time.Freeze();
    if (!retry_time.AdvanceTo(retry_clock.Value(), GameplayTimePoint{20}) ||
        !retry_time.Schedule(retry_clock.Value(), GameplayTimePoint{20}, object, retry_action.Value()))
    {
        return 171;
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

    // H04: observers are independent legs. Unknown actions remain pending even after every observer Ack,
    // while explicitly ObserverOnly actions may complete without a gameplay action handler.
    GameplayTimeService manifest_time;
    const auto manifest_clock = manifest_time.RegisterClock("framework.clock.manifest", CalendarDefinition{});
    const auto unknown_action = manifest_time.RegisterAction("framework.test.unknown_manifest_action", test_domain);
    const auto observer_only_action = manifest_time.RegisterAction("framework.test.observer_only_action", test_domain);
    const auto missing_handler_action = manifest_time.RegisterAction("framework.test.missing_handler_action", test_domain);
    if (!manifest_clock || !unknown_action || !observer_only_action || !missing_handler_action)
    {
        return 24;
    }

    ScheduledTriggerDispatcher missing_handler_dispatcher(manifest_time, ScheduledTriggerDispatcherPolicy{8, 16});
    if (!missing_handler_dispatcher.DeclareRequiresActionHandler(missing_handler_action.Value()) ||
        missing_handler_dispatcher.Freeze() || missing_handler_dispatcher.IsFrozen())
    {
        return 25;
    }

    ScheduledTriggerDispatcher manifest_dispatcher(manifest_time, ScheduledTriggerDispatcherPolicy{8, 16});
    int manifest_observer_calls = 0;
    const auto manifest_observer_id = ScheduledTriggerHandlerId::FromString("framework.test.manifest_observer");
    if (!manifest_dispatcher.RegisterObserver(
            manifest_observer_id, 0,
            [&manifest_observer_calls](const ScheduledTrigger&, const GameplayContext&) {
                ++manifest_observer_calls;
                return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::Ack);
            }) ||
        !manifest_dispatcher.DeclareObserverOnlyAction(observer_only_action.Value()) ||
        !manifest_dispatcher.Freeze())
    {
        return 26;
    }
    manifest_time.Freeze();
    if (!manifest_time.AdvanceTo(manifest_clock.Value(), GameplayTimePoint{30}) ||
        !manifest_time.Schedule(manifest_clock.Value(), GameplayTimePoint{30}, object, unknown_action.Value()) ||
        !manifest_time.Schedule(manifest_clock.Value(), GameplayTimePoint{30}, object, observer_only_action.Value()))
    {
        return 27;
    }
    const auto manifest_collected = manifest_dispatcher.CollectDue(manifest_clock.Value(), GameplayContext{});
    if (!manifest_collected || manifest_collected.Value() != 2)
    {
        return 28;
    }
    const auto manifest_report = manifest_dispatcher.DispatchPending();
    if (manifest_report.acknowledged != 1 || manifest_report.unhandled != 1 || manifest_report.pending != 1 ||
        manifest_observer_calls != 2)
    {
        return 29;
    }
    const auto manifest_retry = manifest_dispatcher.DispatchPending();
    if (manifest_retry.pending != 1 || manifest_retry.unhandled != 1 || manifest_observer_calls != 2)
    {
        return 30;
    }
    const auto unknown_pending_snapshot = manifest_dispatcher.CaptureSnapshot();
    ScheduledTriggerDispatcher changed_manifest_dispatcher(manifest_time, ScheduledTriggerDispatcherPolicy{8, 16});
    if (!changed_manifest_dispatcher.RegisterObserver(
            manifest_observer_id, 0,
            [](const ScheduledTrigger&, const GameplayContext&) {
                return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::Ack);
            }) ||
        !changed_manifest_dispatcher.DeclareObserverOnlyAction(unknown_action.Value()) ||
        !changed_manifest_dispatcher.DeclareObserverOnlyAction(observer_only_action.Value()) ||
        !changed_manifest_dispatcher.Freeze() || changed_manifest_dispatcher.RestoreSnapshot(unknown_pending_snapshot))
    {
        return 301;
    }

    // H01: save after destructive collection and before delivery. Restore keeps the occurrence in the
    // dispatcher inbox and never requeues it into Time.
    GameplayTimeService save_time;
    const auto save_clock = save_time.RegisterClock("framework.clock.dispatcher_save", CalendarDefinition{});
    const auto save_action = save_time.RegisterAction("framework.test.dispatcher_save_action", test_domain);
    if (!save_clock || !save_action)
    {
        return 31;
    }
    const auto save_observer_id = ScheduledTriggerHandlerId::FromString("framework.test.save_observer");
    const auto save_action_handler_id = ScheduledTriggerHandlerId::FromString("framework.test.save_action_handler");
    ScheduledTriggerDispatcher save_dispatcher(save_time, ScheduledTriggerDispatcherPolicy{8, 16});
    if (!save_dispatcher.RegisterObserver(
            save_observer_id, 0,
            [](const ScheduledTrigger&, const GameplayContext&) {
                return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::Ack);
            }) ||
        !save_dispatcher.RegisterActionHandler(
            save_action.Value(), save_action_handler_id,
            [](const ScheduledTrigger&, const GameplayContext&) {
                return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::Ack);
            }) ||
        !save_dispatcher.Freeze())
    {
        return 32;
    }
    save_time.Freeze();
    if (!save_time.AdvanceTo(save_clock.Value(), GameplayTimePoint{40}) ||
        !save_time.Schedule(save_clock.Value(), GameplayTimePoint{40}, object, save_action.Value()) ||
        !save_dispatcher.CollectDue(save_clock.Value(), GameplayContext{}))
    {
        return 33;
    }

    TestSaveBarrier save_barrier;
    ScheduledTriggerDispatcherSaveParticipant save_participant(save_dispatcher);
    SaveGameOrchestrator save_orchestrator;
    if (!save_orchestrator.SetBarrier(save_barrier) || !save_orchestrator.RegisterParticipant(save_participant) ||
        !save_orchestrator.FreezeRegistry())
    {
        return 34;
    }
    SaveContext save_context;
    save_context.now = GameplayTimePoint{40};
    const auto saved_image_result = save_orchestrator.Capture(save_context);
    if (!saved_image_result)
    {
        return 35;
    }
    const auto saved_image = saved_image_result.Value();

    int restored_save_observer_calls = 0;
    int restored_save_action_calls = 0;
    ScheduledTriggerDispatcher loaded_dispatcher(save_time, ScheduledTriggerDispatcherPolicy{8, 16});
    if (!loaded_dispatcher.RegisterObserver(
            save_observer_id, 0,
            [&restored_save_observer_calls](const ScheduledTrigger&, const GameplayContext&) {
                ++restored_save_observer_calls;
                return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::Ack);
            }) ||
        !loaded_dispatcher.RegisterActionHandler(
            save_action.Value(), save_action_handler_id,
            [&restored_save_action_calls](const ScheduledTrigger&, const GameplayContext&) {
                ++restored_save_action_calls;
                return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::Ack);
            }) ||
        !loaded_dispatcher.Freeze())
    {
        return 36;
    }
    ScheduledTriggerDispatcherSaveParticipant loaded_participant(loaded_dispatcher);
    SaveGameOrchestrator load_orchestrator;
    if (!load_orchestrator.SetBarrier(save_barrier) || !load_orchestrator.RegisterParticipant(loaded_participant) ||
        !load_orchestrator.FreezeRegistry())
    {
        return 37;
    }
    RestoreContext restore_context;
    if (!load_orchestrator.Restore(saved_image, restore_context) || loaded_dispatcher.PendingCount() != 1)
    {
        return 38;
    }
    const auto loaded_report = loaded_dispatcher.DispatchPending();
    if (loaded_report.pending != 0 || restored_save_observer_calls != 1 || restored_save_action_calls != 1)
    {
        return 39;
    }
    const auto no_requeue = save_time.CollectDue(save_clock.Value(), SchedulerBudget{});
    if (!no_requeue || !no_requeue.Value().empty())
    {
        return 40;
    }

    // Save after observer Ack but before action Ack. After restore only the unfinished action leg runs.
    GameplayTimeService leg_time;
    const auto leg_clock = leg_time.RegisterClock("framework.clock.dispatcher_leg_save", CalendarDefinition{});
    const auto leg_action = leg_time.RegisterAction("framework.test.dispatcher_leg_action", test_domain);
    if (!leg_clock || !leg_action)
    {
        return 45;
    }
    const auto leg_observer_id = ScheduledTriggerHandlerId::FromString("framework.test.leg_observer");
    const auto leg_action_handler_id = ScheduledTriggerHandlerId::FromString("framework.test.leg_action_handler");
    ScheduledTriggerDispatcher leg_dispatcher(leg_time, ScheduledTriggerDispatcherPolicy{8, 16});
    int original_leg_observer_calls = 0;
    int original_leg_action_calls = 0;
    if (!leg_dispatcher.RegisterObserver(
            leg_observer_id, 0,
            [&original_leg_observer_calls](const ScheduledTrigger&, const GameplayContext&) {
                ++original_leg_observer_calls;
                return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::Ack);
            }) ||
        !leg_dispatcher.RegisterActionHandler(
            leg_action.Value(), leg_action_handler_id,
            [&original_leg_action_calls](const ScheduledTrigger&, const GameplayContext&) {
                ++original_leg_action_calls;
                return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::Retry);
            }) ||
        !leg_dispatcher.Freeze())
    {
        return 46;
    }
    leg_time.Freeze();
    if (!leg_time.AdvanceTo(leg_clock.Value(), GameplayTimePoint{50}) ||
        !leg_time.Schedule(leg_clock.Value(), GameplayTimePoint{50}, object, leg_action.Value()) ||
        !leg_dispatcher.CollectDue(leg_clock.Value(), GameplayContext{}))
    {
        return 47;
    }
    const auto leg_first_delivery = leg_dispatcher.DispatchPending();
    if (leg_first_delivery.pending != 1 || original_leg_observer_calls != 1 || original_leg_action_calls != 1)
    {
        return 48;
    }

    ScheduledTriggerDispatcherSaveParticipant leg_participant(leg_dispatcher);
    const auto leg_section_result = leg_participant.CaptureSnapshot(SaveContext{});
    if (!leg_section_result)
    {
        return 49;
    }
    const auto leg_section = leg_section_result.Value();

    ScheduledTriggerDispatcher restored_leg_dispatcher(leg_time, ScheduledTriggerDispatcherPolicy{8, 16});
    int restored_leg_observer_calls = 0;
    int restored_leg_action_calls = 0;
    if (!restored_leg_dispatcher.RegisterObserver(
            leg_observer_id, 0,
            [&restored_leg_observer_calls](const ScheduledTrigger&, const GameplayContext&) {
                ++restored_leg_observer_calls;
                return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::Ack);
            }) ||
        !restored_leg_dispatcher.RegisterActionHandler(
            leg_action.Value(), leg_action_handler_id,
            [&restored_leg_action_calls](const ScheduledTrigger&, const GameplayContext&) {
                ++restored_leg_action_calls;
                return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::Ack);
            }) ||
        !restored_leg_dispatcher.Freeze())
    {
        return 50;
    }
    ScheduledTriggerDispatcherSaveParticipant restored_leg_participant(restored_leg_dispatcher);
    const auto staged_leg = restored_leg_participant.StageRestore(leg_section, RestoreContext{});
    if (!staged_leg || !staged_leg.Value())
    {
        return 53;
    }
    restored_leg_participant.CommitRestore(*staged_leg.Value());
    const auto leg_after_restore = restored_leg_dispatcher.DispatchPending();
    if (leg_after_restore.pending != 0 || restored_leg_observer_calls != 0 || restored_leg_action_calls != 1)
    {
        return 54;
    }

    // The participant must reject an otherwise well-formed section whose completed-handler ID is
    // unknown to the frozen registry. With one completed handler it is the final U64 in schema v1.
    auto corrupt_handler_section = leg_section;
    if (corrupt_handler_section.payload.size() < sizeof(std::uint64_t))
    {
        return 55;
    }
    const auto unknown_handler_raw = ScheduledTriggerHandlerId::FromString("framework.test.unknown_completed_handler").Raw();
    for (std::size_t byte = 0; byte < sizeof(std::uint64_t); ++byte)
    {
        corrupt_handler_section.payload[corrupt_handler_section.payload.size() - sizeof(std::uint64_t) + byte] =
            static_cast<std::byte>((unknown_handler_raw >> (byte * 8u)) & 0xffu);
    }
    corrupt_handler_section.payload_hash = SaveGameOrchestrator::HashBytes(corrupt_handler_section.payload);
    if (restored_leg_participant.StageRestore(corrupt_handler_section, RestoreContext{}))
    {
        return 56;
    }
    if (restored_leg_dispatcher.PendingCount() != 0)
    {
        return 57;
    }

    // D1-M02: duplicate validation remains correct near the configured pending capacity without a nested scan.
    GameplayTimeService scale_time;
    ScheduledTriggerDispatcher scale_dispatcher(scale_time, ScheduledTriggerDispatcherPolicy{16384, 16384});
    if (!scale_dispatcher.Freeze())
    {
        return 58;
    }
    ScheduledTriggerDispatcherSnapshot scale_snapshot;
    scale_snapshot.pending.reserve(16000);
    const auto scale_clock = ClockId::FromString("framework.clock.trigger_scale");
    const auto scale_action = ActionTypeId::FromString("framework.test.trigger_scale");
    for (std::uint64_t index = 0; index < 16000; ++index)
    {
        ScheduledTriggerDeliveryRecord delivery;
        delivery.trigger.schedule = ScheduleId::FromRaw(1, index + 1);
        delivery.trigger.clock = scale_clock;
        delivery.trigger.owner = object;
        delivery.trigger.action = scale_action;
        delivery.trigger.scheduled_for = GameplayTimePoint{static_cast<std::int64_t>(index + 1)};
        delivery.trigger.observed_at = delivery.trigger.scheduled_for;
        delivery.trigger.occurrence_count = 1;
        delivery.context.time = delivery.trigger.observed_at;
        scale_snapshot.pending.push_back(std::move(delivery));
    }
    if (!scale_dispatcher.RestoreSnapshot(scale_snapshot) || scale_dispatcher.PendingCount() != scale_snapshot.pending.size())
    {
        return 59;
    }
    auto scale_duplicate = scale_snapshot;
    scale_duplicate.pending.push_back(scale_duplicate.pending.front());
    if (scale_dispatcher.RestoreSnapshot(std::move(scale_duplicate)))
    {
        return 60;
    }
    if (scale_dispatcher.PendingCount() != scale_snapshot.pending.size())
    {
        return 61;
    }

    return 0;
}
