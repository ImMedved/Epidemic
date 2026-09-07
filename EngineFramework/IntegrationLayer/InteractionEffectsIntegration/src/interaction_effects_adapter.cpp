#include "Epidemic/GameFramework/InteractionEffectsIntegration/interaction_effects_adapter.h"

#include <algorithm>

namespace epidemic::gameplay::interaction_effects_integration
{
namespace
{
foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}
} // namespace

std::size_t InteractionEffectExecutor::DeliveryKeyHash::operator()(const DeliveryKey& key) const noexcept
{
    const auto a = std::hash<GameplayObjectId>{}(key.execution.value);
    const auto b = std::hash<std::uint32_t>{}(key.effect_index);
    return a ^ (b + 0x9E3779B97F4A7C15ull + (a << 6u) + (a >> 2u));
}

OperationId InteractionEffectExecutor::DeliveryOperation(
    interaction::InteractionExecutionId execution,
    std::uint32_t effect_index,
    effects::EffectDefinitionId definition) noexcept
{
    // The interaction execution is already globally stable inside the Interaction
    // domain. Mix in the ordered mapped-effect index/definition so every delivery
    // has a deterministic operation key while retries receive the same key.
    constexpr std::uint64_t kMix = 0x9E3779B97F4A7C15ull;
    const auto ordinal = static_cast<std::uint64_t>(effect_index) + 1u;
    auto high = execution.value.High() ^ (kMix * ordinal);
    auto low = execution.value.Low() ^ definition.Raw() ^ (0xD6E8FEB86659FD93ull * ordinal);
    if (high == 0 && low == 0)
    {
        low = ordinal;
    }
    return OperationId::FromRaw(high, low);
}

CorrelationId InteractionEffectExecutor::DeliveryCorrelation(interaction::InteractionExecutionId execution) noexcept
{
    return CorrelationId::FromRaw(execution.value.High(), execution.value.Low());
}

foundation::Result<void> InteractionEffectExecutor::RegisterEffect(effects::EffectDefinitionId definition)
{
    if (frozen_)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.registry_frozen", "interaction effect mapping is frozen"));
    }
    if (!definition.IsValid())
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.interaction_effects.mapping_invalid", "effect mapping requires a valid effect definition"));
    }
    if (std::find(definitions_.begin(), definitions_.end(), definition) != definitions_.end())
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.interaction_effects.mapping_duplicate", "effect definition is already mapped to this interaction"));
    }
    definitions_.push_back(definition);
    return foundation::Result<void>::Success();
}

foundation::Result<void> InteractionEffectExecutor::ValidateMapping() const
{
    if (!interaction_type_.IsValid() || definitions_.empty())
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.interaction_effects.mapping_invalid", "mapping requires a valid interaction type and at least one effect"));
    }
    for (const auto definition : definitions_)
    {
        const auto* found = effects_.FindDefinition(definition);
        if (found == nullptr || found->steps.empty())
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.interaction_effects.effect_definition_missing", "mapped effect definition is missing or invalid"));
        }
        // EffectService validates every registered step payload against its handler
        // schema when RegisterDefinition succeeds. Requiring a registered definition
        // here therefore freezes payload compatibility together with the mapping.
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> InteractionEffectExecutor::Freeze(const interaction::InteractionService& interactions)
{
    if (frozen_)
    {
        return foundation::Result<void>::Success();
    }
    if (interactions.FindDefinition(interaction_type_) == nullptr)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.interaction_effects.interaction_definition_missing", "mapped interaction definition is missing"));
    }
    if (!effects_.IsFrozen())
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.interaction_effects.effects_not_frozen", "effect registry must be frozen before interaction-effect mappings"));
    }
    auto valid = ValidateMapping();
    if (!valid)
    {
        return valid;
    }
    frozen_ = true;
    return foundation::Result<void>::Success();
}

