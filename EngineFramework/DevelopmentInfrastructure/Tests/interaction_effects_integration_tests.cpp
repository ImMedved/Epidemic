#include "Epidemic/GameFramework/InteractionEffectsIntegration/interaction_effects_adapter.h"

#include <memory>
#include <optional>
#include <unordered_map>

using namespace epidemic;
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::effects;
using namespace epidemic::gameplay::interaction;
using namespace epidemic::gameplay::interaction_effects_integration;

namespace
{
class CountingHandler final : public IEffectHandler
{
  public:
    CountingHandler(EffectTypeId type, int& commits, std::optional<GameplayContext>* observed = nullptr)
        : type_(type), commits_(commits), observed_(observed)
    {
    }

    [[nodiscard]] EffectTypeId Type() const noexcept override { return type_; }
    [[nodiscard]] EffectHandlerCapabilities Capabilities() const noexcept override { return {true, false, false, true}; }
    [[nodiscard]] foundation::Result<EffectPrepareResult> Prepare(const EffectOperation&) const override
    {
        return foundation::Result<EffectPrepareResult>::Success({EffectPrepareDisposition::Accepted, {}});
    }
    [[nodiscard]] foundation::Result<EffectCommitResult> Commit(
        const EffectOperation& operation,
        const RegisteredEffectPayload&) noexcept override
    {
        ++commits_;
        if (observed_ != nullptr)
        {
            *observed_ = operation.context;
        }
        return foundation::Result<EffectCommitResult>::Success({EffectCommitDisposition::Applied, {}});
    }

  private:
    EffectTypeId type_{};
    int& commits_;
    std::optional<GameplayContext>* observed_ = nullptr;
};

class FailingCommitHandler final : public IEffectHandler
{
  public:
    FailingCommitHandler(EffectTypeId type, int& attempts) : type_(type), attempts_(attempts) {}

    [[nodiscard]] EffectTypeId Type() const noexcept override { return type_; }
    [[nodiscard]] EffectHandlerCapabilities Capabilities() const noexcept override { return {true, false, false, true}; }
    [[nodiscard]] foundation::Result<EffectPrepareResult> Prepare(const EffectOperation&) const override
    {
        return foundation::Result<EffectPrepareResult>::Success({EffectPrepareDisposition::Accepted, {}});
    }
    [[nodiscard]] foundation::Result<EffectCommitResult> Commit(
        const EffectOperation&,
        const RegisteredEffectPayload&) noexcept override
    {
        ++attempts_;
        return foundation::Result<EffectCommitResult>::Failure(
            foundation::Error::Create("test.effect.commit_failed", "injected commit failure"));
    }

  private:
    EffectTypeId type_{};
    int& attempts_;
};

InteractionDefinition MakeInteractionDefinition(InteractionTypeId type)
{
    InteractionDefinition definition;
    definition.type = type;
    definition.canonical_name = "test.interaction.effects";
    definition.mode = InteractionExecutionMode::Instant;
    definition.materialization = InteractionMaterializationPolicy::AbstractAllowed;
    return definition;
}

InteractionPlan MakePlan(InteractionTypeId type, GameplayObjectRef actor, GameplayObjectRef target)
{
    InteractionPlan plan;
    plan.candidate.type = type;
    plan.candidate.actor = actor;
    plan.candidate.target = target;
    plan.candidate.availability = InteractionAvailability::Available;
    plan.context.actor = actor;
    plan.context.target = target;
    plan.context.gameplay.actor = actor;
    plan.context.gameplay.instigator = actor;
    plan.context.gameplay.source = actor;
    plan.context.gameplay.time = GameplayTimePoint{123};
    return plan;
}
} // namespace

