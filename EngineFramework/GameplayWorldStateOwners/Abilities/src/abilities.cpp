#include "Epidemic/GameFramework/Abilities/abilities.h"
#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_set>
#include <utility>

namespace epidemic::gameplay::abilities
{
namespace
{
[[nodiscard]] foundation::Error Error(std::string c, std::string m)
{
    return foundation::Error::Create(std::move(c), std::move(m));
}
[[nodiscard]] GameplayTimePoint Add(GameplayTimePoint p, GameplayDuration d) noexcept
{
    return SaturatingAdd(p, d);
}
[[nodiscard]] GameplayDuration ScaleDuration(GameplayDuration d, std::uint64_t count) noexcept
{
    if (d.ticks <= 0 || count == 0)
        return {};
    const auto max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (count > max / static_cast<std::uint64_t>(d.ticks))
        return GameplayDuration{std::numeric_limits<std::int64_t>::max()};
    return GameplayDuration{d.ticks * static_cast<std::int64_t>(count)};
}
[[nodiscard]] bool HasEntityTargets(const AbilityTargetSet &targets) noexcept
{
    return targets.primary.IsValid() || !targets.targets.empty();
}
[[nodiscard]] bool DirectionValid(const std::optional<std::array<std::int64_t, 3>> &direction) noexcept
{
    return direction && ((*direction)[0] != 0 || (*direction)[1] != 0 || (*direction)[2] != 0);
}
template <class Snapshot>
[[nodiscard]] bool ValidGeneratorSnapshot(const Snapshot &snapshot, std::uint64_t expected_scope,
                                          std::uint64_t max_restored_low) noexcept
{
    if (snapshot.scope != expected_scope || snapshot.scope == 0)
        return false;
    return snapshot.next == 0 || snapshot.next > max_restored_low;
}

struct CooldownKey
{
    GameplayObjectRef owner{};
    CooldownGroupId group{};
    [[nodiscard]] bool operator==(const CooldownKey &) const noexcept = default;
};
struct CooldownKeyHash
{
    [[nodiscard]] std::size_t operator()(const CooldownKey &key) const noexcept
    {
        const auto a = std::hash<GameplayObjectRef>{}(key.owner);
        const auto b = std::hash<TypeId>{}(key.group.value);
        return a ^ (b + 0x9E3779B97F4A7C15ull + (a << 6u) + (a >> 2u));
    }
};
} // namespace

AbilityService::AbilityService()
    : instance_ids_(GameplayObjectId::FromString("framework.abilities.instances").High()),
      execution_ids_(GameplayObjectId::FromString("framework.abilities.executions").High())
{
}

foundation::Result<AbilityDefinitionId> AbilityService::RegisterDefinition(AbilityDefinition d)
{
    if (frozen_)
        return foundation::Result<AbilityDefinitionId>::Failure(
            Error("gameplay.registry_frozen", "ability registry frozen"));
    if (d.canonical_name.empty())
        return foundation::Result<AbilityDefinitionId>::Failure(
            Error("gameplay.ability.definition_invalid", "ability name required"));
    const auto expected = AbilityDefinitionId::FromString(d.canonical_name);
    if (!d.id.IsValid())
        d.id = expected;
    if (d.id != expected || definitions_.contains(d.id) || d.cooldown.duration.ticks < 0 ||
        d.timing.cast_duration.ticks < 0 || d.timing.channel_interval.ticks < 0 ||
        d.timing.max_channel_duration.ticks < 0 ||
        (d.timing.kind == AbilityTimingKind::Channel && d.timing.channel_interval.ticks <= 0))
        return foundation::Result<AbilityDefinitionId>::Failure(
            Error("gameplay.ability.definition_invalid", "invalid or duplicate ability definition"));
    for (const auto &cost : d.costs)
        if (!cost.resource.IsValid() || cost.amount_micro < 0)
            return foundation::Result<AbilityDefinitionId>::Failure(
                Error("gameplay.ability.cost_invalid", "invalid ability cost"));
    for (const auto &output : d.outputs)
        if (!output.action.IsValid())
            return foundation::Result<AbilityDefinitionId>::Failure(
                Error("gameplay.ability.output_invalid", "ability output action required"));
    const auto id = d.id;
    definitions_.emplace(id, std::move(d));
    return foundation::Result<AbilityDefinitionId>::Success(id);
}

const AbilityDefinition *AbilityService::FindDefinition(AbilityDefinitionId id) const noexcept
{
    auto it = definitions_.find(id);
    return it == definitions_.end() ? nullptr : &it->second;
}

void AbilityService::Record(AbilityChange c)
{
    c.sequence = next_change_sequence_++;
    if (changes_.size() == kChangeJournalCapacity)
        changes_.pop_front();
    changes_.push_back(std::move(c));
}

bool AbilityService::IsTerminal(AbilityExecutionState state) noexcept
{
    return state == AbilityExecutionState::Completed || state == AbilityExecutionState::Cancelled ||
           state == AbilityExecutionState::Interrupted || state == AbilityExecutionState::Failed;
}

foundation::Result<AbilityInstanceId> AbilityService::Grant(GameplayObjectRef owner, AbilityDefinitionId def,
                                                            GameplayObjectRef source,
                                                            AbilityGrantPersistence persistence,
                                                            GameplayContext context)
{
    if (!owner.IsValid() || !definitions_.contains(def))
        return foundation::Result<AbilityInstanceId>::Failure(
            Error("gameplay.ability.grant_invalid", "invalid owner or definition"));
    const auto generated = instance_ids_.Next();
    if (!generated.IsValid())
        return foundation::Result<AbilityInstanceId>::Failure(
            Error("gameplay.ability.id_exhausted", "ability instance id exhausted"));
    AbilityInstance instance{{generated}, def, owner, source, persistence, true, {1}};
    const auto id = instance.id;
    instances_.emplace(id, instance);
    ++diagnostics_.instances;
    Record({0, AbilityChangeKind::Granted, owner, id, {}, context.time, context});
    return foundation::Result<AbilityInstanceId>::Success(id);
}

foundation::Result<void> AbilityService::Revoke(AbilityInstanceId id, GameplayContext context)
{
    auto it = instances_.find(id);
    if (it == instances_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.instance_missing", "ability instance missing"));
    for (const auto &[execution_id, execution] : executions_)
    {
        (void)execution_id;
        if (execution.ability == id && !IsTerminal(execution.state))
            return foundation::Result<void>::Failure(Error("gameplay.ability.active", "cannot revoke active ability"));
    }
    const auto owner = it->second.owner;
    instances_.erase(it);
    --diagnostics_.instances;
    Record({0, AbilityChangeKind::Revoked, owner, id, {}, context.time, context});
    return foundation::Result<void>::Success();
}

foundation::Result<void> AbilityService::SetAbilityEnabled(AbilityInstanceId id, bool enabled, GameplayContext context)
{
    auto it = instances_.find(id);
    if (it == instances_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.instance_missing", "ability instance missing"));
    if (it->second.enabled == enabled)
        return foundation::Result<void>::Success();
    it->second.enabled = enabled;
    ++it->second.revision.value;
    Record({0, AbilityChangeKind::EnabledChanged, it->second.owner, id, {}, context.time, context});
    return foundation::Result<void>::Success();
}

std::uint64_t AbilityService::RevokeBySource(GameplayObjectRef owner, GameplayObjectRef source, GameplayContext context)
{
    std::vector<AbilityInstanceId> ids;
    for (const auto &[id, instance] : instances_)
        if (instance.owner == owner && instance.source == source && instance.persistence == AbilityGrantPersistence::SourceBound)
            ids.push_back(id);
    std::uint64_t count = 0;
    for (auto id : ids)
        if (Revoke(id, context))
            ++count;
    return count;
}

const AbilityInstance *AbilityService::FindInstance(AbilityInstanceId id) const noexcept
{
    auto it = instances_.find(id);
    return it == instances_.end() ? nullptr : &it->second;
}

std::vector<AbilityInstance> AbilityService::GetAbilities(GameplayObjectRef owner) const
{
    std::vector<AbilityInstance> out;
    for (const auto &[id, instance] : instances_)
    {
        (void)id;
        if (instance.owner == owner)
            out.push_back(instance);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}

GameplayDuration AbilityService::CooldownRemaining(GameplayObjectRef owner, CooldownGroupId group,
                                                   GameplayTimePoint now) const noexcept
{
    GameplayTimePoint end{};
    for (const auto &cooldown : cooldowns_)
        if (cooldown.owner == owner && cooldown.group == group && cooldown.ends_at.ticks > end.ticks)
            end = cooldown.ends_at;
    if (end.ticks <= now.ticks)
        return {};
    return SaturatingDifference(end, now);
}

AbilityAvailabilityResult AbilityService::ValidateTargets(const AbilityDefinition &definition,
                                                          const AbilityInstance &instance,
                                                          const AbilityTargetSet &targets) const
{
    const bool has_entities = HasEntityTargets(targets);
    const bool has_point = targets.point_micro.has_value();
    const bool has_direction = targets.direction_micro.has_value();
    const bool has_area = targets.area_radius_micro != 0;
    const auto invalid = [](std::string_view reason) {
        return AbilityAvailabilityResult{AbilityAvailability::Unavailable, TypeId::FromString(reason)};
    };
    for (const auto &target : targets.targets)
        if (!target.IsValid())
            return invalid("ability.target_invalid");

    switch (definition.targeting)
    {
    case AbilityTargetPolicy::Self:
        if ((targets.primary.IsValid() && targets.primary != instance.owner) || !targets.targets.empty() || has_point ||
            has_direction || has_area)
            return invalid("ability.target_self_required");
        break;
    case AbilityTargetPolicy::SingleTarget:
        if (!targets.primary.IsValid() || !targets.targets.empty() || has_point || has_direction || has_area)
            return invalid("ability.target_single_required");
        break;
    case AbilityTargetPolicy::MultipleTargets:
        if ((!targets.primary.IsValid() && targets.targets.empty()) || has_point || has_direction || has_area)
            return invalid("ability.targets_required");
        break;
    case AbilityTargetPolicy::Point:
        if (!has_point || has_entities || has_direction || has_area)
            return invalid("ability.point_required");
        break;
    case AbilityTargetPolicy::Area:
        if (!has_point || targets.area_radius_micro <= 0 || has_entities || has_direction)
            return invalid("ability.area_required");
        break;
    case AbilityTargetPolicy::Direction:
        if (!DirectionValid(targets.direction_micro) || has_entities || has_point || has_area)
            return invalid("ability.direction_required");
        break;
    case AbilityTargetPolicy::None:
        if (has_entities || has_point || has_direction || has_area)
            return invalid("ability.target_not_allowed");
        break;
    }

    if (definition.requires_materialized_owner)
    {
        if (!materialization_ || !materialization_->IsMaterialized(instance.owner))
            return invalid("ability.owner_not_materialized");
    }
    if (definition.requires_materialized_target)
    {
        if (!materialization_)
            return invalid("ability.materialization_provider_missing");
        bool any = false;
        if (targets.primary.IsValid())
        {
            any = true;
            if (!materialization_->IsMaterialized(targets.primary))
                return invalid("ability.target_not_materialized");
        }
        for (const auto &target : targets.targets)
        {
            any = true;
            if (!materialization_->IsMaterialized(target))
                return invalid("ability.target_not_materialized");
        }
        if (!any)
            return invalid("ability.materialized_target_required");
    }
    return {AbilityAvailability::Available, {}};
}

AbilityAvailabilityResult AbilityService::CanActivate(AbilityInstanceId id, const AbilityTargetSet &targets,
                                                      GameplayTimePoint now) const
{
    const auto *instance = FindInstance(id);
    if (!instance || !instance->enabled)
        return {AbilityAvailability::Unavailable, TypeId::FromString("ability.instance_unavailable")};
    const auto *definition = FindDefinition(instance->definition);
    if (!definition)
        return {AbilityAvailability::Unavailable, TypeId::FromString("ability.definition_missing")};
    const auto targeting = ValidateTargets(*definition, *instance, targets);
    if (targeting.availability != AbilityAvailability::Available)
        return targeting;
    if (definition->cooldown.group.IsValid() && CooldownRemaining(instance->owner, definition->cooldown.group, now).ticks > 0)
        return {AbilityAvailability::Unavailable, TypeId::FromString("ability.cooldown")};
    const bool has_start_costs = std::any_of(definition->costs.begin(), definition->costs.end(), [](const auto &cost) {
        return cost.policy == AbilityCostPolicy::PayOnStart || cost.policy == AbilityCostPolicy::ReserveThenCommit;
    });
    if (has_start_costs && !resources_)
        return {AbilityAvailability::Unavailable, TypeId::FromString("ability.resource_provider_missing")};
    if (resources_)
    {
        std::unordered_map<AbilityResourceTypeId, std::int64_t, IdHash> totals;
        for (const auto &cost : definition->costs)
        {
            if (cost.policy != AbilityCostPolicy::PayOnStart && cost.policy != AbilityCostPolicy::ReserveThenCommit)
                continue;
            if (cost.amount_micro > std::numeric_limits<std::int64_t>::max() - totals[cost.resource])
                return {AbilityAvailability::Unavailable, TypeId::FromString("ability.resource_cost_overflow")};
            totals[cost.resource] += cost.amount_micro;
        }
        for (const auto &[resource, amount] : totals)
            if (!resources_->CanAfford(instance->owner, resource, amount))
                return {AbilityAvailability::Unavailable, TypeId::FromString("ability.resource_missing")};
    }
    if (requirements_)
    {
        try
        {
            auto result = requirements_->Check(*definition, *instance, targets, now);
            if (result.availability != AbilityAvailability::Available)
                return result;
        }
        catch (...)
        {
            return {AbilityAvailability::Unavailable, TypeId::FromString("ability.requirement_provider_failed")};
        }
    }
    return {AbilityAvailability::Available, {}};
}

AbilityAvailabilityResult AbilityService::CheckContinuity(const AbilityDefinition &definition,
                                                        const AbilityInstance &instance,
                                                        const AbilityExecution &execution,
                                                        GameplayTimePoint now) const
{
    if (definition.continuity == AbilityContinuityPolicy::ActivationOnly)
        return {AbilityAvailability::Available, {}};
    const auto targets = ValidateTargets(definition, instance, execution.targets);
    if (targets.availability != AbilityAvailability::Available)
        return targets;
    if (requirements_)
    {
        try
        {
            return requirements_->Check(definition, instance, execution.targets, now);
        }
        catch (...)
        {
            return {AbilityAvailability::Unavailable, TypeId::FromString("ability.requirement_provider_failed")};
        }
    }
    return {AbilityAvailability::Available, {}};
}

void AbilityService::ReleaseReservations(AbilityExecution &execution, GameplayContext context) noexcept
{
    if (resources_)
        for (const auto &reservation : execution.reservations)
            resources_->Release(reservation, context);
    execution.reservations.clear();
}

void AbilityService::FinalizeExecution(AbilityExecutionId id, AbilityExecutionState terminal_state,
                                       AbilityChangeKind change_kind, GameplayTimePoint now, GameplayContext context,
                                       TypeId reason)
{
    auto it = executions_.find(id);
    if (it == executions_.end())
        return;
    const auto owner = it->second.owner;
    const auto ability = it->second.ability;
    if (it->second.schedule)
        schedule_to_execution_.erase(*it->second.schedule);
    it->second.state = terminal_state;
    ++it->second.revision.value;
    Record({0, change_kind, owner, ability, id, now, context, reason});
    executions_.erase(it);
}

foundation::Result<void> AbilityService::AcquireStartCosts(const AbilityDefinition &definition, AbilityExecution &execution)
{
    if (definition.costs.empty())
        return foundation::Result<void>::Success();
    if (!resources_)
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.resource_provider_missing", "ability costs require resource provider"));
    struct Staged
    {
        AbilityResourceReservation reservation;
        AbilityCostPolicy policy;
    };
    std::vector<Staged> staged;
    for (const auto &cost : definition.costs)
    {
        if (cost.policy != AbilityCostPolicy::PayOnStart && cost.policy != AbilityCostPolicy::ReserveThenCommit)
            continue;
        auto reservation = resources_->Reserve(execution.owner, cost.resource, cost.amount_micro, execution.context);
        if (!reservation)
        {
            for (const auto &old : staged)
                resources_->Release(old.reservation, execution.context);
            return foundation::Result<void>::Failure(reservation.GetError());
        }
        staged.push_back({std::move(reservation).Value(), cost.policy});
    }
    for (auto &item : staged)
    {
        if (item.policy == AbilityCostPolicy::PayOnStart)
            resources_->Commit(item.reservation, execution.context);
        else
            execution.reservations.push_back(std::move(item.reservation));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> AbilityService::AcquireExecuteCosts(const AbilityDefinition &definition,
                                                             AbilityExecution &execution)
{
    if (definition.costs.empty())
        return foundation::Result<void>::Success();
    if (!resources_)
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.resource_provider_missing", "ability costs require resource provider"));
    std::vector<AbilityResourceReservation> staged;
    for (const auto &cost : definition.costs)
    {
        if (cost.policy != AbilityCostPolicy::PayOnExecute)
            continue;
        auto reservation = resources_->Reserve(execution.owner, cost.resource, cost.amount_micro, execution.context);
        if (!reservation)
        {
            for (const auto &old : staged)
                resources_->Release(old, execution.context);
            return foundation::Result<void>::Failure(reservation.GetError());
        }
        staged.push_back(std::move(reservation).Value());
    }
    for (const auto &token : staged)
        resources_->Commit(token, execution.context);
    return foundation::Result<void>::Success();
}

void AbilityService::CommitReservations(AbilityExecution &execution) noexcept
{
    if (!resources_)
        return;
    for (const auto &reservation : execution.reservations)
        resources_->Commit(reservation, execution.context);
    execution.reservations.clear();
}

void AbilityService::StartCooldown(const AbilityDefinition &definition, GameplayObjectRef owner, GameplayTimePoint now,
                                   GameplayContext context, AbilityInstanceId ability, AbilityExecutionId execution)
{
    if (!definition.cooldown.group.IsValid() || definition.cooldown.duration.ticks <= 0)
        return;
    const auto end = Add(now, definition.cooldown.duration);
    auto it = std::find_if(cooldowns_.begin(), cooldowns_.end(),
                           [&](const auto &c) { return c.owner == owner && c.group == definition.cooldown.group; });
    if (it == cooldowns_.end())
        cooldowns_.push_back({owner, definition.cooldown.group, end});
    else if (end.ticks > it->ends_at.ticks)
        it->ends_at = end;
    diagnostics_.cooldowns = cooldowns_.size();
    Record({0, AbilityChangeKind::CooldownStarted, owner, ability, execution, now, context});
}

foundation::Result<AbilityExecutionId> AbilityService::BeginActivation(AbilityActivationRequest request)
{
    ++diagnostics_.activation_attempts;
    SweepCooldowns(request.now, request.context);
    if (resource_reconciliation_required_)
        return foundation::Result<AbilityExecutionId>::Failure(
            Error("gameplay.ability.resource_reconciliation_required",
                  "restored ability reservations must be reconciled before activation"));
    const auto *instance = FindInstance(request.ability);
    if (!instance)
        return foundation::Result<AbilityExecutionId>::Failure(
            Error("gameplay.ability.instance_missing", "ability instance missing"));
    const auto *definition = FindDefinition(instance->definition);
    const auto availability = CanActivate(request.ability, request.targets, request.now);
    if (!definition || availability.availability != AbilityAvailability::Available)
    {
        ++diagnostics_.failed;
        return foundation::Result<AbilityExecutionId>::Failure(
            Error("gameplay.ability.unavailable", "ability is unavailable"));
    }

    const auto generated = execution_ids_.Next();
    if (!generated.IsValid())
        return foundation::Result<AbilityExecutionId>::Failure(
            Error("gameplay.ability.id_exhausted", "ability execution id exhausted"));
    AbilityExecution execution;
    execution.id = {generated};
    execution.ability = instance->id;
    execution.owner = instance->owner;
    execution.targets = std::move(request.targets);
    execution.started_at = request.now;
    execution.context = request.context;
    execution.context.actor = instance->owner;
    execution.revision = {1};
    switch (definition->timing.kind)
    {
    case AbilityTimingKind::Instant:
        execution.state = AbilityExecutionState::Executing;
        execution.due_at = request.now;
        break;
    case AbilityTimingKind::CastTime:
    case AbilityTimingKind::DelayedExecution:
        execution.state = AbilityExecutionState::Casting;
        execution.due_at = Add(request.now, definition->timing.cast_duration);
        break;
    case AbilityTimingKind::Channel:
        execution.state = AbilityExecutionState::Channeling;
        execution.next_channel_at = Add(request.now, definition->timing.channel_interval);
        execution.due_at = definition->timing.max_channel_duration.ticks > 0
                               ? Add(request.now, definition->timing.max_channel_duration)
                               : GameplayTimePoint{};
        break;
    }
    auto costs = AcquireStartCosts(*definition, execution);
    if (!costs)
    {
        ++diagnostics_.failed;
        return foundation::Result<AbilityExecutionId>::Failure(costs.GetError());
    }
    const auto id = execution.id;
    executions_.emplace(id, std::move(execution));
    ++diagnostics_.activations;
    Record({0, AbilityChangeKind::Started, instance->owner, instance->id, id, request.now, request.context});
    if (definition->cooldown.starts_on_begin)
        StartCooldown(*definition, instance->owner, request.now, request.context, instance->id, id);
    return foundation::Result<AbilityExecutionId>::Success(id);
}

std::vector<AbilityOutput> AbilityService::BuildOutputs(const AbilityDefinition &definition,
                                                        const AbilityExecution &execution,
                                                        GameplayTimePoint occurrence_at) const
{
    std::vector<AbilityOutput> out;
    out.reserve(definition.outputs.size());
    for (std::uint32_t index = 0; index < definition.outputs.size(); ++index)
    {
        const auto &output = definition.outputs[index];
        out.push_back({execution.id, occurrence_at, index, output.action, execution.owner, execution.targets,
                       output.magnitude_micro, output.payload, execution.context});
    }
    return out;
}

foundation::Result<std::vector<AbilityOutput>> AbilityService::CompleteExecution(AbilityExecutionId id,
                                                                                GameplayTimePoint now)
{
    auto it = executions_.find(id);
    if (it == executions_.end())
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.execution_missing", "execution missing"));
    auto &execution = it->second;
    if (IsTerminal(execution.state))
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.execution_finished", "ability already finished"));
    if (execution.state == AbilityExecutionState::Channeling)
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.channel_requires_tick_or_cancel", "channel executions complete through channel lifecycle"));
    const auto *instance = FindInstance(execution.ability);
    if (!instance)
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.instance_missing", "ability instance missing"));
    const auto *definition = FindDefinition(instance->definition);
    if (!definition)
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.definition_missing", "ability definition missing"));
    if (resource_reconciliation_required_)
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.resource_reconciliation_required",
                  "restored ability reservations must be reconciled before execution"));
    if (now.ticks < execution.due_at.ticks)
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.not_due", "ability execution not due"));
    const auto continuity = CheckContinuity(*definition, *instance, execution, now);
    if (continuity.availability != AbilityAvailability::Available)
    {
        const auto reason = continuity.reason.IsValid() ? continuity.reason : TypeId::FromString("ability.requirements_lost");
        ReleaseReservations(execution, execution.context);
        ++diagnostics_.interrupts;
        FinalizeExecution(id, AbilityExecutionState::Interrupted, AbilityChangeKind::Interrupted, now, execution.context, reason);
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.continuity_lost", "ability requirements or materialization were lost"));
    }

    auto pay = AcquireExecuteCosts(*definition, execution);
    if (!pay)
    {
        const auto context = execution.context;
        ReleaseReservations(execution, context);
        ++diagnostics_.failed;
        FinalizeExecution(id, AbilityExecutionState::Failed, AbilityChangeKind::Failed, now, context,
                          TypeId::FromString("ability.resource_execute_failed"));
        return foundation::Result<std::vector<AbilityOutput>>::Failure(pay.GetError());
    }
    CommitReservations(execution);
    auto out = BuildOutputs(*definition, execution, execution.due_at);
    const auto owner = execution.owner;
    const auto ability = execution.ability;
    const auto context = execution.context;
    diagnostics_.outputs += out.size();
    if (!definition->cooldown.starts_on_begin)
        StartCooldown(*definition, owner, now, context, ability, id);
    FinalizeExecution(id, AbilityExecutionState::Completed, AbilityChangeKind::Executed, now, context);
    return foundation::Result<std::vector<AbilityOutput>>::Success(std::move(out));
}

foundation::Result<std::vector<AbilityOutput>> AbilityService::ChannelTick(AbilityExecutionId id, GameplayTimePoint now,
                                                                           std::uint64_t occurrences)
{
    auto it = executions_.find(id);
    if (it == executions_.end() || it->second.state != AbilityExecutionState::Channeling)
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.channel_invalid", "channel execution missing or inactive"));
    if (occurrences == 0 || occurrences > kMaxChannelOccurrencesPerCall)
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.channel_occurrences_invalid", "channel occurrence count is outside supported bounds"));
    auto &execution = it->second;
    const auto *instance = FindInstance(execution.ability);
    const auto *definition = instance ? FindDefinition(instance->definition) : nullptr;
    if (!instance || !definition || definition->timing.kind != AbilityTimingKind::Channel ||
        definition->timing.channel_interval.ticks <= 0)
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.channel_invalid", "channel definition is unavailable or invalid"));
    if (resource_reconciliation_required_)
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.resource_reconciliation_required",
                  "restored ability reservations must be reconciled before channel execution"));
    if (definition->continuity == AbilityContinuityPolicy::ThroughoutExecution)
    {
        const auto continuity = CheckContinuity(*definition, *instance, execution, now);
        if (continuity.availability != AbilityAvailability::Available)
        {
            const auto reason = continuity.reason.IsValid() ? continuity.reason : TypeId::FromString("ability.requirements_lost");
            const auto context = execution.context;
            ReleaseReservations(execution, context);
            ++diagnostics_.interrupts;
            FinalizeExecution(id, AbilityExecutionState::Interrupted, AbilityChangeKind::Interrupted, now, context, reason);
            return foundation::Result<std::vector<AbilityOutput>>::Failure(
                Error("gameplay.ability.continuity_lost", "channel requirements or materialization were lost"));
        }
    }

    const bool bounded = definition->timing.max_channel_duration.ticks > 0;
    if (bounded && execution.next_channel_at.ticks > execution.due_at.ticks)
    {
        if (now.ticks < execution.due_at.ticks)
            return foundation::Result<std::vector<AbilityOutput>>::Failure(
                Error("gameplay.ability.not_due", "channel completion not due"));
        CommitReservations(execution);
        const auto owner = execution.owner;
        const auto ability = execution.ability;
        const auto context = execution.context;
        if (!definition->cooldown.starts_on_begin)
            StartCooldown(*definition, owner, now, context, ability, id);
        FinalizeExecution(id, AbilityExecutionState::Completed, AbilityChangeKind::Executed, now, context);
        return foundation::Result<std::vector<AbilityOutput>>::Success({});
    }
    if (now.ticks < execution.next_channel_at.ticks)
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.not_due", "channel tick not due"));

    std::uint64_t count = occurrences;
    if (bounded)
    {
        const auto remaining_ticks = SaturatingDifference(execution.due_at, execution.next_channel_at).ticks;
        if (remaining_ticks < 0)
            count = 0;
        else
        {
            const auto remaining = static_cast<std::uint64_t>(remaining_ticks / definition->timing.channel_interval.ticks) + 1;
            count = std::min(count, remaining);
        }
    }
    std::vector<AbilityOutput> all;
    if (count > 0 && definition->outputs.size() > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(count))
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.output_overflow", "channel output batch is too large"));
    all.reserve(definition->outputs.size() * static_cast<std::size_t>(count));

    for (std::uint64_t n = 0; n < count; ++n)
    {
        std::vector<AbilityResourceReservation> staged;
        if (!definition->costs.empty() && !resources_)
        {
            const auto context = execution.context;
            ++diagnostics_.failed;
            FinalizeExecution(id, AbilityExecutionState::Failed, AbilityChangeKind::Failed, now, context,
                              TypeId::FromString("ability.resource_provider_missing"));
            return foundation::Result<std::vector<AbilityOutput>>::Failure(
                Error("gameplay.ability.resource_provider_missing", "channel costs require resource provider"));
        }
        for (const auto &cost : definition->costs)
        {
            if (cost.policy != AbilityCostPolicy::PayPerChannelTick)
                continue;
            auto reservation = resources_->Reserve(execution.owner, cost.resource, cost.amount_micro, execution.context);
            if (!reservation)
            {
                for (const auto &old : staged)
                    resources_->Release(old, execution.context);
                const auto context = execution.context;
                ReleaseReservations(execution, context);
                ++diagnostics_.failed;
                FinalizeExecution(id, AbilityExecutionState::Failed, AbilityChangeKind::Failed, now, context,
                                  TypeId::FromString("ability.channel_resource_failed"));
                return foundation::Result<std::vector<AbilityOutput>>::Failure(reservation.GetError());
            }
            staged.push_back(std::move(reservation).Value());
        }
        for (const auto &token : staged)
            resources_->Commit(token, execution.context);
        const auto occurrence_at = Add(execution.next_channel_at,
                                       ScaleDuration(definition->timing.channel_interval, n));
        auto one = BuildOutputs(*definition, execution, occurrence_at);
        all.insert(all.end(), one.begin(), one.end());
    }
    execution.next_channel_at = Add(execution.next_channel_at, ScaleDuration(definition->timing.channel_interval, count));
    ++execution.revision.value;
    diagnostics_.outputs += all.size();

    if (bounded && execution.next_channel_at.ticks > execution.due_at.ticks)
    {
        CommitReservations(execution);
        const auto owner = execution.owner;
        const auto ability = execution.ability;
        const auto context = execution.context;
        if (!definition->cooldown.starts_on_begin)
            StartCooldown(*definition, owner, now, context, ability, id);
        FinalizeExecution(id, AbilityExecutionState::Completed, AbilityChangeKind::Executed, now, context);
    }
    return foundation::Result<std::vector<AbilityOutput>>::Success(std::move(all));
}

foundation::Result<void> AbilityService::Cancel(AbilityExecutionId id, GameplayTimePoint now, GameplayContext context)
{
    if (resource_reconciliation_required_)
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.resource_reconciliation_required",
                  "restored ability reservations must be reconciled before cancellation"));
    auto it = executions_.find(id);
    if (it == executions_.end())
        return foundation::Result<void>::Failure(Error("gameplay.ability.execution_missing", "execution missing"));
    ReleaseReservations(it->second, context);
    FinalizeExecution(id, AbilityExecutionState::Cancelled, AbilityChangeKind::Cancelled, now, context);
    return foundation::Result<void>::Success();
}