foundation::Result<void> InteractionEffectExecutor::Validate(const interaction::InteractionPlan& plan) const
{
    if (!frozen_)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.registry_not_frozen", "interaction effect mapping must be frozen before validation"));
    }
    if (plan.candidate.type != interaction_type_)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.interaction_effects.mapping_mismatch", "interaction plan does not match this effect mapping"));
    }
    if (!plan.candidate.actor.IsValid() || !plan.candidate.target.IsValid() ||
        plan.context.actor != plan.candidate.actor || plan.context.target != plan.candidate.target)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.interaction_effects.plan_invalid", "interaction actor/target context is invalid or inconsistent"));
    }
    return ValidateMapping();
}

foundation::Result<void> InteractionEffectExecutor::Commit(
    const interaction::InteractionPlan& plan,
    interaction::InteractionExecutionId execution) noexcept
{
    try
    {
        const auto valid = Validate(plan);
        if (!valid)
        {
            return valid;
        }
        if (!execution.IsValid())
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.interaction_effects.execution_invalid", "interaction execution id is invalid"));
        }

        for (std::uint32_t effect_index = 0; effect_index < definitions_.size(); ++effect_index)
        {
            const DeliveryKey key{execution, effect_index};
            auto existing = deliveries_.find(key);
            if (existing != deliveries_.end())
            {
                switch (existing->second.state)
                {
                case InteractionEffectDeliveryState::Applied:
                    continue;
                case InteractionEffectDeliveryState::Rejected:
                    return foundation::Result<void>::Failure(Error(
                        "gameplay.interaction_effects.delivery_rejected",
                        "mapped effect delivery was rejected and cannot complete the interaction"));
                case InteractionEffectDeliveryState::Pending:
                    if (existing->second.reconciliation != InteractionEffectReconciliationOutcome::ConfirmedNotApplied)
                    {
                        return foundation::Result<void>::Failure(Error(
                            "gameplay.interaction_effects.reconciliation_required",
                            "pending effect delivery is not proven retry-safe"));
                    }
                    break;
                case InteractionEffectDeliveryState::ReconciliationRequired:
                    return foundation::Result<void>::Failure(Error(
                        "gameplay.interaction_effects.reconciliation_required",
                        "effect delivery may have crossed the commit boundary and must not be executed again automatically"));
                }
            }

            const auto definition = definitions_[effect_index];
            if (existing == deliveries_.end())
            {
                InteractionEffectDeliveryRecord record;
                record.execution = execution;
                record.interaction_type = interaction_type_;
                record.effect_index = effect_index;
                record.definition = definition;
                record.state = InteractionEffectDeliveryState::Pending;
                const auto [inserted, did_insert] = deliveries_.emplace(key, std::move(record));
                if (!did_insert)
                {
                    return foundation::Result<void>::Failure(
                        Error("gameplay.interaction_effects.delivery_conflict", "effect delivery key already exists"));
                }
                existing = inserted;
            }

            // A Pending record can only reach this point when it is new in this
            // call or an external reconciler proved that the previous uncertain
            // attempt did not cross the commit boundary. Once a new attempt starts,
            // that proof is consumed and any new failure becomes independently
            // uncertain again.
            existing->second.reconciliation = InteractionEffectReconciliationOutcome::None;
            existing->second.effect_execution = {};

            effects::EffectRequest request;
            request.definition = definition;
            request.source = plan.context.actor;
            request.instigator = plan.context.actor;
            request.targets = {plan.context.target};
            request.context = plan.context.gameplay;
            if (request.context.operation.IsValid())
            {
                request.context.parent_operation = request.context.operation;
            }
            request.context.operation = DeliveryOperation(execution, effect_index, definition);
            if (!request.context.correlation.IsValid())
            {
                request.context.correlation = DeliveryCorrelation(execution);
            }
            request.context.actor = plan.context.actor;
            request.context.instigator = plan.context.actor;
            request.context.source = plan.context.actor;

            auto result = [&]() -> foundation::Result<effects::EffectExecutionResult> {
                try
                {
                    return effects_.Execute(std::move(request));
                }
                catch (...)
                {
                    existing->second.state = InteractionEffectDeliveryState::ReconciliationRequired;
                    return foundation::Result<effects::EffectExecutionResult>::Failure(Error(
                        "gameplay.interaction_effects.effect_exception",
                        "effect execution threw after delivery entered the external commit boundary"));
                }
            }();
            if (!result)
            {
                // EffectService can fail after earlier derived waves have already
                // crossed handler commit boundaries. Preserve the uncertain leg and
                // refuse blind retry rather than risk applying the same effect twice.
                existing->second.state = InteractionEffectDeliveryState::ReconciliationRequired;
                return foundation::Result<void>::Failure(result.GetError());
            }

            auto outcome = std::move(result).Value();
            existing->second.effect_execution = outcome.execution;
            switch (outcome.disposition)
            {
            case effects::EffectBatchDisposition::Succeeded:
                existing->second.state = InteractionEffectDeliveryState::Applied;
                break;
            case effects::EffectBatchDisposition::Rejected:
                existing->second.state = InteractionEffectDeliveryState::Rejected;
                return foundation::Result<void>::Failure(Error(
                    "gameplay.interaction_effects.delivery_rejected",
                    "mapped effect definition rejected the interaction delivery"));
            case effects::EffectBatchDisposition::PartiallyApplied:
            case effects::EffectBatchDisposition::Failed:
                existing->second.state = InteractionEffectDeliveryState::ReconciliationRequired;
                return foundation::Result<void>::Failure(Error(
                    "gameplay.interaction_effects.reconciliation_required",
                    "mapped effect did not complete atomically; automatic retry is unsafe"));
            }
        }
        return foundation::Result<void>::Success();
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.interaction_effects.exception", "interaction effect executor caught an unexpected exception"));
    }
}