int main()
{
    EffectService effects;
    int first_commits = 0;
    int second_attempts = 0;
    std::optional<GameplayContext> observed_first;

    const auto first_type = EffectTypeId::FromString("test.interaction_effects.first");
    const auto second_type = EffectTypeId::FromString("test.interaction_effects.second");
    if (!effects.RegisterHandler(
            "test.interaction_effects.first",
            std::make_shared<CountingHandler>(first_type, first_commits, &observed_first)) ||
        !effects.RegisterHandler(
            "test.interaction_effects.second",
            std::make_shared<FailingCommitHandler>(second_type, second_attempts)))
    {
        return 1;
    }

    EffectDefinition first_definition;
    first_definition.canonical_name = "test.interaction_effects.first_bundle";
    first_definition.steps.push_back({first_type, EffectTargetSelector::AllTargets, 1, {}});
    auto first_id = effects.RegisterDefinition(std::move(first_definition));

    EffectDefinition second_definition;
    second_definition.canonical_name = "test.interaction_effects.second_bundle";
    second_definition.steps.push_back({second_type, EffectTargetSelector::AllTargets, 1, {}});
    auto second_id = effects.RegisterDefinition(std::move(second_definition));
    if (!first_id || !second_id)
    {
        return 2;
    }
    effects.Freeze();

    InteractionService interactions;
    const auto interaction_type = InteractionTypeId::FromString("test.interaction.effects");
    if (!interactions.RegisterDefinition(MakeInteractionDefinition(interaction_type)))
    {
        return 3;
    }

    // Mapping validation is bootstrap/freeze-time rather than a runtime surprise.
    InteractionEffectExecutor missing_effect(effects, interaction_type);
    if (!missing_effect.RegisterEffect(EffectDefinitionId::FromString("test.interaction_effects.missing")) ||
        missing_effect.Freeze(interactions))
    {
        return 4;
    }

    InteractionEffectExecutor executor(effects, interaction_type);
    if (!executor.RegisterEffect(first_id.Value()) || !executor.RegisterEffect(second_id.Value()))
    {
        return 5;
    }
    if (executor.RegisterEffect(first_id.Value()))
    {
        return 6;
    }
    if (!executor.Freeze(interactions) || !executor.IsFrozen())
    {
        return 7;
    }
    if (executor.RegisterEffect(first_id.Value()))
    {
        return 8;
    }

    const GameplayDomainId domain = GameplayDomainId::FromString("test.interaction_effects.subject");
    const GameplayObjectRef actor{domain, GameplayObjectId::FromRaw(1, 1)};
    const GameplayObjectRef target{domain, GameplayObjectId::FromRaw(1, 2)};
    auto plan = MakePlan(interaction_type, actor, target);
    const InteractionExecutionId execution{GameplayObjectId::FromRaw(17, 42)};

    // First mapped effect commits, second crosses the Effect commit boundary and
    // fails. The interaction cannot report success.
    const auto first_attempt = executor.Commit(plan, execution);
    if (first_attempt || first_commits != 1 || second_attempts != 1)
    {
        return 9;
    }
    const auto* first_delivery = executor.FindDelivery(execution, 0);
    const auto* second_delivery = executor.FindDelivery(execution, 1);
    if (first_delivery == nullptr || second_delivery == nullptr ||
        first_delivery->state != InteractionEffectDeliveryState::Applied ||
        second_delivery->state != InteractionEffectDeliveryState::ReconciliationRequired)
    {
        return 10;
    }
    if (!observed_first.has_value() || !observed_first->operation.IsValid() ||
        observed_first->correlation != CorrelationId::FromRaw(execution.value.High(), execution.value.Low()))
    {
        return 11;
    }

    // Retry of the SAME InteractionExecutionId skips the already-confirmed first
    // effect and refuses to blindly execute the uncertain second leg again.
    const auto retry = executor.Commit(plan, execution);
    if (retry || first_commits != 1 || second_attempts != 1)
    {
        return 12;
    }

    // Delivery state is durable and restore preserves the no-duplicate decision.
    const auto snapshot = executor.CaptureSnapshot();
    InteractionEffectExecutor restored(effects, interaction_type);
    if (!restored.RegisterEffect(first_id.Value()) || !restored.RegisterEffect(second_id.Value()) ||
        !restored.Freeze(interactions) || !restored.RestoreSnapshot(snapshot))
    {
        return 13;
    }
    const auto restored_retry = restored.Commit(plan, execution);
    if (restored_retry || first_commits != 1 || second_attempts != 1)
    {
        return 14;
    }

    // Snapshot restore is transactional and rejects records that do not match the
    // frozen mapping without replacing the existing delivery ledger.
    auto corrupted = snapshot;
    if (corrupted.deliveries.empty())
    {
        return 15;
    }
    corrupted.deliveries.front().definition = EffectDefinitionId::FromString("test.interaction_effects.corrupt");
    if (restored.RestoreSnapshot(std::move(corrupted)))
    {
        return 16;
    }
    if (restored.FindDelivery(execution, 0) == nullptr ||
        restored.FindDelivery(execution, 0)->state != InteractionEffectDeliveryState::Applied)
    {
        return 17;
    }

    // A captured in-flight delivery becomes reconciliation-required on restore;
    // it is never replayed automatically because commit outcome is uncertain.
    InteractionEffectsSnapshot pending_snapshot;
    pending_snapshot.deliveries.push_back(
        {InteractionExecutionId{GameplayObjectId::FromRaw(17, 99)}, interaction_type, 0, first_id.Value(), {},
         InteractionEffectDeliveryState::Pending});
    if (!restored.RestoreSnapshot(std::move(pending_snapshot)))
    {
        return 18;
    }
    const auto* uncertain = restored.FindDelivery(InteractionExecutionId{GameplayObjectId::FromRaw(17, 99)}, 0);
    if (uncertain == nullptr || uncertain->state != InteractionEffectDeliveryState::ReconciliationRequired)
    {
        return 19;
    }

    // A fully successful mapping reports success and repeated same-execution commit
    // remains exactly-once.
    InteractionEffectExecutor successful(effects, interaction_type);
    if (!successful.RegisterEffect(first_id.Value()) || !successful.Freeze(interactions))
    {
        return 20;
    }
    const InteractionExecutionId success_execution{GameplayObjectId::FromRaw(17, 100)};
    if (!successful.Commit(plan, success_execution) || first_commits != 2)
    {
        return 21;
    }
    if (!successful.Commit(plan, success_execution) || first_commits != 2)
    {
        return 22;
    }

    // Wrong interaction type is a configuration/dispatch error rather than an
    // effect request sent to an unrelated mapping.
    auto wrong_plan = plan;
    wrong_plan.candidate.type = InteractionTypeId::FromString("test.interaction.other");
    if (successful.Validate(wrong_plan))
    {
        return 23;
    }

    // The frozen mapping can be installed as the production executor before the
    // Interaction registry itself is frozen.
    if (!interactions.RegisterExecutor(interaction_type, successful))
    {
        return 24;
    }
    interactions.Freeze();
    const auto prepared = interactions.Prepare(plan.context, plan.candidate);
    if (!prepared)
    {
        return 25;
    }
    const auto committed = interactions.Commit(prepared.Value());
    if (!committed || committed.Value().state != InteractionSessionState::Completed || first_commits != 3)
    {
        return 26;
    }

    return 0;
}
