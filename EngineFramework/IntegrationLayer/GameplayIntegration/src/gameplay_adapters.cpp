#include "Epidemic/GameFramework/GameplayIntegration/gameplay_adapters.h"
#include "Epidemic/Foundation/error.h"
#include "Epidemic/GameFramework/SupportRandom/deterministic_random.h"

#include <algorithm>
#include <memory>
#include <type_traits>

namespace epidemic::gameplay::integration
{
namespace
{
[[nodiscard]] foundation::Error Error(std::string code, std::string message)
{
    return foundation::Error::Create(std::move(code), std::move(message));
}
[[nodiscard]] std::int64_t MulMicro(std::int64_t a, std::int64_t b) noexcept
{
    constexpr std::int64_t s = 1'000'000;
    if (a == 0 || b == 0)
        return 0;
    const auto aq = a / s, ar = a % s, bq = b / s, br = b % s;
    const auto safeMul = [](std::int64_t x, std::int64_t y) {
        if (x == 0 || y == 0)
            return std::int64_t{0};
        if ((x == -1 && y == INT64_MIN) || (y == -1 && x == INT64_MIN))
            return INT64_MAX;
        if (x > 0)
        {
            if (y > 0 && x > INT64_MAX / y)
                return INT64_MAX;
            if (y < 0 && y < INT64_MIN / x)
                return INT64_MIN;
        }
        else
        {
            if (y > 0 && x < INT64_MIN / y)
                return INT64_MIN;
            if (y < 0 && x < INT64_MAX / y)
                return INT64_MAX;
        }
        return x * y;
    };
    const auto safeAdd = [](std::int64_t x, std::int64_t y) {
        if (y > 0 && x > INT64_MAX - y)
            return INT64_MAX;
        if (y < 0 && x < INT64_MIN - y)
            return INT64_MIN;
        return x + y;
    };
    auto r = safeMul(safeMul(aq, bq), s);
    r = safeAdd(r, safeMul(aq, br));
    r = safeAdd(r, safeMul(ar, bq));
    return safeAdd(r, safeMul(ar, br) / s);
}
} // namespace

foundation::Result<effects::EffectPrepareResult> CombatDamageEffectHandler::Prepare(
    const effects::EffectOperation &operation) const
{
    static_assert(std::is_trivially_copyable_v<combat::CombatPlan>);
    const auto payload = operation.payload.AsTrivial<CombatDamageEffectPayload>(PayloadType());
    if (!payload)
        return foundation::Result<effects::EffectPrepareResult>::Failure(
            Error("gameplay.integration.combat_payload_invalid", "combat effect payload invalid"));
    const auto root = operation.context.correlation.IsValid() ? operation.context.correlation.Low()
                                                              : operation.context.operation.Low();
    combat::DamageRequest request;
    request.source = operation.context.source;
    request.instigator = operation.context.instigator;
    request.target = operation.target;
    request.damage_type = payload->damage_type;
    request.profile = payload->profile;
    request.base_amount_micro = operation.magnitude_micro;
    request.seed = {random::StableMix(root ^ payload->seed_salt ^ operation.local_sequence)};
    request.context = operation.context;
    auto plan = combat_.PrepareDamage(request);
    if (!plan)
        return foundation::Result<effects::EffectPrepareResult>::Failure(plan.GetError());
    effects::EffectPrepareResult result;
    result.disposition = effects::EffectPrepareDisposition::Accepted;
    result.commit_token = effects::RegisteredEffectPayload::FromTrivial(PlanTokenType(), plan.Value());
    return foundation::Result<effects::EffectPrepareResult>::Success(std::move(result));
}
foundation::Result<effects::EffectCommitResult> CombatDamageEffectHandler::Commit(
    const effects::EffectOperation &, const effects::RegisteredEffectPayload &token)
{
    const auto plan = token.AsTrivial<combat::CombatPlan>(PlanTokenType());
    if (!plan)
        return foundation::Result<effects::EffectCommitResult>::Failure(
            Error("gameplay.integration.combat_plan_invalid", "combat plan token invalid"));
    auto committed = combat_.CommitDamage(*plan);
    if (!committed)
        return foundation::Result<effects::EffectCommitResult>::Failure(committed.GetError());
    return foundation::Result<effects::EffectCommitResult>::Success({effects::EffectCommitDisposition::Applied, {}});
}

std::vector<combat::CombatModifier> ProgressionCombatModifierProvider::Collect(
    const combat::DamageRequest &request) const
{
    std::vector<combat::CombatModifier> out;
    for (const auto &m : mappings_)
    {
        const auto subject = m.role == ProgressionCombatRole::Attacker
                                 ? (request.instigator.IsValid() ? request.instigator : request.source)
                                 : request.target;
        if (!subject.IsValid())
            continue;
        auto value = progression_.GetAttribute(subject, m.attribute);
        if (!value)
            continue;
        out.push_back({m.phase, m.operation, m.type, m.priority, MulMicro(value.Value(), m.scale_micro), subject});
    }
    return out;
}
Revision ProgressionCombatModifierProvider::RevisionFor(const combat::DamageRequest &request) const noexcept
{
    std::uint64_t a = 0, b = 0;
    const auto attacker = request.instigator.IsValid() ? request.instigator : request.source;
    if (attacker.IsValid())
    {
        auto p = progression_.GetProfileSnapshot(attacker);
        if (p)
            a = p.Value().revision.Raw();
    }
    if (request.target.IsValid())
    {
        auto p = progression_.GetProfileSnapshot(request.target);
        if (p)
            b = p.Value().revision.Raw();
    }
    return Revision{a * 0x9E3779B97F4A7C15ull ^ (b + 0xBF58476D1CE4E5B9ull)};
}

CombatAbilityResourceProvider::CombatAbilityResourceProvider(combat::CombatService &combat)
    : combat_(combat), reservation_ids_(GameplayObjectId::FromString("framework.abilities.combat_reservations").High())
{
}
bool CombatAbilityResourceProvider::CanAfford(GameplayObjectRef owner, abilities::AbilityResourceTypeId type,
                                              std::int64_t amount) const
{
    auto map = mappings_.find(type);
    if (map == mappings_.end() || amount < 0)
        return false;
    auto state = combat_.GetResource(owner, map->second);
    return state && state.Value().current_micro >= amount;
}
foundation::Result<abilities::AbilityResourceReservation> CombatAbilityResourceProvider::Reserve(
    GameplayObjectRef owner, abilities::AbilityResourceTypeId type, std::int64_t amount, GameplayContext context)
{
    auto map = mappings_.find(type);
    if (map == mappings_.end() || !CanAfford(owner, type, amount))
        return foundation::Result<abilities::AbilityResourceReservation>::Failure(
            Error("gameplay.integration.resource_unavailable", "combat resource unavailable"));
    const auto reservation_id = reservation_ids_.Next();
    if (!reservation_id.IsValid())
        return foundation::Result<abilities::AbilityResourceReservation>::Failure(
            Error("gameplay.integration.id_exhausted", "ability resource reservation id exhausted"));
    auto debit = combat_.ModifyResource(owner, map->second, -amount, context);
    if (!debit)
        return foundation::Result<abilities::AbilityResourceReservation>::Failure(debit.GetError());
    ReservationToken token{owner, map->second, amount};
    active_reservations_.emplace(reservation_id, token);
    abilities::AbilityResourceReservation r;
    r.id = {reservation_id};
    r.resource = type;
    r.provider_token = abilities::RegisteredAbilityPayload::FromTrivial(TokenType(), token);
    return foundation::Result<abilities::AbilityResourceReservation>::Success(std::move(r));
}
void CombatAbilityResourceProvider::Commit(const abilities::AbilityResourceReservation &r, GameplayContext) noexcept
{
    active_reservations_.erase(r.id.value);
}
void CombatAbilityResourceProvider::Release(const abilities::AbilityResourceReservation &r,
                                            GameplayContext context) noexcept
{
    const auto active = active_reservations_.find(r.id.value);
    if (active == active_reservations_.end())
        return;
    const auto token = active->second;
    active_reservations_.erase(active);
    (void)combat_.ModifyResource(token.owner, token.resource, token.amount_micro, context);
}

foundation::Result<std::vector<effects::EffectExecutionResult>> AbilityEffectsDispatcher::Dispatch(
    std::span<const abilities::AbilityOutput> outputs)
{
    std::vector<effects::EffectExecutionResult> results;
    results.reserve(outputs.size());
    for (const auto &o : outputs)
    {
        auto map = mappings_.find(o.action);
        if (map == mappings_.end())
            return foundation::Result<std::vector<effects::EffectExecutionResult>>::Failure(
                Error("gameplay.integration.ability_action_unmapped", "ability action has no effect mapping"));
        effects::EffectRequest request;
        request.definition = map->second;
        request.source = o.owner;
        request.instigator = o.context.instigator.IsValid() ? o.context.instigator : o.owner;
        request.targets = o.targets.targets;
        if (o.targets.primary.IsValid() &&
            std::find(request.targets.begin(), request.targets.end(), o.targets.primary) == request.targets.end())
            request.targets.push_back(o.targets.primary);
        if (request.targets.empty())
            request.targets.push_back(o.owner);
        request.scale_micro = o.magnitude_micro == 0 ? 1'000'000 : o.magnitude_micro;
        request.context = o.context;
        auto r = effects_.Execute(std::move(request));
        if (!r)
            return foundation::Result<std::vector<effects::EffectExecutionResult>>::Failure(r.GetError());
        results.push_back(std::move(r).Value());
    }
    return foundation::Result<std::vector<effects::EffectExecutionResult>>::Success(std::move(results));
}

foundation::Result<void> ConditionProgressionAdapter::ProcessChanges()
{
    const auto changes = conditions_.ChangesSince(cursor_);
    for (const auto &c : changes)
    {
        const auto next_cursor = std::max(cursor_, c.sequence);
        auto map =
            std::find_if(mappings_.begin(), mappings_.end(), [&](const auto &m) { return m.condition == c.type; });
        if (map == mappings_.end())
        {
            cursor_ = next_cursor;
            continue;
        }
        const auto source = ConditionSource(c.instance);
        if (c.kind == conditions::ConditionChangeKind::Removed || c.kind == conditions::ConditionChangeKind::Expired)
        {
            const auto removed = progression_.RemoveModifiersBySource(c.subject, source, c.context);
            (void)removed;
            cursor_ = next_cursor;
            continue;
        }
        if (c.kind != conditions::ConditionChangeKind::Added && c.kind != conditions::ConditionChangeKind::Refreshed &&
            c.kind != conditions::ConditionChangeKind::StackChanged)
        {
            cursor_ = next_cursor;
            continue;
        }
        const auto removed = progression_.RemoveModifiersBySource(c.subject, source, c.context);
        (void)removed;
        const auto *current = conditions_.Find(c.instance);
        if (current == nullptr)
        {
            cursor_ = next_cursor;
            continue;
        }
        progression::ProgressionModifier modifier;
        modifier.target = map->attribute;
        modifier.operation = map->operation;
        modifier.modifier_type = map->modifier_type;
        modifier.priority = map->priority;
        const auto stack_scale_micro = static_cast<std::int64_t>(std::max<std::uint32_t>(1, current->stacks)) * 1'000'000;
        modifier.value_micro = MulMicro(MulMicro(current->magnitude_micro, stack_scale_micro),
                                        map->magnitude_scale_micro);
        modifier.source = source;
        modifier.persistent = true;
        auto added = progression_.AddModifier(c.subject, std::move(modifier), c.context);
        if (!added)
            return foundation::Result<void>::Failure(added.GetError());
        cursor_ = next_cursor;
    }
    return foundation::Result<void>::Success();
}

foundation::Result<ScheduleId> AbilityTimeAdapter::ScheduleExecution(abilities::AbilityExecutionId id)
{
    const auto *e = abilities_.FindExecution(id);
    if (!e)
        return foundation::Result<ScheduleId>::Failure(
            Error("gameplay.integration.ability_execution_missing", "ability execution missing"));
    const auto due = e->state == abilities::AbilityExecutionState::Channeling ? e->next_channel_at : e->due_at;
    const GameplayObjectRef owner{abilities::AbilityService::Domain(), id.value};
    auto scheduled = time_.Schedule(clock_, due, owner, action_, {}, time::CatchUpPolicy::FireOnce,
                                    time::SchedulePersistence::Persistent);
    if (!scheduled)
        return scheduled;
    auto bound = abilities_.BindSchedule(id, scheduled.Value());
    if (!bound)
    {
        auto cancel = time_.Cancel(scheduled.Value());
        (void)cancel;
        return foundation::Result<ScheduleId>::Failure(bound.GetError());
    }
    return scheduled;
}
foundation::Result<std::vector<abilities::AbilityOutput>> AbilityTimeAdapter::ProcessTrigger(
    const time::ScheduledTrigger &t)
{
    if (t.action != action_)
    {
        return foundation::Result<std::vector<abilities::AbilityOutput>>::Failure(
            Error("gameplay.integration.trigger_unhandled", "scheduled trigger does not belong to abilities adapter"));
    }
    std::vector<abilities::AbilityOutput> outputs;
    auto handled = abilities_.NotifyScheduleDue(t.schedule, t.observed_at, outputs);
    if (!handled)
    {
        return foundation::Result<std::vector<abilities::AbilityOutput>>::Failure(handled.GetError());
    }
    const auto id = abilities::AbilityExecutionId{t.owner.id};
    const auto *e = abilities_.FindExecution(id);
    if (e && e->state == abilities::AbilityExecutionState::Channeling)
    {
        auto next = ScheduleExecution(id);
        if (!next)
        {
            return foundation::Result<std::vector<abilities::AbilityOutput>>::Failure(next.GetError());
        }
    }
    return foundation::Result<std::vector<abilities::AbilityOutput>>::Success(std::move(outputs));
}

foundation::Result<loot::RewardDeliveryDisposition> ProgressionRewardHandler::Validate(
    const loot::RewardOperation &o) const
{
    const auto payload = o.payload.AsTrivial<ProgressionRewardPayload>(PayloadType());
    if (!payload || !progression_.HasProfile(o.recipient) || !progression_.GetTrack(o.recipient, payload->track))
        return foundation::Result<loot::RewardDeliveryDisposition>::Success(loot::RewardDeliveryDisposition::Rejected);
    return foundation::Result<loot::RewardDeliveryDisposition>::Success(loot::RewardDeliveryDisposition::Delivered);
}
foundation::Result<loot::RewardDeliveryStage> ProgressionRewardHandler::Prepare(const loot::RewardOperation &o)
{
    const auto payload = o.payload.AsTrivial<ProgressionRewardPayload>(PayloadType());
    if (!payload)
        return foundation::Result<loot::RewardDeliveryStage>::Failure(
            Error("gameplay.integration.reward_payload_invalid", "progression reward payload invalid"));
    auto reservation = progression_.ReserveProgressGrant(o.recipient, payload->track, o.quantity_micro, o.context);
    if (!reservation)
        return foundation::Result<loot::RewardDeliveryStage>::Failure(reservation.GetError());
    loot::RewardDeliveryStage stage;
    stage.operation = o;
    stage.operation.payload = loot::RegisteredRewardPayload::FromTrivial(StagePayloadType(), StagePayload{reservation.Value()});
    stage.disposition = loot::RewardDeliveryDisposition::Delivered;
    return foundation::Result<loot::RewardDeliveryStage>::Success(std::move(stage));
}
void ProgressionRewardHandler::Commit(loot::RewardDeliveryStage &stage) noexcept
{
    const auto payload = stage.operation.payload.AsTrivial<StagePayload>(StagePayloadType());
    if (!payload)
        return;
    progression_.CommitProgressGrant(payload->reservation);
    stage.operation.payload = {};
}
void ProgressionRewardHandler::Cancel(loot::RewardDeliveryStage &stage) noexcept
{
    const auto payload = stage.operation.payload.AsTrivial<StagePayload>(StagePayloadType());
    if (!payload)
        return;
    progression_.ReleaseProgressGrant(payload->reservation);
    stage.operation.payload = {};
}

foundation::Result<std::vector<loot::RewardExecutionId>> DeathRewardAdapter::ProcessChanges()
{
    std::vector<loot::RewardExecutionId> out;
    const auto batch = combat_.ReadChangesSince(cursor_);
    if (batch.snapshot_required)
        return foundation::Result<std::vector<loot::RewardExecutionId>>::Failure(
            Error("gameplay.integration.change_gap", "combat change journal gap requires reconciliation from snapshot"));
    for (const auto &c : batch.changes)
    {
        if (c.kind == combat::CombatChangeKind::LifeStateChanged && c.life_state == combat::CombatLifeState::Dead)
        {
            const auto table = tables_.find(c.subject);
            if (table != tables_.end() && c.context.instigator.IsValid())
            {
                loot::LootContext context;
                context.source = c.subject;
                context.recipient = c.context.instigator;
                context.instigator = c.context.instigator;
                context.gameplay = c.context;
                context.seed = {random::StableMix(c.subject.id.Low() ^ c.resolution.value.Low() ^ c.sequence)};
                auto bundle = loot_.Generate(table->second, context);
                if (!bundle)
                    return foundation::Result<std::vector<loot::RewardExecutionId>>::Failure(bundle.GetError());
                auto pending = loot_.MakePending(std::move(bundle).Value());
                if (!pending)
                    return foundation::Result<std::vector<loot::RewardExecutionId>>::Failure(pending.GetError());
                out.push_back(pending.Value());
            }
        }
        cursor_ = c.sequence;
    }
    return foundation::Result<std::vector<loot::RewardExecutionId>>::Success(std::move(out));
}
} // namespace epidemic::gameplay::integration