InteractionEffectsSnapshot InteractionEffectExecutor::CaptureSnapshot() const
{
    InteractionEffectsSnapshot snapshot;
    snapshot.deliveries.reserve(deliveries_.size());
    for (const auto& [_, record] : deliveries_)
    {
        snapshot.deliveries.push_back(record);
    }
    std::sort(snapshot.deliveries.begin(), snapshot.deliveries.end(), [](const auto& left, const auto& right) {
        if (left.execution != right.execution)
        {
            return left.execution < right.execution;
        }
        return left.effect_index < right.effect_index;
    });
    return snapshot;
}

foundation::Result<void> InteractionEffectExecutor::RestoreSnapshot(InteractionEffectsSnapshot snapshot)
{
    if (!frozen_)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.registry_not_frozen", "interaction effect mapping must be frozen before restore"));
    }

    std::unordered_map<DeliveryKey, InteractionEffectDeliveryRecord, DeliveryKeyHash> restored;
    restored.reserve(snapshot.deliveries.size());
    std::vector<effects::EffectExecutionId> restored_effect_executions;
    restored_effect_executions.reserve(snapshot.deliveries.size());
    for (auto record : snapshot.deliveries)
    {
        if (!record.execution.IsValid() || record.interaction_type != interaction_type_ ||
            record.effect_index >= definitions_.size() || record.definition != definitions_[record.effect_index])
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.interaction_effects.snapshot_invalid", "interaction effect delivery record does not match frozen mapping"));
        }
        if (record.reconciliation == InteractionEffectReconciliationOutcome::ConfirmedApplied &&
            record.state != InteractionEffectDeliveryState::Applied)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.interaction_effects.snapshot_invalid", "confirmed-applied delivery must be in Applied state"));
        }
        if (record.reconciliation == InteractionEffectReconciliationOutcome::ConfirmedRejected &&
            record.state != InteractionEffectDeliveryState::Rejected)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.interaction_effects.snapshot_invalid", "confirmed-rejected delivery must be in Rejected state"));
        }
        if (record.reconciliation == InteractionEffectReconciliationOutcome::ConfirmedNotApplied &&
            (record.state != InteractionEffectDeliveryState::Pending || record.effect_execution.IsValid()))
        {
            return foundation::Result<void>::Failure(Error(
                "gameplay.interaction_effects.snapshot_invalid",
                "confirmed-not-applied delivery must be retry-safe Pending without an effect execution id"));
        }
        if (record.state == InteractionEffectDeliveryState::Pending && record.effect_execution.IsValid())
        {
            return foundation::Result<void>::Failure(Error(
                "gameplay.interaction_effects.snapshot_invalid",
                "pending delivery cannot already reference an effect execution"));
        }
        if (record.reconciliation == InteractionEffectReconciliationOutcome::None &&
            (record.state == InteractionEffectDeliveryState::Applied ||
             record.state == InteractionEffectDeliveryState::Rejected) &&
            !record.effect_execution.IsValid())
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.interaction_effects.snapshot_invalid", "terminal delivery record requires a valid effect execution id"));
        }
        if (record.state == InteractionEffectDeliveryState::ReconciliationRequired &&
            record.reconciliation != InteractionEffectReconciliationOutcome::None)
        {
            return foundation::Result<void>::Failure(Error(
                "gameplay.interaction_effects.snapshot_invalid",
                "unresolved delivery cannot already contain a reconciliation outcome"));
        }
        if (record.state == InteractionEffectDeliveryState::Pending &&
            record.reconciliation == InteractionEffectReconciliationOutcome::None)
        {
            // A save captured while a non-transactional external Effect commit was
            // in flight cannot prove whether side effects crossed the boundary.
            record.state = InteractionEffectDeliveryState::ReconciliationRequired;
        }
        if (record.effect_execution.IsValid())
        {
            if (std::find(restored_effect_executions.begin(), restored_effect_executions.end(), record.effect_execution) !=
                restored_effect_executions.end())
            {
                return foundation::Result<void>::Failure(Error(
                    "gameplay.interaction_effects.snapshot_duplicate_execution",
                    "effect execution id is referenced by more than one interaction delivery"));
            }
            restored_effect_executions.push_back(record.effect_execution);
        }
        const DeliveryKey key{record.execution, record.effect_index};
        if (!restored.emplace(key, std::move(record)).second)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.interaction_effects.snapshot_duplicate", "duplicate interaction effect delivery record"));
        }
    }

    deliveries_.swap(restored);
    return foundation::Result<void>::Success();
}