foundation::Result<void> AbilityService::Interrupt(AbilityExecutionId id, TypeId reason, GameplayTimePoint now,
                                                   GameplayContext context)
{
    if (resource_reconciliation_required_)
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.resource_reconciliation_required",
                  "restored ability reservations must be reconciled before interruption"));
    auto it = executions_.find(id);
    if (it == executions_.end())
        return foundation::Result<void>::Failure(Error("gameplay.ability.execution_missing", "execution missing"));
    ReleaseReservations(it->second, context);
    ++diagnostics_.interrupts;
    FinalizeExecution(id, AbilityExecutionState::Interrupted, AbilityChangeKind::Interrupted, now, context, reason);
    return foundation::Result<void>::Success();
}

foundation::Result<void> AbilityService::BindSchedule(AbilityExecutionId id, ScheduleId schedule)
{
    auto it = executions_.find(id);
    if (it == executions_.end() || !schedule.IsValid() || IsTerminal(it->second.state))
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.schedule_invalid", "execution or schedule invalid"));
    auto existing = schedule_to_execution_.find(schedule);
    if (existing != schedule_to_execution_.end() && existing->second != id)
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.schedule_duplicate", "schedule is already bound to another execution"));
    if (it->second.schedule && *it->second.schedule != schedule)
        schedule_to_execution_.erase(*it->second.schedule);
    it->second.schedule = schedule;
    schedule_to_execution_[schedule] = id;
    ++it->second.revision.value;
    return foundation::Result<void>::Success();
}

