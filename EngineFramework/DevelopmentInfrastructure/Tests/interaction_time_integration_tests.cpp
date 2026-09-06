#include "Epidemic/GameFramework/InteractionTimeIntegration/interaction_time_adapter.h"

#include <cstdlib>

#define CHECK(expr) do { if (!(expr)) std::abort(); } while (false)

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::integration;
using namespace epidemic::gameplay::interaction;
using namespace epidemic::gameplay::interaction_time_integration;
using namespace epidemic::gameplay::time;

namespace
{
struct TimedProvider final : IInteractionProvider
{
    InteractionTypeId type{};
    InteractionProviderId id = InteractionProviderId::FromString("test.interaction_time.provider");

    [[nodiscard]] InteractionProviderId Id() const noexcept override { return id; }

    [[nodiscard]] std::vector<InteractionCandidate> Collect(const InteractionContext& context) const override
    {
        return {{type, context.actor, context.target, id, 10, InteractionAvailability::Available, {}, {}}};
    }
};

struct Fixture
{
    GameplayTimeService time;
    InteractionService interactions;
    ScheduledTriggerDispatcher dispatcher;
    InteractionTimeAdapter adapter;
    TimedProvider provider;
    ClockId clock{};
    InteractionTypeId type = InteractionTypeId::FromString("game.repair");

    Fixture()
        : dispatcher(time), adapter(interactions, time)
    {
        const auto clock_result = time.RegisterClock("game.world");
        CHECK(clock_result);
        clock = clock_result.Value();

        InteractionDefinition definition;
        definition.type = type;
        definition.canonical_name = "game.repair";
        definition.mode = InteractionExecutionMode::Timed;
        definition.materialization = InteractionMaterializationPolicy::AbstractAllowed;
        definition.persistence = InteractionPersistence::PersistentSession;
        definition.duration = GameplayDuration{10};
        CHECK(interactions.RegisterDefinition(definition));

        provider.type = type;
        CHECK(interactions.RegisterProvider(provider));
        CHECK(adapter.RegisterContracts());
        CHECK(adapter.RegisterContracts());
        CHECK(adapter.RegisterWithDispatcher(dispatcher));
        CHECK(adapter.RegisterWithDispatcher(dispatcher));

        interactions.Freeze();
        time.Freeze();
        dispatcher.Freeze();
        CHECK(time.SynchronizeClock(clock, GameplayTimePoint{0}, Revision{1}));
    }

    [[nodiscard]] InteractionContext Context(std::uint64_t suffix = 1) const
    {
        InteractionContext context;
        context.actor = {GameplayDomainId::FromString("test.actor"), GameplayObjectId::FromRaw(1, suffix)};
        context.target = {GameplayDomainId::FromString("test.target"), GameplayObjectId::FromRaw(2, suffix)};
        context.gameplay.time = GameplayTimePoint{0};
        return context;
    }