foundation::Result<InteractionEffectReconciliationStatus> InteractionEffectExecutor::ReconcileDelivery(
    const InteractionEffectReconciliationRequest& request)
{
    if (!frozen_)
    {
        return foundation::Result<InteractionEffectReconciliationStatus>::Failure(
            Error("gameplay.registry_not_frozen", "interaction effect mapping must be frozen before reconciliation"));
    }
    if (!request.execution.IsValid() || request.effect_index >= definitions_.size())
    {
        return foundation::Result<InteractionEffectReconciliationStatus>::Failure(Error(
            "gameplay.interaction_effects.reconciliation_key_invalid",
            "reconciliation requires a valid interaction execution and mapped effect index"));
    }
    if (request.outcome == InteractionEffectReconciliationOutcome::None)
    {
        return foundation::Result<InteractionEffectReconciliationStatus>::Failure(Error(
            "gameplay.interaction_effects.reconciliation_outcome_invalid",
            "reconciliation requires a proven applied, not-applied, or rejected outcome"));
    }
    if (request.outcome != InteractionEffectReconciliationOutcome::ConfirmedApplied &&
        request.effect_execution.IsValid())
    {
        return foundation::Result<InteractionEffectReconciliationStatus>::Failure(Error(
            "gameplay.interaction_effects.reconciliation_evidence_invalid",
            "effect execution evidence is only valid for ConfirmedApplied resolution"));
    }

    const auto found = deliveries_.find(DeliveryKey{request.execution, request.effect_index});
    if (found == deliveries_.end())
    {
        return foundation::Result<InteractionEffectReconciliationStatus>::Failure(Error(
            "gameplay.interaction_effects.delivery_missing",
            "interaction effect delivery does not exist"));
    }

    auto& record = found->second;
    if (request.effect_execution.IsValid())
    {
        for (const auto& [key, other] : deliveries_)
        {
            if (!(key == found->first) && other.effect_execution == request.effect_execution)
            {
                return foundation::Result<InteractionEffectReconciliationStatus>::Failure(Error(
                    "gameplay.interaction_effects.reconciliation_evidence_conflict",
                    "effect execution evidence already belongs to another interaction delivery"));
            }
        }
    }
    if (record.reconciliation != InteractionEffectReconciliationOutcome::None)
    {
        if (record.reconciliation != request.outcome)
        {
            return foundation::Result<InteractionEffectReconciliationStatus>::Failure(Error(
                "gameplay.interaction_effects.reconciliation_conflict",
                "delivery was already resolved with a contradictory reconciliation outcome"));
        }
        if (request.outcome == InteractionEffectReconciliationOutcome::ConfirmedApplied &&
            request.effect_execution.IsValid() && record.effect_execution.IsValid() &&
            record.effect_execution != request.effect_execution)
        {
            return foundation::Result<InteractionEffectReconciliationStatus>::Failure(Error(
                "gameplay.interaction_effects.reconciliation_evidence_conflict",
                "confirmed effect execution id contradicts previously stored evidence"));
        }
        if (request.outcome == InteractionEffectReconciliationOutcome::ConfirmedApplied &&
            request.effect_execution.IsValid() && !record.effect_execution.IsValid())
        {
            record.effect_execution = request.effect_execution;
        }
        return foundation::Result<InteractionEffectReconciliationStatus>::Success(
            InteractionEffectReconciliationStatus::AlreadyResolved);
    }

    if (record.state != InteractionEffectDeliveryState::ReconciliationRequired)
    {
        return foundation::Result<InteractionEffectReconciliationStatus>::Failure(Error(
            "gameplay.interaction_effects.reconciliation_state_invalid",
            "only ReconciliationRequired delivery can be resolved"));
    }

    switch (request.outcome)
    {
    case InteractionEffectReconciliationOutcome::ConfirmedApplied:
        if (request.effect_execution.IsValid() && record.effect_execution.IsValid() &&
            record.effect_execution != request.effect_execution)
        {
            return foundation::Result<InteractionEffectReconciliationStatus>::Failure(Error(
                "gameplay.interaction_effects.reconciliation_evidence_conflict",
                "confirmed effect execution id contradicts delivery evidence"));
        }
        if (request.effect_execution.IsValid())
        {
            record.effect_execution = request.effect_execution;
        }
        record.state = InteractionEffectDeliveryState::Applied;
        break;
    case InteractionEffectReconciliationOutcome::ConfirmedNotApplied:
        record.effect_execution = {};
        record.state = InteractionEffectDeliveryState::Pending;
        break;
    case InteractionEffectReconciliationOutcome::ConfirmedRejected:
        record.state = InteractionEffectDeliveryState::Rejected;
        break;
    case InteractionEffectReconciliationOutcome::None:
        return foundation::Result<InteractionEffectReconciliationStatus>::Failure(Error(
            "gameplay.interaction_effects.reconciliation_outcome_invalid",
            "reconciliation outcome is invalid"));
    }
    record.reconciliation = request.outcome;
    return foundation::Result<InteractionEffectReconciliationStatus>::Success(
        InteractionEffectReconciliationStatus::Resolved);
}

const InteractionEffectDeliveryRecord* InteractionEffectExecutor::FindDelivery(
    interaction::InteractionExecutionId execution,
    std::uint32_t effect_index) const noexcept
{
    const auto found = deliveries_.find(DeliveryKey{execution, effect_index});
    return found == deliveries_.end() ? nullptr : &found->second;
}

void InteractionEffectExecutor::PruneTerminalExecution(interaction::InteractionExecutionId execution) noexcept
{
    for (auto it = deliveries_.begin(); it != deliveries_.end();)
    {
        if (it->first.execution == execution &&
            (it->second.state == InteractionEffectDeliveryState::Applied ||
             it->second.state == InteractionEffectDeliveryState::Rejected))
        {
            it = deliveries_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
} // namespace epidemic::gameplay::interaction_effects_integration