foundation::Result<void> AbilityService::NotifyScheduleDue(ScheduleId schedule, GameplayTimePoint now,
                                                           std::vector<AbilityOutput> &outputs)
{
    if (resource_reconciliation_required_)
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.resource_reconciliation_required",
                  "restored ability reservations must be reconciled before schedule processing"));
    auto bound = schedule_to_execution_.find(schedule);
    if (bound == schedule_to_execution_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.schedule_missing", "ability schedule not found"));
    const auto execution_id = bound->second;
    auto execution = executions_.find(execution_id);
    if (execution == executions_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.schedule_missing", "ability execution for schedule not found"));
    auto result = execution->second.state == AbilityExecutionState::Channeling
                      ? ChannelTick(execution->first, now, 1)
                      : CompleteExecution(execution->first, now);
    if (!result)
        return foundation::Result<void>::Failure(result.GetError());
    schedule_to_execution_.erase(schedule);
    const auto remaining = executions_.find(execution_id);
    if (remaining != executions_.end() && remaining->second.schedule && *remaining->second.schedule == schedule)
    {
        remaining->second.schedule.reset();
        ++remaining->second.revision.value;
    }
    outputs = std::move(result).Value();
    return foundation::Result<void>::Success();
}

void AbilityService::SweepCooldowns(GameplayTimePoint now, GameplayContext context)
{
    auto it = cooldowns_.begin();
    while (it != cooldowns_.end())
    {
        if (it->ends_at.ticks > now.ticks)
        {
            ++it;
            continue;
        }
        const auto owner = it->owner;
        it = cooldowns_.erase(it);
        Record({0, AbilityChangeKind::CooldownFinished, owner, {}, {}, now, context});
    }
    diagnostics_.cooldowns = cooldowns_.size();
}

foundation::Result<void> AbilityService::ReconcileRestoredReservations(GameplayContext context)
{
    if (!resource_reconciliation_required_)
        return foundation::Result<void>::Success();
    if (!resources_)
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.resource_provider_missing", "resource provider is required for reconciliation"));
    std::vector<AbilityExecutionId> ids;
    ids.reserve(executions_.size());
    for (const auto &[id, execution] : executions_)
        if (!execution.reservations.empty())
            ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    for (const auto id : ids)
    {
        const auto &execution = executions_.at(id);
        for (const auto &reservation : execution.reservations)
        {
            auto reconciled = resources_->ReconcileReservation(reservation, execution.owner, context);
            if (!reconciled)
                return foundation::Result<void>::Failure(reconciled.GetError());
        }
    }
    resource_reconciliation_required_ = false;
    return foundation::Result<void>::Success();
}

const AbilityExecution *AbilityService::FindExecution(AbilityExecutionId id) const noexcept
{
    auto it = executions_.find(id);
    return it == executions_.end() ? nullptr : &it->second;
}

std::vector<AbilityChange> AbilityService::ChangesSinceSequence(std::uint64_t sequence) const
{
    return ReadChangesSinceSequence(sequence).changes;
}

AbilityChangeBatch AbilityService::ReadChangesSinceSequence(std::uint64_t sequence) const
{
    AbilityChangeBatch batch;
    batch.oldest_available_sequence = changes_.empty() ? next_change_sequence_ : changes_.front().sequence;
    batch.snapshot_required = batch.oldest_available_sequence > 0 && sequence < batch.oldest_available_sequence - 1;
    if (batch.snapshot_required)
        return batch;
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(batch.changes),
                 [&](const auto &change) { return change.sequence > sequence; });
    return batch;
}