    [[nodiscard]] InteractionExecutionId Start(const InteractionContext& context)
    {
        const auto candidates = interactions.GetAvailableInteractions(context);
        CHECK(candidates.size() == 1);
        const auto plan = interactions.Prepare(context, candidates.front());
        CHECK(plan);
        const auto committed = interactions.Commit(plan.Value());
        CHECK(committed);
        return committed.Value().execution;
    }
};

void TestDispatcherCompletionAndObservedTime()
{
    Fixture fixture;
    const auto context = fixture.Context();
    const auto execution = fixture.Start(context);

    const auto synchronized = fixture.adapter.Synchronize(fixture.clock);
    CHECK(synchronized);
    CHECK(synchronized.Value() == 1);

    const auto* session = fixture.interactions.FindSession(execution);
    CHECK(session != nullptr);
    CHECK(session->completion_schedule.has_value());
    const auto completion_schedule = *session->completion_schedule;
    CHECK(fixture.time.HasSchedule(completion_schedule));

    // Collect after the semantic due time. The interaction must record trigger.observed_at (15), not caller time (999)
    // and not reinterpret the logical tick as gameplay time.
    CHECK(fixture.time.SynchronizeClock(fixture.clock, GameplayTimePoint{15}, Revision{2}));
    GameplayContext processing_context;
    processing_context.time = GameplayTimePoint{999};
    processing_context.tick = GameplayTickId{77};
    const auto pumped = fixture.dispatcher.Pump(fixture.clock, processing_context);
    CHECK(pumped);
    CHECK(pumped.Value().collected == 1);
    CHECK(pumped.Value().acknowledged == 1);
    CHECK(pumped.Value().pending == 0);
    CHECK(fixture.interactions.FindSession(execution) == nullptr);

    const auto changes = fixture.interactions.ReadChangesSince(0);
    CHECK(!changes.snapshot_required);
    CHECK(!changes.changes.empty());
    const auto& completed = changes.changes.back();
    CHECK(completed.kind == InteractionChangeKind::Completed);
    CHECK(completed.execution == execution);
    CHECK(completed.context.time == GameplayTimePoint{15});
    CHECK(completed.context.tick == GameplayTickId{77});

    // Time already consumed the one-shot schedule; reconciliation must not recreate it for a terminal session.
    CHECK(fixture.adapter.Synchronize(fixture.clock));
    CHECK(!fixture.time.HasSchedule(completion_schedule));
}

void TestRestoreReusesPersistentSchedule()
{
    Fixture original;
    const auto context = original.Context(2);
    const auto execution = original.Start(context);
    CHECK(original.adapter.Synchronize(original.clock));

    const auto* session = original.interactions.FindSession(execution);
    CHECK(session != nullptr && session->completion_schedule.has_value());
    const auto original_schedule = *session->completion_schedule;
    const auto interaction_snapshot = original.interactions.CaptureSnapshot();
    const auto time_snapshot = original.time.CaptureSnapshot();
    CHECK(interaction_snapshot.sessions.size() == 1);
    CHECK(time_snapshot.schedules.size() == 1);

    Fixture restored;
    CHECK(restored.interactions.RestoreSnapshot(interaction_snapshot));
    CHECK(restored.time.RestoreSnapshot(time_snapshot));

    // Interaction snapshots intentionally clear scheduler handles. Reconciliation must bind the already-restored Time
    // schedule instead of creating a duplicate.
    CHECK(restored.interactions.SessionsRequiringSchedule().size() == 1);
    const auto synchronized = restored.adapter.Synchronize(restored.clock);
    CHECK(synchronized);
    CHECK(restored.time.CaptureSnapshot().schedules.size() == 1);
    const auto* restored_session = restored.interactions.FindSession(execution);
    CHECK(restored_session != nullptr);
    CHECK(restored_session->completion_schedule.has_value());
    CHECK(*restored_session->completion_schedule == original_schedule);
}

void TestOrphanScheduleCleanup()
{
    Fixture fixture;
    const auto context = fixture.Context(3);
    const auto execution = fixture.Start(context);
    CHECK(fixture.adapter.Synchronize(fixture.clock));
    const auto* session = fixture.interactions.FindSession(execution);
    CHECK(session != nullptr && session->completion_schedule.has_value());
    const auto schedule = *session->completion_schedule;

    CHECK(fixture.interactions.Cancel(execution, TypeId::FromString("test.cancel"), GameplayContext{}));
    CHECK(fixture.time.HasSchedule(schedule));
    const auto synchronized = fixture.adapter.Synchronize(fixture.clock);
    CHECK(synchronized);
    CHECK(!fixture.time.HasSchedule(schedule));
    CHECK(fixture.adapter.PendingReconciliations().empty());
}

void TestJournalGapFallsBackToAuthoritativeReconciliation()
{
    Fixture fixture;

    // Overflow Interaction's bounded journal while this adapter remains at cursor 0.
    for (std::uint64_t i = 10; i < 2200; ++i)
    {
        const auto context = fixture.Context(i);
        const auto execution = fixture.Start(context);
        CHECK(fixture.interactions.Cancel(execution, TypeId::FromString("test.cancel"), GameplayContext{}));
    }
    CHECK(fixture.interactions.ReadChangesSince(0).snapshot_required);

    const auto live_context = fixture.Context(9000);
    const auto live_execution = fixture.Start(live_context);
    const auto synchronized = fixture.adapter.Synchronize(fixture.clock);
    CHECK(synchronized);
    CHECK(fixture.adapter.Cursor() != 0);

    const auto* session = fixture.interactions.FindSession(live_execution);
    CHECK(session != nullptr && session->completion_schedule.has_value());
    CHECK(fixture.time.HasSchedule(*session->completion_schedule));
}

void TestPendingDispatcherTriggerSurvivesRestore()
{
    Fixture original;
    const auto context = original.Context(4);
    const auto execution = original.Start(context);
    CHECK(original.adapter.Synchronize(original.clock));
    CHECK(original.time.SynchronizeClock(original.clock, GameplayTimePoint{10}, Revision{2}));

    // Destructively collect into the shared dispatcher's durable pending state, but do not dispatch yet.
    const auto collected = original.dispatcher.CollectDue(original.clock, GameplayContext{});
    CHECK(collected && collected.Value() == 1);
    CHECK(original.dispatcher.PendingCount() == 1);
    const auto dispatcher_snapshot = original.dispatcher.CaptureSnapshot();
    const auto interaction_snapshot = original.interactions.CaptureSnapshot();
    const auto time_snapshot = original.time.CaptureSnapshot();
    CHECK(time_snapshot.schedules.empty());

    Fixture restored;
    CHECK(restored.interactions.RestoreSnapshot(interaction_snapshot));
    CHECK(restored.time.RestoreSnapshot(time_snapshot));
    CHECK(restored.dispatcher.RestoreSnapshot(dispatcher_snapshot));

    // There is intentionally no Time schedule now: the occurrence lives in the dispatcher inbox. Even though the
    // restored Interaction session has no scheduler handle, reconciliation must recognize the pending occurrence and
    // must not create a duplicate schedule.
    CHECK(restored.adapter.Synchronize(restored.clock));
    CHECK(restored.time.CaptureSnapshot().schedules.empty());
    const auto report = restored.dispatcher.DispatchPending();
    CHECK(report.acknowledged == 1);
    CHECK(report.pending == 0);
    CHECK(restored.interactions.FindSession(execution) == nullptr);
    CHECK(restored.adapter.Synchronize(restored.clock));
    CHECK(restored.time.CaptureSnapshot().schedules.empty());
}
} // namespace

int main()
{
    TestDispatcherCompletionAndObservedTime();
    TestRestoreReusesPersistentSchedule();
    TestOrphanScheduleCleanup();
    TestJournalGapFallsBackToAuthoritativeReconciliation();
    TestPendingDispatcherTriggerSurvivesRestore();
    return 0;
}
