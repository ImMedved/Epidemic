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
    FailingCommitHandler(EffectTypeId type, int& attempts, bool& should_fail)
        : type_(type), attempts_(attempts), should_fail_(should_fail)
    {
    }

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
        if (!should_fail_)
        {
            return foundation::Result<EffectCommitResult>::Success({EffectCommitDisposition::Applied, {}});
        }
        return foundation::Result<EffectCommitResult>::Failure(
            foundation::Error::Create("test.effect.commit_failed", "injected commit failure"));
    }

  private:
    EffectTypeId type_{};
    int& attempts_;
    bool& should_fail_;
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
    bool second_should_fail = true;
    std::optional<GameplayContext> observed_first;

    const auto first_type = EffectTypeId::FromString("test.interaction_effects.first");
    const auto second_type = EffectTypeId::FromString("test.interaction_effects.second");
    if (!effects.RegisterHandler(
            "test.interaction_effects.first",
            std::make_shared<CountingHandler>(first_type, first_commits, &observed_first)) ||
        !effects.RegisterHandler(
            "test.interaction_effects.second",
            std::make_shared<FailingCommitHandler>(second_type, second_attempts, second_should_fail)))
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
    const auto* restored_uncertain = restored.FindDelivery(execution, 1);
    if (restored_uncertain == nullptr ||
        restored_uncertain->state != InteractionEffectDeliveryState::ReconciliationRequired)
    {
        return 14;
    }
    const auto restored_retry = restored.Commit(plan, execution);
    if (restored_retry || first_commits != 1 || second_attempts != 1)
    {
        return 15;
    }

    // Snapshot restore is transactional and rejects records that do not match the
    // frozen mapping without replacing the existing delivery ledger.
    auto corrupted = snapshot;
    if (corrupted.deliveries.empty())
    {
        return 16;
    }
    corrupted.deliveries.front().definition = EffectDefinitionId::FromString("test.interaction_effects.corrupt");
    if (restored.RestoreSnapshot(std::move(corrupted)))
    {
        return 17;
    }
    if (restored.FindDelivery(execution, 0) == nullptr ||
        restored.FindDelivery(execution, 0)->state != InteractionEffectDeliveryState::Applied)
    {
        return 18;
    }

    corrupted = snapshot;
    corrupted.deliveries.front().effect_index = 999;
    if (restored.RestoreSnapshot(std::move(corrupted)))
    {
        return 19;
    }
    corrupted = snapshot;
    corrupted.deliveries.front().effect_execution = {};
    if (restored.RestoreSnapshot(std::move(corrupted)))
    {
        return 20;
    }

    // External recovery can prove that the uncertain effect was already applied.
    // Optional execution evidence is persisted but never invented by the adapter.
    const auto* unresolved_with_evidence = restored.FindDelivery(execution, 1);
    if (unresolved_with_evidence == nullptr || !unresolved_with_evidence->effect_execution.IsValid())
    {
        return 21;
    }
    const EffectExecutionId reconciled_effect_execution = unresolved_with_evidence->effect_execution;
    const InteractionEffectReconciliationRequest confirmed_applied{
        execution,
        1,
        InteractionEffectReconciliationOutcome::ConfirmedApplied,
        reconciled_effect_execution};
    auto resolved = restored.ReconcileDelivery(confirmed_applied);
    if (!resolved || resolved.Value() != InteractionEffectReconciliationStatus::Resolved)
    {
        return 22;
    }
    const auto* applied_by_reconciliation = restored.FindDelivery(execution, 1);
    if (applied_by_reconciliation == nullptr ||
        applied_by_reconciliation->state != InteractionEffectDeliveryState::Applied ||
        applied_by_reconciliation->effect_execution != reconciled_effect_execution)
    {
        return 23;
    }
    auto repeated_resolution = restored.ReconcileDelivery(confirmed_applied);
    if (!repeated_resolution || repeated_resolution.Value() != InteractionEffectReconciliationStatus::AlreadyResolved)
    {
        return 24;
    }
    if (restored.ReconcileDelivery(
            {execution, 1, InteractionEffectReconciliationOutcome::ConfirmedRejected, {}}))
    {
        return 25;
    }
    if (restored.ReconcileDelivery(
            {execution, 99, InteractionEffectReconciliationOutcome::ConfirmedApplied, {}}))
    {
        return 26;
    }
    if (!restored.Commit(plan, execution) || first_commits != 1 || second_attempts != 1)
    {
        return 27;
    }

    // ConfirmedRejected is terminal and a later Commit never runs the Effect.
    InteractionEffectExecutor rejected(effects, interaction_type);
    if (!rejected.RegisterEffect(second_id.Value()) || !rejected.Freeze(interactions))
    {
        return 28;
    }
    const InteractionExecutionId rejected_execution{GameplayObjectId::FromRaw(17, 43)};
    second_should_fail = true;
    if (rejected.Commit(plan, rejected_execution) || second_attempts != 2)
    {
        return 29;
    }
    auto rejected_resolution = rejected.ReconcileDelivery(
        {rejected_execution, 0, InteractionEffectReconciliationOutcome::ConfirmedRejected, {}});
    if (!rejected_resolution || rejected_resolution.Value() != InteractionEffectReconciliationStatus::Resolved)
    {
        return 30;
    }
    if (rejected.Commit(plan, rejected_execution) || second_attempts != 2)
    {
        return 31;
    }
    auto repeated_rejected = rejected.ReconcileDelivery(
        {rejected_execution, 0, InteractionEffectReconciliationOutcome::ConfirmedRejected, {}});
    if (!repeated_rejected || repeated_rejected.Value() != InteractionEffectReconciliationStatus::AlreadyResolved)
    {
        return 32;
    }

    // ConfirmedNotApplied is the only transition that re-opens the normal execution
    // path. The proof survives save/restore and exactly one safe retry is attempted.
    InteractionEffectExecutor retry_safe(effects, interaction_type);
    if (!retry_safe.RegisterEffect(second_id.Value()) || !retry_safe.Freeze(interactions))
    {
        return 33;
    }
    const InteractionExecutionId retry_safe_execution{GameplayObjectId::FromRaw(17, 44)};
    if (retry_safe.Commit(plan, retry_safe_execution) || second_attempts != 3)
    {
        return 34;
    }
    auto retry_safe_resolution = retry_safe.ReconcileDelivery(
        {retry_safe_execution, 0, InteractionEffectReconciliationOutcome::ConfirmedNotApplied, {}});
    if (!retry_safe_resolution || retry_safe_resolution.Value() != InteractionEffectReconciliationStatus::Resolved)
    {
        return 35;
    }
    const auto* retry_pending = retry_safe.FindDelivery(retry_safe_execution, 0);
    if (retry_pending == nullptr || retry_pending->state != InteractionEffectDeliveryState::Pending ||
        retry_pending->effect_execution.IsValid() ||
        retry_pending->reconciliation != InteractionEffectReconciliationOutcome::ConfirmedNotApplied)
    {
        return 36;
    }
    InteractionEffectExecutor retry_safe_restored(effects, interaction_type);
    if (!retry_safe_restored.RegisterEffect(second_id.Value()) || !retry_safe_restored.Freeze(interactions) ||
        !retry_safe_restored.RestoreSnapshot(retry_safe.CaptureSnapshot()))
    {
        return 37;
    }
    retry_pending = retry_safe_restored.FindDelivery(retry_safe_execution, 0);
    if (retry_pending == nullptr || retry_pending->state != InteractionEffectDeliveryState::Pending ||
        retry_pending->reconciliation != InteractionEffectReconciliationOutcome::ConfirmedNotApplied)
    {
        return 38;
    }
    second_should_fail = false;
    if (!retry_safe_restored.Commit(plan, retry_safe_execution) || second_attempts != 4)
    {
        return 39;
    }
    if (!retry_safe_restored.Commit(plan, retry_safe_execution) || second_attempts != 4)
    {
        return 40;
    }

    // Unresolved deliveries are not terminal retention even when the source
    // Interaction execution is terminal, and therefore cannot be pruned.
    InteractionEffectExecutor unresolved_for_prune(effects, interaction_type);
    if (!unresolved_for_prune.RegisterEffect(second_id.Value()) || !unresolved_for_prune.Freeze(interactions))
    {
        return 41;
    }
    const InteractionExecutionId prune_execution{GameplayObjectId::FromRaw(17, 45)};
    second_should_fail = true;
    if (unresolved_for_prune.Commit(plan, prune_execution) || second_attempts != 5)
    {
        return 42;
    }
    unresolved_for_prune.PruneTerminalExecution(prune_execution);
    const auto* retained_unresolved = unresolved_for_prune.FindDelivery(prune_execution, 0);
    if (retained_unresolved == nullptr ||
        retained_unresolved->state != InteractionEffectDeliveryState::ReconciliationRequired)
    {
        return 43;
    }

    // A captured in-flight delivery without recovery proof becomes
    // reconciliation-required on restore and is never automatically replayed.
    InteractionEffectsSnapshot pending_snapshot;
    const InteractionExecutionId pending_execution{GameplayObjectId::FromRaw(17, 99)};
    pending_snapshot.deliveries.push_back(
        {pending_execution, interaction_type, 0, first_id.Value(), {}, InteractionEffectDeliveryState::Pending});
    InteractionEffectExecutor pending_restored(effects, interaction_type);
    if (!pending_restored.RegisterEffect(first_id.Value()) || !pending_restored.Freeze(interactions) ||
        !pending_restored.RestoreSnapshot(std::move(pending_snapshot)))
    {
        return 44;
    }
    const auto* uncertain = pending_restored.FindDelivery(pending_execution, 0);
    if (uncertain == nullptr || uncertain->state != InteractionEffectDeliveryState::ReconciliationRequired)
    {
        return 45;
    }
    pending_restored.PruneTerminalExecution(pending_execution);
    if (pending_restored.FindDelivery(pending_execution, 0) == nullptr)
    {
        return 46;
    }

    // A fully successful mapping reports success and repeated same-execution commit
    // remains exactly-once.
    InteractionEffectExecutor successful(effects, interaction_type);
    if (!successful.RegisterEffect(first_id.Value()) || !successful.Freeze(interactions))
    {
        return 47;
    }
    const InteractionExecutionId success_execution{GameplayObjectId::FromRaw(17, 100)};
    if (!successful.Commit(plan, success_execution) || first_commits != 2)
    {
        return 48;
    }
    if (!successful.Commit(plan, success_execution) || first_commits != 2)
    {
        return 49;
    }

    // Wrong interaction type is a configuration/dispatch error rather than an
    // effect request sent to an unrelated mapping.
    auto wrong_plan = plan;
    wrong_plan.candidate.type = InteractionTypeId::FromString("test.interaction.other");
    if (successful.Validate(wrong_plan))
    {
        return 50;
    }

    // The frozen mapping can be installed as the production executor before the
    // Interaction registry itself is frozen.
    if (!interactions.RegisterExecutor(interaction_type, successful))
    {
        return 51;
    }
    interactions.Freeze();
    const auto prepared = interactions.Prepare(plan.context, plan.candidate);
    if (!prepared)
    {
        return 52;
    }
    const auto committed = interactions.Commit(prepared.Value());
    if (!committed || committed.Value().state != InteractionSessionState::Completed || first_commits != 3)
    {
        return 53;
    }

    return 0;
}