AbilitiesSnapshot AbilityService::CaptureSnapshot() const
{
    AbilitiesSnapshot snapshot;
    snapshot.instance_ids = instance_ids_.GetSnapshot();
    snapshot.execution_ids = execution_ids_.GetSnapshot();
    snapshot.next_change_sequence = next_change_sequence_;
    snapshot.journal.assign(changes_.begin(), changes_.end());

    std::unordered_set<AbilityInstanceId, IdHash> active_instances;
    for (const auto &[id, execution] : executions_)
    {
        (void)id;
        if (!IsTerminal(execution.state))
        {
            snapshot.executions.push_back(execution);
            active_instances.insert(execution.ability);
        }
    }
    for (const auto &[id, instance] : instances_)
    {
        if (instance.persistence != AbilityGrantPersistence::Temporary || active_instances.contains(id))
            snapshot.instances.push_back(instance);
    }
    snapshot.cooldowns = cooldowns_;
    std::sort(snapshot.instances.begin(), snapshot.instances.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.executions.begin(), snapshot.executions.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.cooldowns.begin(), snapshot.cooldowns.end(), [](const auto &a, const auto &b) {
        if (a.owner != b.owner)
            return a.owner < b.owner;
        return a.group < b.group;
    });
    snapshot.change_epoch = journal_epoch_;
    return snapshot;
}

