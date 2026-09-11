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
    changes_.reserve(kChangeJournalCapacity);
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

bool AbilityService::CanRecord(std::size_t count) const noexcept
{
    if (count == 0)
        return true;
    if (next_change_sequence_ == 0)
        return false;
    return count - 1 <= std::numeric_limits<std::uint64_t>::max() - next_change_sequence_;
}

void AbilityService::Record(AbilityChange c) noexcept
{
    c.sequence = next_change_sequence_;
    if (changes_.size() == kChangeJournalCapacity)
        changes_.erase(changes_.begin());
    changes_.push_back(std::move(c));
    if (next_change_sequence_ == std::numeric_limits<std::uint64_t>::max())
        next_change_sequence_ = 0;
    else
        ++next_change_sequence_;
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
    if (!CanRecord())
        return foundation::Result<AbilityInstanceId>::Failure(
            Error("gameplay.ability.journal_exhausted", "ability change sequence exhausted"));
    auto staged_ids = instance_ids_;
    const auto generated = staged_ids.Next();
    if (!generated.IsValid())
        return foundation::Result<AbilityInstanceId>::Failure(
            Error("gameplay.ability.id_exhausted", "ability instance id exhausted"));
    AbilityInstance instance{{generated}, def, owner, source, persistence, true, {1}};
    const auto id = instance.id;
    try
    {
        instances_.emplace(id, instance);
    }
    catch (const std::exception &)
    {
        return foundation::Result<AbilityInstanceId>::Failure(
            Error("gameplay.ability.allocation_failed", "failed to publish ability instance"));
    }
    catch (...)
    {
        return foundation::Result<AbilityInstanceId>::Failure(
            Error("gameplay.ability.allocation_failed", "failed to publish ability instance"));
    }
    (void)instance_ids_.Restore(staged_ids.GetSnapshot());
    if (diagnostics_.instances != std::numeric_limits<std::uint64_t>::max())
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
    if (!CanRecord())
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.journal_exhausted", "ability change sequence exhausted"));
    const auto owner = it->second.owner;
    instances_.erase(it);
    if (diagnostics_.instances > 0)
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
    const auto next_revision = CheckedNext(it->second.revision);
    if (!next_revision)
        return foundation::Result<void>::Failure(
            Error("gameplay.revision_exhausted", "ability instance revision exhausted"));
    if (!CanRecord())
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.journal_exhausted", "ability change sequence exhausted"));
    it->second.enabled = enabled;
    it->second.revision = *next_revision;
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
        try
        {
            std::unordered_map<AbilityResourceTypeId, std::int64_t, IdHash> totals;
            for (const auto &cost : definition->costs)
            {
                if (cost.policy != AbilityCostPolicy::PayOnStart && cost.policy != AbilityCostPolicy::ReserveThenCommit)
                    continue;
                auto [it, inserted] = totals.try_emplace(cost.resource, 0);
                (void)inserted;
                if (cost.amount_micro > std::numeric_limits<std::int64_t>::max() - it->second)
                    return {AbilityAvailability::Unavailable, TypeId::FromString("ability.resource_cost_overflow")};
                it->second += cost.amount_micro;
            }
            for (const auto &[resource, amount] : totals)
                if (!resources_->CanAfford(instance->owner, resource, amount))
                    return {AbilityAvailability::Unavailable, TypeId::FromString("ability.resource_missing")};
        }
        catch (...)
        {
            return {AbilityAvailability::Unavailable, TypeId::FromString("ability.internal_allocation_failed")};
        }
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

foundation::Result<void> AbilityService::FinalizeExecution(AbilityExecutionId id, AbilityExecutionState terminal_state,
                                                               AbilityChangeKind change_kind, GameplayTimePoint now,
                                                               GameplayContext context, TypeId reason)
{
    (void)terminal_state;
    auto it = executions_.find(id);
    if (it == executions_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.execution_missing", "execution missing"));
    const auto next_revision = CheckedNext(it->second.revision);
    if (!next_revision)
        return foundation::Result<void>::Failure(
            Error("gameplay.revision_exhausted", "ability execution revision exhausted"));
    if (!CanRecord())
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.journal_exhausted", "ability change sequence exhausted"));
    const auto owner = it->second.owner;
    const auto ability = it->second.ability;
    if (it->second.schedule)
        schedule_to_execution_.erase(*it->second.schedule);
    Record({0, change_kind, owner, ability, id, now, context, reason});
    executions_.erase(it);
    return foundation::Result<void>::Success();
}

foundation::Result<void> AbilityService::AcquireStartCosts(
    const AbilityDefinition &definition, AbilityExecution &execution,
    std::vector<AbilityResourceReservation> &pay_on_start)
{
    if (definition.costs.empty())
        return foundation::Result<void>::Success();
    if (!resources_)
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.resource_provider_missing", "ability costs require resource provider"));

    const auto start_count = static_cast<std::size_t>(std::count_if(
        definition.costs.begin(), definition.costs.end(), [](const auto &cost) {
            return cost.policy == AbilityCostPolicy::PayOnStart;
        }));
    const auto held_count = static_cast<std::size_t>(std::count_if(
        definition.costs.begin(), definition.costs.end(), [](const auto &cost) {
            return cost.policy == AbilityCostPolicy::ReserveThenCommit;
        }));
    try
    {
        pay_on_start.reserve(start_count);
        execution.reservations.reserve(held_count);
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.allocation_failed", "failed to prepare ability reservation storage"));
    }

    auto rollback = [&]() noexcept {
        for (const auto &reservation : pay_on_start)
            resources_->Release(reservation, execution.context);
        for (const auto &reservation : execution.reservations)
            resources_->Release(reservation, execution.context);
        pay_on_start.clear();
        execution.reservations.clear();
    };
    try
    {
        for (const auto &cost : definition.costs)
        {
            if (cost.policy != AbilityCostPolicy::PayOnStart && cost.policy != AbilityCostPolicy::ReserveThenCommit)
                continue;
            auto reservation = resources_->Reserve(execution.owner, cost.resource, cost.amount_micro, execution.context);
            if (!reservation)
            {
                rollback();
                return foundation::Result<void>::Failure(reservation.GetError());
            }
            if (cost.policy == AbilityCostPolicy::PayOnStart)
                pay_on_start.push_back(std::move(reservation).Value());
            else
                execution.reservations.push_back(std::move(reservation).Value());
        }
    }
    catch (const std::exception &)
    {
        rollback();
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.resource_provider_exception", "ability resource provider threw while reserving"));
    }
    catch (...)
    {
        rollback();
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.resource_provider_exception", "ability resource provider threw while reserving"));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> AbilityService::AcquireExecuteCosts(
    const AbilityDefinition &definition, AbilityExecution &execution,
    std::vector<AbilityResourceReservation> &pay_on_execute)
{
    const auto count = static_cast<std::size_t>(std::count_if(
        definition.costs.begin(), definition.costs.end(), [](const auto &cost) {
            return cost.policy == AbilityCostPolicy::PayOnExecute;
        }));
    if (count == 0)
        return foundation::Result<void>::Success();
    if (!resources_)
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.resource_provider_missing", "ability execute costs require resource provider"));
    try
    {
        pay_on_execute.reserve(count);
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.allocation_failed", "failed to prepare execute reservation storage"));
    }
    auto rollback = [&]() noexcept {
        for (const auto &reservation : pay_on_execute)
            resources_->Release(reservation, execution.context);
        pay_on_execute.clear();
    };
    try
    {
        for (const auto &cost : definition.costs)
        {
            if (cost.policy != AbilityCostPolicy::PayOnExecute)
                continue;
            auto reservation = resources_->Reserve(execution.owner, cost.resource, cost.amount_micro, execution.context);
            if (!reservation)
            {
                rollback();
                return foundation::Result<void>::Failure(reservation.GetError());
            }
            pay_on_execute.push_back(std::move(reservation).Value());
        }
    }
    catch (const std::exception &)
    {
        rollback();
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.resource_provider_exception", "ability resource provider threw while reserving"));
    }
    catch (...)
    {
        rollback();
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.resource_provider_exception", "ability resource provider threw while reserving"));
    }
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
        return foundation::Result<AbilityExecutionId>::Failure(
            Error("gameplay.ability.unavailable", "ability is unavailable"));

    const bool starts_cooldown = definition->cooldown.starts_on_begin && definition->cooldown.group.IsValid() &&
                                 definition->cooldown.duration.ticks > 0;
    if (!CanRecord(starts_cooldown ? 2 : 1))
        return foundation::Result<AbilityExecutionId>::Failure(
            Error("gameplay.ability.journal_exhausted", "ability change sequence exhausted"));

    auto staged_ids = execution_ids_;
    const auto generated = staged_ids.Next();
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

    std::vector<AbilityResourceReservation> pay_on_start;
    auto costs = AcquireStartCosts(*definition, execution, pay_on_start);
    if (!costs)
        return foundation::Result<AbilityExecutionId>::Failure(costs.GetError());

    auto rollback_resources = [&]() noexcept {
        if (!resources_)
            return;
        for (const auto &reservation : pay_on_start)
            resources_->Release(reservation, execution.context);
        for (const auto &reservation : execution.reservations)
            resources_->Release(reservation, execution.context);
    };

    bool execution_inserted = false;
    std::optional<std::size_t> existing_cooldown;
    const auto cooldown_end = starts_cooldown ? Add(request.now, definition->cooldown.duration) : GameplayTimePoint{};
    if (starts_cooldown)
    {
        const auto cooldown = std::find_if(cooldowns_.begin(), cooldowns_.end(), [&](const auto &state) {
            return state.owner == instance->owner && state.group == definition->cooldown.group;
        });
        if (cooldown != cooldowns_.end())
            existing_cooldown = static_cast<std::size_t>(std::distance(cooldowns_.begin(), cooldown));
    }
    try
    {
        const auto [execution_it, inserted] = executions_.emplace(execution.id, execution);
        (void)execution_it;
        if (!inserted)
        {
            rollback_resources();
            return foundation::Result<AbilityExecutionId>::Failure(
                Error("gameplay.ability.execution_collision", "ability execution id already exists"));
        }
        execution_inserted = true;
        if (starts_cooldown && !existing_cooldown)
            cooldowns_.push_back({instance->owner, definition->cooldown.group, cooldown_end});
    }
    catch (const std::bad_alloc &)
    {
        if (execution_inserted)
            executions_.erase(execution.id);
        rollback_resources();
        return foundation::Result<AbilityExecutionId>::Failure(
            Error("gameplay.ability.allocation_failed", "failed to prepare ability activation state"));
    }

    if (existing_cooldown && cooldown_end.ticks > cooldowns_[*existing_cooldown].ends_at.ticks)
        cooldowns_[*existing_cooldown].ends_at = cooldown_end;
    for (const auto &reservation : pay_on_start)
        resources_->Commit(reservation, execution.context);
    (void)execution_ids_.Restore(staged_ids.GetSnapshot());
    diagnostics_.activation_attempts = diagnostics_.activation_attempts == std::numeric_limits<std::uint64_t>::max()
                                           ? diagnostics_.activation_attempts
                                           : diagnostics_.activation_attempts + 1;
    diagnostics_.activations = diagnostics_.activations == std::numeric_limits<std::uint64_t>::max()
                                   ? diagnostics_.activations
                                   : diagnostics_.activations + 1;
    diagnostics_.cooldowns = cooldowns_.size();
    const auto id = execution.id;
    Record({0, AbilityChangeKind::Started, instance->owner, instance->id, id, request.now, request.context});
    if (starts_cooldown)
        Record({0, AbilityChangeKind::CooldownStarted, instance->owner, instance->id, id, request.now, request.context});
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
    const auto *definition = instance ? FindDefinition(instance->definition) : nullptr;
    if (!instance || !definition)
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.definition_missing", "ability instance or definition missing"));
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
        if (!CheckedNext(execution.revision) || !CanRecord())
            return foundation::Result<std::vector<AbilityOutput>>::Failure(
                Error("gameplay.revision_exhausted", "ability execution cannot publish terminal state"));
        const auto reason = continuity.reason.IsValid() ? continuity.reason : TypeId::FromString("ability.requirements_lost");
        const auto context = execution.context;
        ReleaseReservations(execution, context);
        if (diagnostics_.interrupts != std::numeric_limits<std::uint64_t>::max())
            ++diagnostics_.interrupts;
        (void)FinalizeExecution(id, AbilityExecutionState::Interrupted, AbilityChangeKind::Interrupted, now, context, reason);
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.continuity_lost", "ability requirements or materialization were lost"));
    }

    std::vector<AbilityResourceReservation> pay_on_execute;
    auto pay = AcquireExecuteCosts(*definition, execution, pay_on_execute);
    if (!pay)
    {
        if (CheckedNext(execution.revision) && CanRecord())
        {
            const auto context = execution.context;
            ReleaseReservations(execution, context);
            if (diagnostics_.failed != std::numeric_limits<std::uint64_t>::max())
                ++diagnostics_.failed;
            (void)FinalizeExecution(id, AbilityExecutionState::Failed, AbilityChangeKind::Failed, now, context,
                                    TypeId::FromString("ability.resource_execute_failed"));
        }
        return foundation::Result<std::vector<AbilityOutput>>::Failure(pay.GetError());
    }
    auto release_execute = [&]() noexcept {
        if (resources_)
            for (const auto &reservation : pay_on_execute)
                resources_->Release(reservation, execution.context);
    };

    const bool starts_cooldown = !definition->cooldown.starts_on_begin && definition->cooldown.group.IsValid() &&
                                 definition->cooldown.duration.ticks > 0;
    if (!CheckedNext(execution.revision) || !CanRecord(starts_cooldown ? 2 : 1))
    {
        release_execute();
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.revision_exhausted", "ability execution cannot publish completion"));
    }

    std::vector<AbilityOutput> out;
    std::vector<AbilityCooldownState> staged_cooldowns;
    try
    {
        out = BuildOutputs(*definition, execution, execution.due_at);
        staged_cooldowns = cooldowns_;
        if (starts_cooldown)
        {
            const auto end = Add(now, definition->cooldown.duration);
            auto cooldown = std::find_if(staged_cooldowns.begin(), staged_cooldowns.end(), [&](const auto &c) {
                return c.owner == execution.owner && c.group == definition->cooldown.group;
            });
            if (cooldown == staged_cooldowns.end())
                staged_cooldowns.push_back({execution.owner, definition->cooldown.group, end});
            else if (end.ticks > cooldown->ends_at.ticks)
                cooldown->ends_at = end;
        }
    }
    catch (...)
    {
        release_execute();
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.allocation_failed", "failed to prepare ability completion outputs"));
    }

    for (const auto &reservation : pay_on_execute)
        resources_->Commit(reservation, execution.context);
    CommitReservations(execution);
    const auto owner = execution.owner;
    const auto ability = execution.ability;
    const auto context = execution.context;
    cooldowns_.swap(staged_cooldowns);
    diagnostics_.cooldowns = cooldowns_.size();
    if (diagnostics_.outputs <= std::numeric_limits<std::uint64_t>::max() - out.size())
        diagnostics_.outputs += out.size();
    else
        diagnostics_.outputs = std::numeric_limits<std::uint64_t>::max();
    if (starts_cooldown)
        Record({0, AbilityChangeKind::CooldownStarted, owner, ability, id, now, context});
    auto finalized = FinalizeExecution(id, AbilityExecutionState::Completed, AbilityChangeKind::Executed, now, context);
    if (!finalized)
        return foundation::Result<std::vector<AbilityOutput>>::Failure(finalized.GetError());
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
            if (!CheckedNext(execution.revision) || !CanRecord())
                return foundation::Result<std::vector<AbilityOutput>>::Failure(
                    Error("gameplay.revision_exhausted", "ability execution cannot publish interruption"));
            const auto reason = continuity.reason.IsValid() ? continuity.reason : TypeId::FromString("ability.requirements_lost");
            const auto context = execution.context;
            ReleaseReservations(execution, context);
            if (diagnostics_.interrupts != std::numeric_limits<std::uint64_t>::max())
                ++diagnostics_.interrupts;
            (void)FinalizeExecution(id, AbilityExecutionState::Interrupted, AbilityChangeKind::Interrupted, now, context, reason);
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
        const bool starts_cooldown = !definition->cooldown.starts_on_begin && definition->cooldown.group.IsValid() &&
                                     definition->cooldown.duration.ticks > 0;
        if (!CheckedNext(execution.revision) || !CanRecord(starts_cooldown ? 2 : 1))
            return foundation::Result<std::vector<AbilityOutput>>::Failure(
                Error("gameplay.revision_exhausted", "ability execution cannot publish completion"));
        std::vector<AbilityCooldownState> staged_cooldowns;
        try
        {
            staged_cooldowns = cooldowns_;
            if (starts_cooldown)
            {
                const auto end = Add(now, definition->cooldown.duration);
                auto cooldown = std::find_if(staged_cooldowns.begin(), staged_cooldowns.end(), [&](const auto &c) {
                    return c.owner == execution.owner && c.group == definition->cooldown.group;
                });
                if (cooldown == staged_cooldowns.end())
                    staged_cooldowns.push_back({execution.owner, definition->cooldown.group, end});
                else if (end.ticks > cooldown->ends_at.ticks)
                    cooldown->ends_at = end;
            }
        }
        catch (...)
        {
            return foundation::Result<std::vector<AbilityOutput>>::Failure(
                Error("gameplay.ability.allocation_failed", "failed to prepare channel completion"));
        }
        CommitReservations(execution);
        const auto owner = execution.owner;
        const auto ability = execution.ability;
        const auto context = execution.context;
        cooldowns_.swap(staged_cooldowns);
        diagnostics_.cooldowns = cooldowns_.size();
        if (starts_cooldown)
            Record({0, AbilityChangeKind::CooldownStarted, owner, ability, id, now, context});
        auto finalized = FinalizeExecution(id, AbilityExecutionState::Completed, AbilityChangeKind::Executed, now, context);
        if (!finalized)
            return foundation::Result<std::vector<AbilityOutput>>::Failure(finalized.GetError());
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
    if (count > 0 && definition->outputs.size() > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(count))
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.output_overflow", "channel output batch is too large"));

    const auto next_channel = Add(execution.next_channel_at, ScaleDuration(definition->timing.channel_interval, count));
    const bool completes = bounded && next_channel.ticks > execution.due_at.ticks;
    const bool starts_cooldown = completes && !definition->cooldown.starts_on_begin &&
                                 definition->cooldown.group.IsValid() && definition->cooldown.duration.ticks > 0;
    if (!completes && !CheckedNext(execution.revision))
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.revision_exhausted", "ability execution revision exhausted"));
    if (completes && (!CheckedNext(execution.revision) || !CanRecord(starts_cooldown ? 2 : 1)))
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.revision_exhausted", "ability execution cannot publish completion"));

    std::vector<AbilityOutput> all;
    std::vector<AbilityResourceReservation> tick_reservations;
    std::vector<AbilityCooldownState> staged_cooldowns;
    const auto tick_cost_count = static_cast<std::size_t>(std::count_if(
        definition->costs.begin(), definition->costs.end(), [](const auto &cost) {
            return cost.policy == AbilityCostPolicy::PayPerChannelTick;
        }));
    try
    {
        all.reserve(definition->outputs.size() * static_cast<std::size_t>(count));
        tick_reservations.reserve(tick_cost_count * static_cast<std::size_t>(count));
        for (std::uint64_t n = 0; n < count; ++n)
        {
            const auto occurrence_at = Add(execution.next_channel_at,
                                           ScaleDuration(definition->timing.channel_interval, n));
            auto one = BuildOutputs(*definition, execution, occurrence_at);
            all.insert(all.end(), one.begin(), one.end());
        }
        if (starts_cooldown)
        {
            staged_cooldowns = cooldowns_;
            const auto end = Add(now, definition->cooldown.duration);
            auto cooldown = std::find_if(staged_cooldowns.begin(), staged_cooldowns.end(), [&](const auto &c) {
                return c.owner == execution.owner && c.group == definition->cooldown.group;
            });
            if (cooldown == staged_cooldowns.end())
                staged_cooldowns.push_back({execution.owner, definition->cooldown.group, end});
            else if (end.ticks > cooldown->ends_at.ticks)
                cooldown->ends_at = end;
        }
    }
    catch (...)
    {
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.allocation_failed", "failed to prepare channel output batch"));
    }

    if (tick_cost_count > 0 && !resources_)
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.resource_provider_missing", "channel costs require resource provider"));
    auto release_ticks = [&]() noexcept {
        if (resources_)
            for (const auto &reservation : tick_reservations)
                resources_->Release(reservation, execution.context);
    };
    try
    {
        for (std::uint64_t n = 0; n < count; ++n)
        {
            (void)n;
            for (const auto &cost : definition->costs)
            {
                if (cost.policy != AbilityCostPolicy::PayPerChannelTick)
                    continue;
                auto reservation = resources_->Reserve(execution.owner, cost.resource, cost.amount_micro, execution.context);
                if (!reservation)
                {
                    release_ticks();
                    return foundation::Result<std::vector<AbilityOutput>>::Failure(reservation.GetError());
                }
                tick_reservations.push_back(std::move(reservation).Value());
            }
        }
    }
    catch (...)
    {
        release_ticks();
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.resource_provider_exception", "ability resource provider threw while reserving channel cost"));
    }

    for (const auto &reservation : tick_reservations)
        resources_->Commit(reservation, execution.context);
    if (completes)
        CommitReservations(execution);
    else
    {
        execution.next_channel_at = next_channel;
        execution.revision = *CheckedNext(execution.revision);
    }
    if (starts_cooldown)
    {
        cooldowns_.swap(staged_cooldowns);
        diagnostics_.cooldowns = cooldowns_.size();
        Record({0, AbilityChangeKind::CooldownStarted, execution.owner, execution.ability, id, now, execution.context});
    }
    if (diagnostics_.outputs <= std::numeric_limits<std::uint64_t>::max() - all.size())
        diagnostics_.outputs += all.size();
    else
        diagnostics_.outputs = std::numeric_limits<std::uint64_t>::max();
    if (completes)
    {
        const auto context = execution.context;
        auto finalized = FinalizeExecution(id, AbilityExecutionState::Completed, AbilityChangeKind::Executed, now, context);
        if (!finalized)
            return foundation::Result<std::vector<AbilityOutput>>::Failure(finalized.GetError());
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
    if (!CheckedNext(it->second.revision) || !CanRecord())
        return foundation::Result<void>::Failure(
            Error("gameplay.revision_exhausted", "ability execution cannot publish cancellation"));
    ReleaseReservations(it->second, context);
    return FinalizeExecution(id, AbilityExecutionState::Cancelled, AbilityChangeKind::Cancelled, now, context);
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
    if (!CheckedNext(it->second.revision) || !CanRecord())
        return foundation::Result<void>::Failure(
            Error("gameplay.revision_exhausted", "ability execution cannot publish interruption"));
    ReleaseReservations(it->second, context);
    if (diagnostics_.interrupts != std::numeric_limits<std::uint64_t>::max())
        ++diagnostics_.interrupts;
    return FinalizeExecution(id, AbilityExecutionState::Interrupted, AbilityChangeKind::Interrupted, now, context, reason);
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
    if (it->second.schedule && *it->second.schedule == schedule)
        return foundation::Result<void>::Success();
    const auto next_revision = CheckedNext(it->second.revision);
    if (!next_revision)
        return foundation::Result<void>::Failure(
            Error("gameplay.revision_exhausted", "ability execution revision exhausted"));
    try
    {
        auto [inserted_it, inserted] = schedule_to_execution_.try_emplace(schedule, id);
        (void)inserted_it;
        if (!inserted && inserted_it->second != id)
            return foundation::Result<void>::Failure(
                Error("gameplay.ability.schedule_duplicate", "schedule is already bound to another execution"));
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.allocation_failed", "failed to publish ability schedule binding"));
    }
    const auto old = it->second.schedule;
    it->second.schedule = schedule;
    it->second.revision = *next_revision;
    if (old && *old != schedule)
        schedule_to_execution_.erase(*old);
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

    const bool may_remain = execution->second.state == AbilityExecutionState::Channeling;
    if (may_remain && !CheckedNext(execution->second.revision))
        return foundation::Result<void>::Failure(
            Error("gameplay.revision_exhausted", "ability execution revision exhausted before schedule delivery"));
    auto result = execution->second.state == AbilityExecutionState::Channeling
                      ? ChannelTick(execution->first, now, 1)
                      : CompleteExecution(execution->first, now);
    if (!result)
        return foundation::Result<void>::Failure(result.GetError());

    schedule_to_execution_.erase(schedule);
    const auto remaining = executions_.find(execution_id);
    if (remaining != executions_.end() && remaining->second.schedule && *remaining->second.schedule == schedule)
    {
        const auto next_revision = CheckedNext(remaining->second.revision);
        if (!next_revision)
            return foundation::Result<void>::Failure(
                Error("gameplay.revision_exhausted", "ability execution revision exhausted after schedule delivery"));
        remaining->second.schedule.reset();
        remaining->second.revision = *next_revision;
    }
    outputs = std::move(result).Value();
    return foundation::Result<void>::Success();
}

void AbilityService::SweepCooldowns(GameplayTimePoint now, GameplayContext context)
{
    const auto due = static_cast<std::size_t>(std::count_if(cooldowns_.begin(), cooldowns_.end(),
                                                            [&](const auto &c) { return c.ends_at.ticks <= now.ticks; }));
    if (due == 0 || !CanRecord(due))
        return;
    std::vector<AbilityCooldownState> remaining;
    std::vector<GameplayObjectRef> finished;
    try
    {
        remaining.reserve(cooldowns_.size() - due);
        finished.reserve(due);
        for (const auto &cooldown : cooldowns_)
        {
            if (cooldown.ends_at.ticks <= now.ticks)
                finished.push_back(cooldown.owner);
            else
                remaining.push_back(cooldown);
        }
    }
    catch (...)
    {
        return;
    }
    cooldowns_.swap(remaining);
    for (const auto owner : finished)
        Record({0, AbilityChangeKind::CooldownFinished, owner, {}, {}, now, context});
    diagnostics_.cooldowns = cooldowns_.size();
}

foundation::Result<void> AbilityService::ReconcileRestoredReservations(GameplayContext context)
{
    if (!resource_reconciliation_required_)
        return foundation::Result<void>::Success();
    if (!resources_)
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.resource_provider_missing", "resource provider is required for reconciliation"));

    std::vector<std::pair<AbilityExecutionId, AbilityResourceReservation>> pending;
    try
    {
        std::size_t total = 0;
        for (const auto &[id, execution] : executions_)
        {
            (void)id;
            total += execution.reservations.size();
        }
        pending.reserve(total);
        reconciled_reservations_.reserve(total);
        for (const auto &[id, execution] : executions_)
            for (const auto &reservation : execution.reservations)
                pending.emplace_back(id, reservation);
        std::sort(pending.begin(), pending.end(), [](const auto &a, const auto &b) {
            if (a.first != b.first)
                return a.first < b.first;
            return a.second.id < b.second.id;
        });
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.allocation_failed", "failed to prepare reservation reconciliation"));
    }

    for (const auto &[execution_id, reservation] : pending)
    {
        if (std::find(reconciled_reservations_.begin(), reconciled_reservations_.end(), reservation.id) !=
            reconciled_reservations_.end())
            continue;
        const auto execution = executions_.find(execution_id);
        if (execution == executions_.end())
            continue;
        try
        {
            auto reconciled = resources_->ReconcileReservation(reservation, execution->second.owner, context);
            if (!reconciled)
                return foundation::Result<void>::Failure(reconciled.GetError());
        }
        catch (...)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.ability.resource_provider_exception", "resource provider threw during reconciliation"));
        }
        reconciled_reservations_.push_back(reservation.id);
    }
    reconciled_reservations_.clear();
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
    std::vector<AbilityChange> journal;
    journal.reserve(std::min(snapshot.journal.size(), kChangeJournalCapacity));
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
    (void)instance_ids_.Restore(snapshot.instance_ids);
    (void)execution_ids_.Restore(snapshot.execution_ids);
    next_change_sequence_ = snapshot.next_change_sequence;
    resource_reconciliation_required_ = std::any_of(
        executions_.begin(), executions_.end(), [](const auto &entry) { return !entry.second.reservations.empty(); });
    reconciled_reservations_.clear();
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
