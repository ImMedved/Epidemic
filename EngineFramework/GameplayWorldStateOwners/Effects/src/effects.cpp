#include "Epidemic/GameFramework/Effects/effects.h"

#include "Epidemic/Core/task_scheduler.h"
#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <optional>

namespace epidemic::gameplay::effects
{
namespace
{
[[nodiscard]] foundation::Error Error(std::string code, std::string message)
{
    return foundation::Error::Create(std::move(code), std::move(message));
}

[[nodiscard]] std::int64_t SaturatingAdd(std::int64_t left, std::int64_t right) noexcept
{
    if (right > 0 && left > std::numeric_limits<std::int64_t>::max() - right)
    {
        return std::numeric_limits<std::int64_t>::max();
    }
    if (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)
    {
        return std::numeric_limits<std::int64_t>::min();
    }
    return left + right;
}

[[nodiscard]] std::int64_t SaturatingMultiply(std::int64_t left, std::int64_t right) noexcept
{
    if (left == 0 || right == 0)
    {
        return 0;
    }
    if (left == -1 && right == std::numeric_limits<std::int64_t>::min())
    {
        return std::numeric_limits<std::int64_t>::max();
    }
    if (right == -1 && left == std::numeric_limits<std::int64_t>::min())
    {
        return std::numeric_limits<std::int64_t>::max();
    }
    if (left > 0)
    {
        if (right > 0 && left > std::numeric_limits<std::int64_t>::max() / right)
        {
            return std::numeric_limits<std::int64_t>::max();
        }
        if (right < 0 && right < std::numeric_limits<std::int64_t>::min() / left)
        {
            return std::numeric_limits<std::int64_t>::min();
        }
    }
    else
    {
        if (right > 0 && left < std::numeric_limits<std::int64_t>::min() / right)
        {
            return std::numeric_limits<std::int64_t>::min();
        }
        if (right < 0 && left < std::numeric_limits<std::int64_t>::max() / right)
        {
            return std::numeric_limits<std::int64_t>::max();
        }
    }
    return left * right;
}

[[nodiscard]] std::int64_t ScaleMagnitude(std::int64_t magnitude, std::int64_t scale) noexcept
{
    constexpr std::int64_t kScale = 1'000'000;
    const auto quotient = magnitude / kScale;
    const auto remainder = magnitude % kScale;
    const auto scale_quotient = scale / kScale;
    const auto scale_remainder = scale % kScale;

    auto result = SaturatingMultiply(quotient, scale);
    result = SaturatingAdd(result, SaturatingMultiply(remainder, scale_quotient));
    const auto fractional = SaturatingMultiply(remainder, scale_remainder) / kScale;
    return SaturatingAdd(result, fractional);
}
} // namespace

EffectService::EffectService()
    : execution_ids_(GameplayObjectId::FromString("framework.effects.executions").High()),
      deferred_ids_(GameplayObjectId::FromString("framework.effects.deferred").High())
{
}

foundation::Result<EffectTypeId> EffectService::RegisterHandler(
    std::string_view canonical_name,
    std::shared_ptr<IEffectHandler> handler,
    TypeId payload_type,
    std::size_t max_payload_bytes,
    PayloadValidator validator)
{
    if (frozen_)
    {
        return foundation::Result<EffectTypeId>::Failure(Error("gameplay.registry_frozen", "effect handler registry is frozen"));
    }
    if (canonical_name.empty() || !handler)
    {
        return foundation::Result<EffectTypeId>::Failure(Error("gameplay.effect_handler_invalid", "effect handler name and instance are required"));
    }
    const auto id = EffectTypeId::FromString(canonical_name);
    if (handler->Type() != id)
    {
        return foundation::Result<EffectTypeId>::Failure(Error("gameplay.effect_handler_type_mismatch", "effect handler Type() does not match canonical name"));
    }
    if (handlers_.contains(id))
    {
        return foundation::Result<EffectTypeId>::Failure(Error("gameplay.already_registered", "effect handler is already registered"));
    }
    if (payload_type.IsValid() && max_payload_bytes == 0)
    {
        return foundation::Result<EffectTypeId>::Failure(Error("gameplay.effect_payload_schema_invalid", "typed effect payload requires non-zero max bytes"));
    }
    handlers_.emplace(id, HandlerEntry{std::string(canonical_name), std::move(handler), payload_type, max_payload_bytes, std::move(validator)});
    return foundation::Result<EffectTypeId>::Success(id);
}

foundation::Result<EffectDefinitionId> EffectService::RegisterDefinition(EffectDefinition definition)
{
    if (frozen_)
    {
        return foundation::Result<EffectDefinitionId>::Failure(Error("gameplay.registry_frozen", "effect definition registry is frozen"));
    }
    if (definition.canonical_name.empty() || definition.steps.empty())
    {
        return foundation::Result<EffectDefinitionId>::Failure(Error("gameplay.effect_definition_invalid", "effect definition requires name and at least one step"));
    }
    const auto expected = EffectDefinitionId::FromString(definition.canonical_name);
    if (!definition.id.IsValid())
    {
        definition.id = expected;
    }
    if (definition.id != expected)
    {
        return foundation::Result<EffectDefinitionId>::Failure(Error("gameplay.effect_definition_id_mismatch", "effect definition id does not match canonical name"));
    }
    if (definitions_.contains(definition.id))
    {
        return foundation::Result<EffectDefinitionId>::Failure(Error("gameplay.already_registered", "effect definition is already registered"));
    }
    for (const auto& step : definition.steps)
    {
        const auto handler = handlers_.find(step.type);
        if (handler == handlers_.end())
        {
            return foundation::Result<EffectDefinitionId>::Failure(Error("gameplay.effect_type_unknown", "effect definition references unregistered effect type"));
        }
        const auto payload = ValidatePayload(handler->second, step.payload);
        if (!payload)
        {
            return foundation::Result<EffectDefinitionId>::Failure(payload.GetError());
        }
    }
    const auto id = definition.id;
    definitions_.emplace(id, std::move(definition));
    return foundation::Result<EffectDefinitionId>::Success(id);
}

const EffectDefinition* EffectService::FindDefinition(EffectDefinitionId id) const noexcept
{
    const auto found = definitions_.find(id);
    return found == definitions_.end() ? nullptr : &found->second;
}

foundation::Result<void> EffectService::ValidatePayload(
    const HandlerEntry& entry,
    const RegisteredEffectPayload& payload) const
{
    if (!entry.payload_type.IsValid())
    {
        if (payload.type.IsValid() || !payload.bytes.empty())
        {
            return foundation::Result<void>::Failure(Error("gameplay.effect_payload_unexpected", "effect type does not accept payload"));
        }
        return foundation::Result<void>::Success();
    }
    if (payload.type != entry.payload_type || payload.bytes.size() > entry.max_payload_bytes)
    {
        return foundation::Result<void>::Failure(Error("gameplay.effect_payload_invalid", "effect payload type or size is invalid"));
    }
    if (entry.validator && !entry.validator(payload.bytes))
    {
        return foundation::Result<void>::Failure(Error("gameplay.effect_payload_rejected", "effect payload validator rejected payload"));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<std::vector<EffectOperation>> EffectService::ExpandRequest(
    const EffectRequest& request,
    EffectExecutionId execution) const
{
    const auto* definition = FindDefinition(request.definition);
    if (definition == nullptr || request.scale_micro < 0)
    {
        return foundation::Result<std::vector<EffectOperation>>::Failure(Error("gameplay.effect_request_invalid", "effect definition and non-negative scale are required"));
    }
    if (request.targets.empty())
    {
        return foundation::Result<std::vector<EffectOperation>>::Failure(Error("gameplay.effect_targets_empty", "effect request requires at least one target"));
    }

    auto targets = request.targets;
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    if (std::any_of(targets.begin(), targets.end(), [](GameplayObjectRef target) { return !target.IsValid(); }))
    {
        return foundation::Result<std::vector<EffectOperation>>::Failure(Error("gameplay.effect_target_invalid", "effect target must be a valid gameplay object reference"));
    }

    std::vector<EffectOperation> operations;
    std::uint64_t local_sequence = 0;
    for (std::uint32_t step_index = 0; step_index < definition->steps.size(); ++step_index)
    {
        const auto& step = definition->steps[step_index];
        const auto append = [&](GameplayObjectRef target) {
            auto context = request.context;
            context.source = request.source;
            context.instigator = request.instigator;
            operations.push_back(EffectOperation{execution,
                                                 0,
                                                 step_index,
                                                 local_sequence++,
                                                 step.type,
                                                 target,
                                                 ScaleMagnitude(step.magnitude_micro, request.scale_micro),
                                                 step.payload,
                                                 context});
        };
        if (step.selector == EffectTargetSelector::FirstTarget)
        {
            append(targets.front());
        }
        else
        {
            for (const auto target : targets)
            {
                append(target);
            }
        }
    }
    return foundation::Result<std::vector<EffectOperation>>::Success(std::move(operations));
}

EffectOperationDisposition EffectService::ValidateCapabilities(
    const HandlerEntry& entry,
    GameplayObjectRef target) const
{
    const auto capabilities = entry.handler->Capabilities();
    if (!target_state_provider_)
    {
        return (capabilities.requires_materialized || capabilities.requires_runtime_projection)
                   ? EffectOperationDisposition::Unavailable
                   : EffectOperationDisposition::Applied;
    }
    const auto state = target_state_provider_->Resolve(target);
    if (!state.known)
    {
        return EffectOperationDisposition::InvalidTarget;
    }
    if (capabilities.requires_materialized && !state.materialized)
    {
        return EffectOperationDisposition::Unavailable;
    }
    if (capabilities.requires_runtime_projection && !state.runtime_projection)
    {
        return EffectOperationDisposition::Unavailable;
    }
    if (!capabilities.abstract_capable && !state.materialized)
    {
        return EffectOperationDisposition::Unavailable;
    }
    return EffectOperationDisposition::Applied;
}

EffectOperationDisposition EffectService::MapPrepareDisposition(EffectPrepareDisposition disposition) const noexcept
{
    switch (disposition)
    {
    case EffectPrepareDisposition::Accepted:
        return EffectOperationDisposition::Applied;
    case EffectPrepareDisposition::Rejected:
        return EffectOperationDisposition::Rejected;
    case EffectPrepareDisposition::NoOp:
        return EffectOperationDisposition::NoOp;
    case EffectPrepareDisposition::Unavailable:
        return EffectOperationDisposition::Unavailable;
    case EffectPrepareDisposition::InvalidTarget:
        return EffectOperationDisposition::InvalidTarget;
    case EffectPrepareDisposition::Unsupported:
        return EffectOperationDisposition::Unsupported;
    }
    return EffectOperationDisposition::Failed;
}

foundation::Result<std::vector<EffectService::PreparedOperation>> EffectService::PrepareWave(
    std::vector<EffectOperation> operations,
    EffectExecutionPolicy policy,
    core::tasks::ITaskScheduler* scheduler)
{
    std::sort(operations.begin(), operations.end(), [](const auto& left, const auto& right) {
        if (left.wave != right.wave)
        {
            return left.wave < right.wave;
        }
        if (left.step_index != right.step_index)
        {
            return left.step_index < right.step_index;
        }
        if (left.target != right.target)
        {
            return left.target < right.target;
        }
        return left.local_sequence < right.local_sequence;
    });

    std::vector<PreparedOperation> prepared(operations.size());
    bool can_parallelize = scheduler != nullptr && operations.size() > 1;
    for (const auto& operation : operations)
    {
        const auto handler = handlers_.find(operation.type);
        if (handler == handlers_.end() || !handler->second.handler->Capabilities().thread_safe_prepare)
        {
            can_parallelize = false;
            break;
        }
    }

    auto prepare_one = [this, &operations, &prepared](std::size_t index) {
        auto& output = prepared[index];
        output.operation = operations[index];
        const auto handler_found = handlers_.find(output.operation.type);
        if (handler_found == handlers_.end())
        {
            output.preflight_disposition = EffectOperationDisposition::Unsupported;
            return;
        }
        const auto payload_result = ValidatePayload(handler_found->second, output.operation.payload);
        if (!payload_result)
        {
            output.preflight_disposition = EffectOperationDisposition::Unsupported;
            return;
        }
        const auto capability = ValidateCapabilities(handler_found->second, output.operation.target);
        if (capability != EffectOperationDisposition::Applied)
        {
            output.preflight_disposition = capability;
            return;
        }
        const auto result = handler_found->second.handler->Prepare(output.operation);
        if (!result)
        {
            output.preflight_disposition = EffectOperationDisposition::Failed;
            return;
        }
        output.prepared = result.Value();
        output.preflight_disposition = MapPrepareDisposition(output.prepared.disposition);
        output.commit = output.prepared.disposition == EffectPrepareDisposition::Accepted;
    };

    if (can_parallelize)
    {
        core::tasks::TaskGroup group;
        for (std::size_t i = 0; i < operations.size(); ++i)
        {
            [[maybe_unused]] const auto handle = scheduler->Schedule([prepare_one, i]() { prepare_one(i); }, group, "GameplayEffectPrepare");
        }
        scheduler->Wait(group);
    }
    else
    {
        for (std::size_t i = 0; i < operations.size(); ++i)
        {
            prepare_one(i);
        }
    }

    if (policy == EffectExecutionPolicy::RequireAllPrepared)
    {
        const bool all = std::all_of(prepared.begin(), prepared.end(), [](const PreparedOperation& item) {
            return item.preflight_disposition == EffectOperationDisposition::Applied || item.preflight_disposition == EffectOperationDisposition::NoOp;
        });
        if (!all)
        {
            for (auto& item : prepared)
            {
                if (item.preflight_disposition == EffectOperationDisposition::Applied)
                {
                    item.preflight_disposition = EffectOperationDisposition::Rejected;
                }
                item.commit = false;
            }
        }
    }
    return foundation::Result<std::vector<PreparedOperation>>::Success(std::move(prepared));
}

foundation::Result<EffectExecutionResult> EffectService::Execute(
    EffectRequest request,
    EffectExecutionBudget budget,
    core::tasks::ITaskScheduler* scheduler)
{
    const auto* definition = FindDefinition(request.definition);
    if (definition == nullptr)
    {
        return foundation::Result<EffectExecutionResult>::Failure(Error("gameplay.effect_definition_unknown", "effect definition is not registered"));
    }
    const auto raw_execution = execution_ids_.Next();
    if (!raw_execution.IsValid())
    {
        return foundation::Result<EffectExecutionResult>::Failure(Error("gameplay.effect_execution_id_exhausted", "effect execution id generator is exhausted"));
    }
    const EffectExecutionId execution{raw_execution};
    auto expanded = ExpandRequest(request, execution);
    if (!expanded)
    {
        return foundation::Result<EffectExecutionResult>::Failure(expanded.GetError());
    }

    ++executions_;
    targets_ += request.targets.size();
    RecordChange(EffectChange{0, EffectChangeKind::ExecutionStarted, execution, {}, {}, {}, EffectOperationDisposition::Applied, request.context});

    EffectExecutionResult result;
    result.execution = execution;
    std::vector<EffectOperation> wave = std::move(expanded).Value();
    std::uint64_t total_operations = 0;
    bool any_applied = false;
    bool any_rejected = false;
    bool any_failed = false;
    bool any_noop = false;

    for (std::uint32_t wave_index = 0; !wave.empty(); ++wave_index)
    {
        if (wave_index >= budget.max_waves || total_operations + wave.size() > budget.max_effects)
        {
            ++budget_exhaustions_;
            any_failed = true;
            RecordChange(EffectChange{0, EffectChangeKind::BudgetExceeded, execution, {}, {}, {}, EffectOperationDisposition::BudgetExceeded, request.context});
            for (const auto& operation : wave)
            {
                result.operations.push_back(EffectOperationResult{operation, EffectOperationDisposition::BudgetExceeded});
            }
            break;
        }

        for (auto& operation : wave)
        {
            operation.wave = wave_index;
        }
        auto prepared_result = PrepareWave(std::move(wave), definition->policy, scheduler);
        if (!prepared_result)
        {
            return foundation::Result<EffectExecutionResult>::Failure(prepared_result.GetError());
        }
        auto prepared = std::move(prepared_result).Value();
        std::vector<EffectOperation> next_wave;
        std::uint64_t derived_sequence = 0;

        for (auto& item : prepared)
        {
            ++total_operations;
            ++operations_;
            auto disposition = item.preflight_disposition;
            if (item.commit)
            {
                const auto handler = handlers_.find(item.operation.type);
                if (handler == handlers_.end())
                {
                    disposition = EffectOperationDisposition::Unsupported;
                }
                else
                {
                    const auto commit = handler->second.handler->Commit(item.operation, item.prepared.commit_token);
                    if (!commit)
                    {
                        disposition = EffectOperationDisposition::Failed;
                    }
                    else
                    {
                        auto commit_value = std::move(commit).Value();
                        disposition = commit_value.disposition == EffectCommitDisposition::Applied
                                          ? EffectOperationDisposition::Applied
                                          : EffectOperationDisposition::NoOp;
                        if (commit_value.derived_effects.size() > budget.max_derived_per_parent)
                        {
                            ++budget_exhaustions_;
                            any_failed = true;
                            RecordChange(EffectChange{0, EffectChangeKind::BudgetExceeded, execution, item.operation.type,
                                                      item.operation.target, {}, EffectOperationDisposition::BudgetExceeded,
                                                      item.operation.context});
                        }
                        else
                        {
                            for (auto derived : commit_value.derived_effects)
                            {
                                derived.execution = execution;
                                derived.wave = wave_index + 1;
                                derived.local_sequence = derived_sequence++;
                                if (!derived.context.correlation.IsValid())
                                {
                                    derived.context.correlation = item.operation.context.correlation;
                                }
                                if (!derived.context.instigator.IsValid())
                                {
                                    derived.context.instigator = item.operation.context.instigator;
                                }
                                if (!derived.context.source.IsValid())
                                {
                                    derived.context.source = item.operation.context.source;
                                }
                                next_wave.push_back(std::move(derived));
                            }
                            derived_effects_ += commit_value.derived_effects.size();
                        }
                    }
                }
            }

            if (disposition == EffectOperationDisposition::Applied)
            {
                any_applied = true;
                RecordChange(EffectChange{0, EffectChangeKind::Applied, execution, item.operation.type, item.operation.target,
                                          {}, disposition, item.operation.context});
            }
            else if (disposition == EffectOperationDisposition::NoOp)
            {
                any_noop = true;
            }
            else if (disposition == EffectOperationDisposition::Rejected || disposition == EffectOperationDisposition::Unavailable ||
                     disposition == EffectOperationDisposition::InvalidTarget || disposition == EffectOperationDisposition::Unsupported)
            {
                any_rejected = true;
                ++rejections_;
                RecordChange(EffectChange{0, EffectChangeKind::Rejected, execution, item.operation.type, item.operation.target,
                                          {}, disposition, item.operation.context});
            }
            else if (disposition == EffectOperationDisposition::Failed || disposition == EffectOperationDisposition::BudgetExceeded)
            {
                any_failed = true;
                if (disposition == EffectOperationDisposition::Failed)
                {
                    RecordChange(EffectChange{0, EffectChangeKind::Failed, execution, item.operation.type, item.operation.target,
                                              {}, disposition, item.operation.context});
                }
            }
            result.operations.push_back(EffectOperationResult{item.operation, disposition});
        }

        ++result.waves;
        ++waves_;
        wave = std::move(next_wave);
    }

    if (any_failed)
    {
        result.disposition = EffectBatchDisposition::Failed;
    }
    else if (any_applied && any_rejected)
    {
        result.disposition = EffectBatchDisposition::PartiallyApplied;
    }
    else if (any_applied || any_noop)
    {
        result.disposition = EffectBatchDisposition::Succeeded;
    }
    else
    {
        result.disposition = EffectBatchDisposition::Rejected;
    }

    const auto completed_disposition = any_failed ? EffectOperationDisposition::Failed
                                      : result.disposition == EffectBatchDisposition::Rejected ? EffectOperationDisposition::Rejected
                                                                                                : EffectOperationDisposition::Applied;
    RecordChange(EffectChange{0, EffectChangeKind::ExecutionCompleted, execution, {}, {}, {}, completed_disposition, request.context});
    return foundation::Result<EffectExecutionResult>::Success(std::move(result));
}

foundation::Result<DeferredEffectId> EffectService::Defer(
    EffectRequest request,
    ClockId clock,
    GameplayTimePoint due,
    DeferredEffectPersistence persistence)
{
    auto canonical_request = ExpandRequest(request, {});
    if (!canonical_request || !clock.IsValid())
    {
        return foundation::Result<DeferredEffectId>::Failure(Error("gameplay.deferred_effect_invalid", "deferred effect requires valid registered definition, clock and targets"));
    }
    const auto raw = deferred_ids_.Next();
    if (!raw.IsValid())
    {
        return foundation::Result<DeferredEffectId>::Failure(Error("gameplay.deferred_effect_id_exhausted", "deferred effect id generator is exhausted"));
    }
    const DeferredEffectId id{raw};
    request.targets.clear();
    for (const auto& operation : canonical_request.Value())
    {
        if (std::find(request.targets.begin(), request.targets.end(), operation.target) == request.targets.end())
        {
            request.targets.push_back(operation.target);
        }
    }
    deferred_.emplace(id, DeferredEffectRecord{id, std::move(request), clock, due, std::nullopt, persistence});
    RecordChange(EffectChange{0, EffectChangeKind::DeferredCreated, {}, {}, {}, id, EffectOperationDisposition::Applied, deferred_.at(id).request.context});
    return foundation::Result<DeferredEffectId>::Success(id);
}

foundation::Result<void> EffectService::BindDeferredSchedule(DeferredEffectId id, ScheduleId schedule)
{
    const auto found = deferred_.find(id);
    if (found == deferred_.end() || !schedule.IsValid())
    {
        return foundation::Result<void>::Failure(Error("gameplay.deferred_effect_unknown", "deferred effect or schedule is invalid"));
    }
    found->second.schedule = schedule;
    return foundation::Result<void>::Success();
}

foundation::Result<void> EffectService::CancelDeferred(DeferredEffectId id, GameplayContext context)
{
    const auto found = deferred_.find(id);
    if (found == deferred_.end())
    {
        return foundation::Result<void>::Failure(Error("gameplay.deferred_effect_unknown", "deferred effect does not exist"));
    }
    EffectChange change{0, EffectChangeKind::DeferredCancelled, {}, {}, {}, id, EffectOperationDisposition::Applied, context};
    change.schedule = found->second.schedule;
    RecordChange(std::move(change));
    deferred_.erase(found);
    return foundation::Result<void>::Success();
}

std::uint64_t EffectService::CancelDeferredTargeting(GameplayObjectRef target, GameplayContext context)
{
    std::vector<DeferredEffectId> ids;
    for (const auto& [id, record] : deferred_)
    {
        if (std::find(record.request.targets.begin(), record.request.targets.end(), target) != record.request.targets.end())
        {
            ids.push_back(id);
        }
    }
    std::sort(ids.begin(), ids.end());
    for (const auto id : ids)
    {
        [[maybe_unused]] const auto cancelled = CancelDeferred(id, context);
    }
    return ids.size();
}

const DeferredEffectRecord* EffectService::FindDeferred(DeferredEffectId id) const noexcept
{
    const auto found = deferred_.find(id);
    return found == deferred_.end() ? nullptr : &found->second;
}

std::vector<DeferredEffectRecord> EffectService::UnscheduledDeferred() const
{
    std::vector<DeferredEffectRecord> result;
    for (const auto& [_, record] : deferred_)
    {
        if (!record.schedule.has_value())
        {
            result.push_back(record);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
    return result;
}

foundation::Result<EffectRequest> EffectService::TakeDeferredBySchedule(ScheduleId schedule, GameplayContext context)
{
    auto found = std::find_if(deferred_.begin(), deferred_.end(), [schedule](const auto& pair) {
        return pair.second.schedule.has_value() && *pair.second.schedule == schedule;
    });
    if (found == deferred_.end())
    {
        return foundation::Result<EffectRequest>::Failure(Error("gameplay.deferred_effect_schedule_unknown", "no deferred effect bound to schedule"));
    }
    const auto id = found->first;
    auto request = std::move(found->second.request);
    EffectChange change{0, EffectChangeKind::DeferredExecuted, {}, {}, {}, id, EffectOperationDisposition::Applied, context};
    change.schedule = found->second.schedule;
    RecordChange(std::move(change));
    deferred_.erase(found);
    return foundation::Result<EffectRequest>::Success(std::move(request));
}

void EffectService::RecordChange(EffectChange change)
{
    change.sequence = next_change_sequence_++;
    changes_.push_back(std::move(change));
}

std::vector<EffectChange> EffectService::ChangesSince(std::uint64_t sequence) const
{
    const auto found = std::upper_bound(changes_.begin(), changes_.end(), sequence, [](std::uint64_t value, const EffectChange& change) {
        return value < change.sequence;
    });
    return std::vector<EffectChange>(found, changes_.end());
}

void EffectService::PruneChangesBefore(std::uint64_t sequence)
{
    const auto found = std::lower_bound(changes_.begin(), changes_.end(), sequence, [](const EffectChange& change, std::uint64_t value) {
        return change.sequence < value;
    });
    changes_.erase(changes_.begin(), found);
}

EffectsSnapshot EffectService::CaptureSnapshot() const
{
    EffectsSnapshot snapshot;
    snapshot.execution_ids = execution_ids_.GetSnapshot();
    snapshot.deferred_ids = deferred_ids_.GetSnapshot();
    for (const auto& [_, record] : deferred_)
    {
        if (record.persistence == DeferredEffectPersistence::Persistent)
        {
            snapshot.deferred.push_back(record);
        }
    }
    std::sort(snapshot.deferred.begin(), snapshot.deferred.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
    return snapshot;
}

foundation::Result<void> EffectService::RestoreSnapshot(EffectsSnapshot snapshot)
{
    std::unordered_map<DeferredEffectId, DeferredEffectRecord, DeferredEffectIdHash> rebuilt;
    for (auto& record : snapshot.deferred)
    {
        if (!record.id.IsValid() || FindDefinition(record.request.definition) == nullptr || !record.clock.IsValid() || rebuilt.contains(record.id))
        {
            return foundation::Result<void>::Failure(Error("gameplay.effects_snapshot_invalid", "effects snapshot contains invalid deferred effect"));
        }
        rebuilt.emplace(record.id, std::move(record));
    }
    deferred_ = std::move(rebuilt);
    execution_ids_.Restore(snapshot.execution_ids);
    deferred_ids_.Restore(snapshot.deferred_ids);
    changes_.clear();
    next_change_sequence_ = 1;
    executions_ = operations_ = targets_ = derived_effects_ = waves_ = rejections_ = budget_exhaustions_ = 0;
    return foundation::Result<void>::Success();
}

EffectsDiagnostics EffectService::GetDiagnostics() const noexcept
{
    return EffectsDiagnostics{executions_, operations_, targets_, derived_effects_, waves_, rejections_, budget_exhaustions_, deferred_.size()};
}
} // namespace epidemic::gameplay::effects

