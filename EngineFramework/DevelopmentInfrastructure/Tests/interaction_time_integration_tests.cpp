#include "Epidemic/GameFramework/InteractionTimeIntegration/interaction_time_adapter.h"

#include <cstdlib>
#define CHECK(expr) do { if (!(expr)) std::abort(); } while (false)

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::interaction;
using namespace epidemic::gameplay::interaction_time_integration;
using namespace epidemic::gameplay::time;

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

int main()
{
    GameplayTimeService time;
    const auto clock_result = time.RegisterClock("game.world");
    CHECK(clock_result);
    const ClockId clock = clock_result.Value();

    InteractionService interactions;
    const auto type = InteractionTypeId::FromString("game.repair");
    InteractionDefinition definition;
    definition.type = type;
    definition.canonical_name = "game.repair";
    definition.mode = InteractionExecutionMode::Timed;
    definition.materialization = InteractionMaterializationPolicy::AbstractAllowed;
    definition.persistence = InteractionPersistence::PersistentSession;
    definition.duration = GameplayDuration{10};
    CHECK(interactions.RegisterDefinition(definition));

    TimedProvider provider;
    provider.type = type;
    CHECK(interactions.RegisterProvider(provider));

    InteractionTimeAdapter adapter(interactions, time);
    CHECK(adapter.RegisterContracts());
    interactions.Freeze();
    time.Freeze();

    CHECK(time.SynchronizeClock(clock, GameplayTimePoint{0}, Revision{1}));

    InteractionContext context;
    context.actor = {GameplayDomainId::FromString("test.actor"), GameplayObjectId::FromString("actor.1")};
    context.target = {GameplayDomainId::FromString("test.target"), GameplayObjectId::FromString("target.1")};
    context.gameplay.time = GameplayTimePoint{0};

    const auto candidates = interactions.GetAvailableInteractions(context);
    CHECK(candidates.size() == 1);
    const auto plan = interactions.Prepare(context, candidates.front());
    CHECK(plan);
    const auto committed = interactions.Commit(plan.Value());
    CHECK(committed);

    const InteractionExecutionId execution = committed.Value().execution;
    CHECK(interactions.FindSession(execution) != nullptr);
    CHECK(adapter.Synchronize(clock));

    const auto* session = interactions.FindSession(execution);
    CHECK(session != nullptr);
    CHECK(session->completion_schedule.has_value());
    CHECK(time.HasSchedule(*session->completion_schedule));

    // Persistent active sessions retain the scheduler linkage across snapshots.
    const auto interaction_snapshot = interactions.CaptureSnapshot();
    CHECK(interaction_snapshot.sessions.size() == 1);

    CHECK(time.SynchronizeClock(clock, GameplayTimePoint{10}, Revision{2}));
    const auto triggers = time.CollectDue(clock);
    CHECK(triggers);
    CHECK(triggers.Value().size() == 1);
    CHECK(triggers.Value().front().action == adapter.CompleteAction());
    CHECK(adapter.ProcessTriggers(triggers.Value(), GameplayContext{}));
    CHECK(interactions.FindActive(context.actor).empty());

    // Synchronization consumes the completion change and must not recreate a timer.
    CHECK(adapter.Synchronize(clock));
    CHECK(!time.HasSchedule(*session->completion_schedule));
    CHECK(interactions.CaptureSnapshot().sessions.empty());
    return 0;
}
