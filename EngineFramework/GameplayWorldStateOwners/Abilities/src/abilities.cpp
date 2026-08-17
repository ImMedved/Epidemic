#include "Epidemic/GameFramework/Abilities/abilities.h"
#include "Epidemic/Foundation/error.h"
#include <algorithm>
#include <iterator>

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
    return GameplayTimePoint{p.ticks + d.ticks};
}
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
    const auto e = AbilityDefinitionId::FromString(d.canonical_name);
    if (!d.id.IsValid())
        d.id = e;
    if (d.id != e || definitions_.contains(d.id) || d.cooldown.duration.ticks < 0 || d.timing.cast_duration.ticks < 0 ||
        d.timing.channel_interval.ticks < 0)
        return foundation::Result<AbilityDefinitionId>::Failure(
            Error("gameplay.ability.definition_invalid", "invalid or duplicate ability definition"));
    for (const auto &c : d.costs)
        if (!c.resource.IsValid() || c.amount_micro < 0)
            return foundation::Result<AbilityDefinitionId>::Failure(
                Error("gameplay.ability.cost_invalid", "invalid ability cost"));
    for (const auto &o : d.outputs)
        if (!o.action.IsValid())
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
    changes_.push_back(std::move(c));
}
foundation::Result<AbilityInstanceId> AbilityService::Grant(GameplayObjectRef owner, AbilityDefinitionId def,
                                                            GameplayObjectRef source,
                                                            AbilityGrantPersistence persistence,
                                                            GameplayContext context)
{
    if (!owner.IsValid() || !definitions_.contains(def))
        return foundation::Result<AbilityInstanceId>::Failure(
            Error("gameplay.ability.grant_invalid", "invalid owner or definition"));
    AbilityInstance i{{instance_ids_.Next()}, def, owner, source, persistence, true, {1}};
    const auto id = i.id;
    instances_.emplace(id, i);
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
    for (const auto &[eid, e] : executions_)
        if (e.ability == id &&
            (e.state == AbilityExecutionState::Casting || e.state == AbilityExecutionState::Channeling))
            return foundation::Result<void>::Failure(Error("gameplay.ability.active", "cannot revoke active ability"));
    const auto owner = it->second.owner;
    instances_.erase(it);
    --diagnostics_.instances;
    Record({0, AbilityChangeKind::Revoked, owner, id, {}, context.time, context});
    return foundation::Result<void>::Success();
}
std::uint64_t AbilityService::RevokeBySource(GameplayObjectRef owner, GameplayObjectRef source, GameplayContext context)
{
    std::vector<AbilityInstanceId> ids;
    for (const auto &[id, i] : instances_)
        if (i.owner == owner && i.source == source && i.persistence == AbilityGrantPersistence::SourceBound)
            ids.push_back(id);
    std::uint64_t n = 0;
    for (auto id : ids)
        if (Revoke(id, context))
            ++n;
    return n;
}
const AbilityInstance *AbilityService::FindInstance(AbilityInstanceId id) const noexcept
{
    auto it = instances_.find(id);
    return it == instances_.end() ? nullptr : &it->second;
}
std::vector<AbilityInstance> AbilityService::GetAbilities(GameplayObjectRef owner) const
{
    std::vector<AbilityInstance> o;
    for (const auto &[id, i] : instances_)
    {
        (void)id;
        if (i.owner == owner)
            o.push_back(i);
    }
    std::sort(o.begin(), o.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return o;
}
GameplayDuration AbilityService::CooldownRemaining(GameplayObjectRef owner, CooldownGroupId group,
                                                   GameplayTimePoint now) const noexcept
{
    GameplayTimePoint end{};
    for (const auto &c : cooldowns_)
        if (c.owner == owner && c.group == group && c.ends_at.ticks > end.ticks)
            end = c.ends_at;
    return GameplayDuration{std::max<std::int64_t>(0, end.ticks - now.ticks)};
}
AbilityAvailabilityResult AbilityService::CanActivate(AbilityInstanceId id, const AbilityTargetSet &targets,
                                                      GameplayTimePoint now) const
{
    const auto *i = FindInstance(id);
    if (!i || !i->enabled)
        return {AbilityAvailability::Unavailable, TypeId::FromString("ability.instance_unavailable")};
    const auto *d = FindDefinition(i->definition);
    if (!d)
        return {AbilityAvailability::Unavailable, TypeId::FromString("ability.definition_missing")};
    if (d->cooldown.group.IsValid() && CooldownRemaining(i->owner, d->cooldown.group, now).ticks > 0)
        return {AbilityAvailability::Unavailable, TypeId::FromString("ability.cooldown")};
    if (resources_)
        for (const auto &c : d->costs)
            if (!resources_->CanAfford(i->owner, c.resource, c.amount_micro))
                return {AbilityAvailability::Unavailable, TypeId::FromString("ability.resource_missing")};
    if (requirements_)
    {
        auto r = requirements_->Check(*d, *i, targets, now);
        if (r.availability != AbilityAvailability::Available)
            return r;
    }
    return {AbilityAvailability::Available, {}};
}
foundation::Result<void> AbilityService::AcquireStartCosts(const AbilityDefinition &d, AbilityExecution &e)
{
    if (d.costs.empty())
        return foundation::Result<void>::Success();
    if (!resources_)
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.resource_provider_missing", "ability costs require resource provider"));
    struct StagedCost
    {
        AbilityResourceReservation reservation;
        AbilityCostPolicy policy;
    };
    std::vector<StagedCost> staged;
    for (const auto &c : d.costs)
    {
        if (c.policy != AbilityCostPolicy::PayOnStart && c.policy != AbilityCostPolicy::ReserveThenCommit)
            continue;
        auto r = resources_->Reserve(e.owner, c.resource, c.amount_micro, e.context);
        if (!r)
        {
            for (const auto &old : staged)
            {
                auto rel = resources_->Release(old.reservation, e.context);
                (void)rel;
            }
            e.reservations.clear();
            return foundation::Result<void>::Failure(r.GetError());
        }
        staged.push_back({std::move(r).Value(), c.policy});
    }
    for (auto &item : staged)
    {
        if (item.policy == AbilityCostPolicy::PayOnStart)
        {
            auto commit = resources_->Commit(item.reservation, e.context);
            if (!commit)
                return commit;
        }
        else
        {
            e.reservations.push_back(std::move(item.reservation));
        }
    }
    return foundation::Result<void>::Success();
}
foundation::Result<void> AbilityService::AcquireExecuteCosts(const AbilityDefinition &d, AbilityExecution &e)
{
    if (d.costs.empty())
        return foundation::Result<void>::Success();
    if (!resources_)
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.resource_provider_missing", "ability costs require resource provider"));
    std::vector<AbilityResourceReservation> staged;
    for (const auto &c : d.costs)
    {
        if (c.policy != AbilityCostPolicy::PayOnExecute)
            continue;
        auto r = resources_->Reserve(e.owner, c.resource, c.amount_micro, e.context);
        if (!r)
        {
            for (const auto &old : staged)
            {
                auto rel = resources_->Release(old, e.context);
                (void)rel;
            }
            return foundation::Result<void>::Failure(r.GetError());
        }
        staged.push_back(std::move(r).Value());
    }
    for (const auto &token : staged)
    {
        auto commit = resources_->Commit(token, e.context);
        if (!commit)
            return commit;
    }
    return foundation::Result<void>::Success();
}
foundation::Result<void> AbilityService::CommitReservations(AbilityExecution &e)
{
    if (!resources_)
        return e.reservations.empty()
                   ? foundation::Result<void>::Success()
                   : foundation::Result<void>::Failure(
                         Error("gameplay.ability.resource_provider_missing", "reservation provider missing"));
    const auto *d = FindDefinition(FindInstance(e.ability)->definition);
    for (std::size_t idx = 0; idx < e.reservations.size(); ++idx)
    {
        const auto type = e.reservations[idx].resource;
        auto cost = std::find_if(d->costs.begin(), d->costs.end(), [&](const auto &c) {
            return c.resource == type && c.policy == AbilityCostPolicy::ReserveThenCommit;
        });
        if (cost != d->costs.end())
        {
            auto r = resources_->Commit(e.reservations[idx], e.context);
            if (!r)
                return r;
        }
    }
    return foundation::Result<void>::Success();
}
void AbilityService::StartCooldown(const AbilityDefinition &d, GameplayObjectRef owner, GameplayTimePoint now,
                                   GameplayContext context, AbilityInstanceId ability, AbilityExecutionId execution)
{
    if (!d.cooldown.group.IsValid() || d.cooldown.duration.ticks <= 0)
        return;
    const auto end = Add(now, d.cooldown.duration);
    auto it = std::find_if(cooldowns_.begin(), cooldowns_.end(),
                           [&](const auto &c) { return c.owner == owner && c.group == d.cooldown.group; });
    if (it == cooldowns_.end())
        cooldowns_.push_back({owner, d.cooldown.group, end});
    else if (end.ticks > it->ends_at.ticks)
        it->ends_at = end;
    diagnostics_.cooldowns = cooldowns_.size();
    Record({0, AbilityChangeKind::CooldownStarted, owner, ability, execution, now, context});
}
foundation::Result<AbilityExecutionId> AbilityService::BeginActivation(AbilityActivationRequest request)
{
    ++diagnostics_.activation_attempts;
    const auto *i = FindInstance(request.ability);
    if (!i)
        return foundation::Result<AbilityExecutionId>::Failure(
            Error("gameplay.ability.instance_missing", "ability instance missing"));
    const auto *d = FindDefinition(i->definition);
    auto avail = CanActivate(request.ability, request.targets, request.now);
    if (avail.availability != AbilityAvailability::Available)
    {
        ++diagnostics_.failed;
        return foundation::Result<AbilityExecutionId>::Failure(
            Error("gameplay.ability.unavailable", "ability is unavailable"));
    }
    AbilityExecution e;
    e.id = {execution_ids_.Next()};
    e.ability = i->id;
    e.owner = i->owner;
    e.targets = std::move(request.targets);
    e.started_at = request.now;
    e.context = request.context;
    e.context.actor = i->owner;
    e.revision = {1};
    switch (d->timing.kind)
    {
    case AbilityTimingKind::Instant:
        e.state = AbilityExecutionState::Executing;
        e.due_at = request.now;
        break;
    case AbilityTimingKind::CastTime:
    case AbilityTimingKind::DelayedExecution:
        e.state = AbilityExecutionState::Casting;
        e.due_at = Add(request.now, d->timing.cast_duration);
        break;
    case AbilityTimingKind::Channel:
        e.state = AbilityExecutionState::Channeling;
        e.next_channel_at = Add(request.now, d->timing.channel_interval);
        e.due_at = d->timing.max_channel_duration.ticks > 0 ? Add(request.now, d->timing.max_channel_duration)
                                                            : GameplayTimePoint{};
        break;
    }
    auto costs = AcquireStartCosts(*d, e);
    if (!costs)
    {
        ++diagnostics_.failed;
        return foundation::Result<AbilityExecutionId>::Failure(costs.GetError());
    }
    const auto id = e.id;
    executions_.emplace(id, std::move(e));
    ++diagnostics_.activations;
    Record({0, AbilityChangeKind::Started, i->owner, i->id, id, request.now, request.context});
    if (d->cooldown.starts_on_begin)
        StartCooldown(*d, i->owner, request.now, request.context, i->id, id);
    return foundation::Result<AbilityExecutionId>::Success(id);
}
std::vector<AbilityOutput> AbilityService::BuildOutputs(const AbilityDefinition &d, const AbilityExecution &e) const
{
    std::vector<AbilityOutput> o;
    o.reserve(d.outputs.size());
    for (const auto &def : d.outputs)
        o.push_back({e.id, def.action, e.owner, e.targets, def.magnitude_micro, def.payload, e.context});
    return o;
}
foundation::Result<std::vector<AbilityOutput>> AbilityService::CompleteExecution(AbilityExecutionId id,
                                                                                 GameplayTimePoint now)
{
    auto it = executions_.find(id);
    if (it == executions_.end())
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.execution_missing", "execution missing"));
    auto &e = it->second;
    const auto *i = FindInstance(e.ability);
    if (!i)
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.instance_missing", "ability instance missing"));
    const auto *d = FindDefinition(i->definition);
    if ((e.state == AbilityExecutionState::Casting || e.state == AbilityExecutionState::Executing) &&
        now.ticks < e.due_at.ticks)
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.not_due", "ability execution not due"));
    if (e.state == AbilityExecutionState::Completed || e.state == AbilityExecutionState::Interrupted)
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.execution_finished", "ability already finished"));
    auto pay = AcquireExecuteCosts(*d, e);
    if (!pay)
    {
        e.state = AbilityExecutionState::Failed;
        ++diagnostics_.failed;
        Record({0, AbilityChangeKind::Failed, e.owner, e.ability, e.id, now, e.context});
        return foundation::Result<std::vector<AbilityOutput>>::Failure(pay.GetError());
    }
    auto commit = CommitReservations(e);
    if (!commit)
    {
        e.state = AbilityExecutionState::Failed;
        ++diagnostics_.failed;
        return foundation::Result<std::vector<AbilityOutput>>::Failure(commit.GetError());
    }
    auto out = BuildOutputs(*d, e);
    e.state = AbilityExecutionState::Completed;
    ++e.revision.value;
    diagnostics_.outputs += out.size();
    if (!d->cooldown.starts_on_begin)
        StartCooldown(*d, e.owner, now, e.context, e.ability, e.id);
    Record({0, AbilityChangeKind::Executed, e.owner, e.ability, e.id, now, e.context});
    return foundation::Result<std::vector<AbilityOutput>>::Success(std::move(out));
}
foundation::Result<std::vector<AbilityOutput>> AbilityService::ChannelTick(AbilityExecutionId id, GameplayTimePoint now,
                                                                           std::uint64_t occurrences)
{
    auto it = executions_.find(id);
    if (it == executions_.end() || it->second.state != AbilityExecutionState::Channeling)
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.channel_invalid", "channel execution missing or inactive"));
    auto &e = it->second;
    const auto *d = FindDefinition(FindInstance(e.ability)->definition);
    if (now.ticks < e.next_channel_at.ticks)
        return foundation::Result<std::vector<AbilityOutput>>::Failure(
            Error("gameplay.ability.not_due", "channel tick not due"));
    std::vector<AbilityOutput> all;
    for (std::uint64_t n = 0; n < occurrences; ++n)
    {
        std::vector<AbilityResourceReservation> staged;
        if (resources_)
            for (const auto &c : d->costs)
                if (c.policy == AbilityCostPolicy::PayPerChannelTick)
                {
                    auto r = resources_->Reserve(e.owner, c.resource, c.amount_micro, e.context);
                    if (!r)
                    {
                        for (const auto &old : staged)
                        {
                            auto rel = resources_->Release(old, e.context);
                            (void)rel;
                        }
                        return foundation::Result<std::vector<AbilityOutput>>::Failure(r.GetError());
                    }
                    staged.push_back(std::move(r).Value());
                }
        for (const auto &token : staged)
        {
            auto commit = resources_->Commit(token, e.context);
            if (!commit)
                return foundation::Result<std::vector<AbilityOutput>>::Failure(commit.GetError());
        }
        auto one = BuildOutputs(*d, e);
        all.insert(all.end(), one.begin(), one.end());
    }
    e.next_channel_at = Add(
        e.next_channel_at, GameplayDuration{d->timing.channel_interval.ticks * static_cast<std::int64_t>(occurrences)});
    diagnostics_.outputs += all.size();
    return foundation::Result<std::vector<AbilityOutput>>::Success(std::move(all));
}
foundation::Result<void> AbilityService::Interrupt(AbilityExecutionId id, TypeId reason, GameplayTimePoint now,
                                                   GameplayContext context)
{
    (void)reason;
    auto it = executions_.find(id);
    if (it == executions_.end())
        return foundation::Result<void>::Failure(Error("gameplay.ability.execution_missing", "execution missing"));
    auto &e = it->second;
    if (e.state == AbilityExecutionState::Completed || e.state == AbilityExecutionState::Interrupted)
        return foundation::Result<void>::Success();
    if (resources_)
        for (const auto &r : e.reservations)
        {
            auto release = resources_->Release(r, context);
            (void)release;
        }
    e.reservations.clear();
    e.state = AbilityExecutionState::Interrupted;
    ++e.revision.value;
    ++diagnostics_.interrupts;
    Record({0, AbilityChangeKind::Interrupted, e.owner, e.ability, e.id, now, context});
    return foundation::Result<void>::Success();
}
foundation::Result<void> AbilityService::BindSchedule(AbilityExecutionId id, ScheduleId schedule)
{
    auto it = executions_.find(id);
    if (it == executions_.end() || !schedule.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.schedule_invalid", "execution or schedule invalid"));
    it->second.schedule = schedule;
    ++it->second.revision.value;
    return foundation::Result<void>::Success();
}
foundation::Result<void> AbilityService::NotifyScheduleDue(ScheduleId schedule, GameplayTimePoint now,
                                                           std::vector<AbilityOutput> &outputs)
{
    auto it = std::find_if(executions_.begin(), executions_.end(),
                           [&](const auto &p) { return p.second.schedule && *p.second.schedule == schedule; });
    if (it == executions_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.ability.schedule_missing", "ability schedule not found"));
    auto result = it->second.state == AbilityExecutionState::Channeling ? ChannelTick(it->first, now, 1)
                                                                        : CompleteExecution(it->first, now);
    if (!result)
        return foundation::Result<void>::Failure(result.GetError());
    outputs = std::move(result).Value();
    return foundation::Result<void>::Success();
}
const AbilityExecution *AbilityService::FindExecution(AbilityExecutionId id) const noexcept
{
    auto it = executions_.find(id);
    return it == executions_.end() ? nullptr : &it->second;
}
std::vector<AbilityChange> AbilityService::ChangesSince(std::uint64_t s) const
{
    std::vector<AbilityChange> o;
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(o),
                 [&](const auto &c) { return c.sequence > s; });
    return o;
}
AbilitiesSnapshot AbilityService::CaptureSnapshot() const
{
    AbilitiesSnapshot s;
    s.instance_ids = instance_ids_.GetSnapshot();
    s.execution_ids = execution_ids_.GetSnapshot();
    for (const auto &[id, i] : instances_)
    {
        (void)id;
        if (i.persistence != AbilityGrantPersistence::Temporary)
            s.instances.push_back(i);
    }
    for (const auto &[id, e] : executions_)
    {
        (void)id;
        if (e.state != AbilityExecutionState::Completed && e.state != AbilityExecutionState::Failed)
            s.executions.push_back(e);
    }
    s.cooldowns = cooldowns_;
    std::sort(s.instances.begin(), s.instances.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.executions.begin(), s.executions.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return s;
}
foundation::Result<void> AbilityService::RestoreSnapshot(AbilitiesSnapshot s)
{
    instances_.clear();
    executions_.clear();
    cooldowns_ = std::move(s.cooldowns);
    for (auto &i : s.instances)
    {
        if (!definitions_.contains(i.definition) || !i.owner.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.ability.restore_invalid", "invalid ability instance"));
        instances_.emplace(i.id, std::move(i));
    }
    for (auto &e : s.executions)
    {
        if (!instances_.contains(e.ability))
            return foundation::Result<void>::Failure(
                Error("gameplay.ability.restore_invalid", "execution references missing ability"));
        executions_.emplace(e.id, std::move(e));
    }
    instance_ids_.Restore(s.instance_ids);
    execution_ids_.Restore(s.execution_ids);
    changes_.clear();
    next_change_sequence_ = 1;
    diagnostics_.instances = instances_.size();
    diagnostics_.cooldowns = cooldowns_.size();
    return foundation::Result<void>::Success();
}
AbilitiesDiagnostics AbilityService::GetDiagnostics() const noexcept
{
    return diagnostics_;
}
} // namespace epidemic::gameplay::abilities