foundation::Result<void> AbilityService::RestoreSnapshot(AbilitiesSnapshot snapshot)
{
    const auto next_journal_epoch = CheckedNextChangeEpoch(snapshot.change_epoch > journal_epoch_ ? snapshot.change_epoch : journal_epoch_);
    if (!next_journal_epoch)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.change_journal.epoch_exhausted", "change journal epoch is exhausted"));
    }
    std::unordered_map<AbilityInstanceId, AbilityInstance, IdHash> instances;
    std::unordered_map<AbilityExecutionId, AbilityExecution, IdHash> executions;
    std::unordered_map<ScheduleId, AbilityExecutionId> schedules;
    std::vector<AbilityCooldownState> cooldowns;
    std::deque<AbilityChange> journal;
    std::uint64_t max_instance_low = 0;
    std::uint64_t max_execution_low = 0;

    instances.reserve(snapshot.instances.size());
    for (auto &instance : snapshot.instances)
    {
        if (!instance.id.IsValid() || !definitions_.contains(instance.definition) || !instance.owner.IsValid() ||
            instance.revision.value == 0 || instances.contains(instance.id))
            return foundation::Result<void>::Failure(
                Error("gameplay.ability.restore_invalid", "invalid or duplicate ability instance"));
        max_instance_low = std::max(max_instance_low, instance.id.value.Low());
        instances.emplace(instance.id, std::move(instance));
    }
    std::unordered_set<AbilityReservationId, IdHash> reservation_ids;
    executions.reserve(snapshot.executions.size());
    for (auto &execution : snapshot.executions)
    {
        auto ability = instances.find(execution.ability);
        if (!execution.id.IsValid() || ability == instances.end() || execution.owner != ability->second.owner ||
            execution.revision.value == 0 || IsTerminal(execution.state) || executions.contains(execution.id))
            return foundation::Result<void>::Failure(
                Error("gameplay.ability.restore_invalid", "invalid ability execution"));
        const auto *definition = FindDefinition(ability->second.definition);
        if (!definition)
            return foundation::Result<void>::Failure(
                Error("gameplay.ability.restore_invalid", "execution definition is missing"));
        if (execution.state == AbilityExecutionState::Channeling &&
            (definition->timing.kind != AbilityTimingKind::Channel || definition->timing.channel_interval.ticks <= 0))
            return foundation::Result<void>::Failure(
                Error("gameplay.ability.restore_invalid", "invalid channel execution"));
        for (const auto &reservation : execution.reservations)
        {
            if (!reservation.id.IsValid() || !reservation.resource.IsValid() ||
                !reservation_ids.insert(reservation.id).second)
                return foundation::Result<void>::Failure(
                    Error("gameplay.ability.restore_invalid", "invalid or duplicate ability resource reservation"));
        }
        if (execution.schedule)
        {
            if (!execution.schedule->IsValid() || schedules.contains(*execution.schedule))
                return foundation::Result<void>::Failure(
                    Error("gameplay.ability.restore_invalid", "duplicate or invalid ability schedule"));
            schedules.emplace(*execution.schedule, execution.id);
        }
        max_execution_low = std::max(max_execution_low, execution.id.value.Low());
        executions.emplace(execution.id, std::move(execution));
    }

    std::unordered_set<CooldownKey, CooldownKeyHash> cooldown_keys;
    cooldowns.reserve(snapshot.cooldowns.size());
    for (const auto &cooldown : snapshot.cooldowns)
    {
        if (!cooldown.owner.IsValid() || !cooldown.group.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.ability.restore_invalid", "invalid cooldown"));
        if (!cooldown_keys.insert(CooldownKey{cooldown.owner, cooldown.group}).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.ability.restore_invalid", "duplicate cooldown"));
        cooldowns.push_back(cooldown);
    }

    if (snapshot.journal.size() > kChangeJournalCapacity)
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.restore_invalid", "ability journal exceeds retention"));
    std::uint64_t previous = 0;
    for (const auto &change : snapshot.journal)
    {
        if (change.sequence == 0 || change.sequence <= previous)
            return foundation::Result<void>::Failure(
                Error("gameplay.ability.restore_invalid", "invalid ability journal sequence"));
        previous = change.sequence;
        journal.push_back(change);
    }
    if (snapshot.next_change_sequence == 0 || (!journal.empty() && snapshot.next_change_sequence <= journal.back().sequence))
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.restore_invalid", "invalid next ability change sequence"));
    const auto expected_instance_scope = GameplayObjectId::FromString("framework.abilities.instances").High();
    const auto expected_execution_scope = GameplayObjectId::FromString("framework.abilities.executions").High();
    if (!ValidGeneratorSnapshot(snapshot.instance_ids, expected_instance_scope, max_instance_low) ||
        !ValidGeneratorSnapshot(snapshot.execution_ids, expected_execution_scope, max_execution_low))
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.restore_invalid", "invalid ability id generator snapshot"));

    instances_.swap(instances);
    executions_.swap(executions);
    schedule_to_execution_.swap(schedules);
    cooldowns_.swap(cooldowns);
    changes_.swap(journal);
    instance_ids_.Restore(snapshot.instance_ids);
    execution_ids_.Restore(snapshot.execution_ids);
    next_change_sequence_ = snapshot.next_change_sequence;
    resource_reconciliation_required_ = std::any_of(
        executions_.begin(), executions_.end(), [](const auto &entry) { return !entry.second.reservations.empty(); });
    diagnostics_.instances = instances_.size();
    diagnostics_.cooldowns = cooldowns_.size();
    journal_epoch_ = *next_journal_epoch;
    return foundation::Result<void>::Success();
}

AbilitiesDiagnostics AbilityService::GetDiagnostics() const noexcept
{
    return diagnostics_;
}
} // namespace epidemic::gameplay::abilities
