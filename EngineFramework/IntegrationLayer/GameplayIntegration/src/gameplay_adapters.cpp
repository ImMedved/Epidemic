#include "Epidemic/GameFramework/GameplayIntegration/gameplay_adapters.h"
#include "Epidemic/Foundation/error.h"
#include "Epidemic/GameFramework/SupportRandom/deterministic_random.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <type_traits>
#include <unordered_set>

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
[[nodiscard]] std::uint64_t HashCombine(std::uint64_t seed, std::uint64_t value) noexcept
{
    return random::StableMix(seed ^ (random::StableMix(value) + 0x9E3779B97F4A7C15ull));
}
[[nodiscard]] std::uint64_t NonZero(std::uint64_t value) noexcept
{
    return value == 0 ? 1 : value;
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
    const effects::EffectOperation &operation, const effects::RegisteredEffectPayload &token) noexcept
{
    const auto plan = token.AsTrivial<combat::CombatPlan>(PlanTokenType());
    if (!plan)
        return foundation::Result<effects::EffectCommitResult>::Failure(
            Error("gameplay.integration.combat_plan_invalid", "combat plan token invalid"));
    auto committed = combat_.CommitDamage(*plan, operation.context.time);
    if (!committed)
        return foundation::Result<effects::EffectCommitResult>::Failure(committed.GetError());
    return foundation::Result<effects::EffectCommitResult>::Success({effects::EffectCommitDisposition::Applied, {}});
}

foundation::Result<void> ProgressionCombatModifierProvider::AddMapping(ProgressionCombatMapping mapping)
{
    if (mappings_frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.integration.mapping_frozen", "combat modifier mappings are frozen"));
    if (!mapping.attribute.IsValid() || !mapping.type.value.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.integration.mapping_invalid", "combat modifier mapping is invalid"));
    const auto duplicate = std::find_if(mappings_.begin(), mappings_.end(), [&](const auto &m) {
        return m.attribute == mapping.attribute && m.role == mapping.role && m.phase == mapping.phase &&
               m.type == mapping.type;
    });
    if (duplicate != mappings_.end())
        return foundation::Result<void>::Failure(Error("gameplay.integration.mapping_duplicate", "duplicate combat modifier mapping"));
    mappings_.push_back(mapping);
    return foundation::Result<void>::Success();
}
foundation::Result<void> ProgressionCombatModifierProvider::FreezeMappings()
{
    if (mappings_frozen_)
        return foundation::Result<void>::Success();
    std::sort(mappings_.begin(), mappings_.end(), [](const auto &a, const auto &b) {
        if (a.attribute != b.attribute) return a.attribute < b.attribute;
        if (a.role != b.role) return a.role < b.role;
        if (a.phase != b.phase) return a.phase < b.phase;
        if (a.type != b.type) return a.type < b.type;
        if (a.priority != b.priority) return a.priority < b.priority;
        if (a.operation != b.operation) return a.operation < b.operation;
        return a.scale_micro < b.scale_micro;
    });
    std::uint64_t hash = 0x4A1D0B3C51E7A991ull;
    for (const auto &m : mappings_)
    {
        hash = HashCombine(hash, m.attribute.value.Raw());
        hash = HashCombine(hash, static_cast<std::uint64_t>(m.role));
        hash = HashCombine(hash, static_cast<std::uint64_t>(m.phase));
        hash = HashCombine(hash, static_cast<std::uint64_t>(m.operation));
        hash = HashCombine(hash, m.type.value.Raw());
        hash = HashCombine(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(m.priority)));
        hash = HashCombine(hash, static_cast<std::uint64_t>(m.scale_micro));
    }
    mapping_revision_ = NonZero(hash);
    mappings_frozen_ = true;
    return foundation::Result<void>::Success();
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
    auto combined = a * 0x9E3779B97F4A7C15ull ^ (b + 0xBF58476D1CE4E5B9ull);
    combined = HashCombine(combined, mapping_revision_);
    return Revision{NonZero(combined)};
}

foundation::Result<void> CombatAbilityResourceProvider::AddMapping(CombatAbilityResourceMapping mapping)
{
    if (mappings_frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.integration.mapping_frozen", "ability resource mappings are frozen"));
    if (!mapping.ability.IsValid() || !mapping.combat.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.integration.mapping_invalid", "ability resource mapping is invalid"));
    if (mappings_.contains(mapping.ability))
        return foundation::Result<void>::Failure(Error("gameplay.integration.mapping_duplicate", "duplicate ability resource mapping"));
    mappings_.emplace(mapping.ability, mapping.combat);
    return foundation::Result<void>::Success();
}
foundation::Result<void> CombatAbilityResourceProvider::FreezeMappings()
{
    if (mappings_frozen_)
        return foundation::Result<void>::Success();
    std::vector<std::pair<abilities::AbilityResourceTypeId, combat::CombatResourceTypeId>> ordered(mappings_.begin(), mappings_.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    std::uint64_t hash = 0xD84F21A57C93B061ull;
    for (const auto &[ability, resource] : ordered)
    {
        hash = HashCombine(hash, ability.value.Raw());
        hash = HashCombine(hash, resource.value.Raw());
    }
    mapping_revision_ = NonZero(hash);
    mappings_frozen_ = true;
    return foundation::Result<void>::Success();
}
bool CombatAbilityResourceProvider::CanAfford(GameplayObjectRef owner, abilities::AbilityResourceTypeId type,
                                              std::int64_t amount) const
{
    if (!mappings_frozen_)
        return false;
    auto map = mappings_.find(type);
    if (map == mappings_.end() || amount < 0)
        return false;
    auto state = combat_.GetResource(owner, map->second);
    return state && state.Value().current_micro >= amount;
}
foundation::Result<abilities::AbilityResourceReservation> CombatAbilityResourceProvider::Reserve(
    GameplayObjectRef owner, abilities::AbilityResourceTypeId type, std::int64_t amount, GameplayContext context)
{
    if (!mappings_frozen_)
        return foundation::Result<abilities::AbilityResourceReservation>::Failure(
            Error("gameplay.integration.mapping_not_frozen", "ability resource mappings must be frozen"));
    auto map = mappings_.find(type);
    if (map == mappings_.end() || amount < 0)
        return foundation::Result<abilities::AbilityResourceReservation>::Failure(
            Error("gameplay.integration.resource_unavailable", "combat resource unavailable"));
    auto reserved = combat_.ReserveResource(owner, map->second, amount, context);
    if (!reserved)
        return foundation::Result<abilities::AbilityResourceReservation>::Failure(reserved.GetError());
    const ReservationToken token{reserved.Value(), map->second, amount};
    abilities::AbilityResourceReservation result;
    result.id = {reserved.Value().value};
    result.resource = type;
    result.provider_token = abilities::RegisteredAbilityPayload::FromTrivial(TokenType(), token);
    return foundation::Result<abilities::AbilityResourceReservation>::Success(std::move(result));
}
void CombatAbilityResourceProvider::Commit(const abilities::AbilityResourceReservation &reservation, GameplayContext) noexcept
{
    const auto token = reservation.provider_token.AsTrivial<ReservationToken>(TokenType());
    if (!token || reservation.id.value != token->reservation.value)
        return;
    combat_.CommitResourceReservation(token->reservation);
}
void CombatAbilityResourceProvider::Release(const abilities::AbilityResourceReservation &reservation,
                                            GameplayContext context) noexcept
{
    const auto token = reservation.provider_token.AsTrivial<ReservationToken>(TokenType());
    if (!token || reservation.id.value != token->reservation.value)
        return;
    combat_.ReleaseResourceReservation(token->reservation, context);
}
foundation::Result<void> CombatAbilityResourceProvider::ReconcileReservation(
    const abilities::AbilityResourceReservation &reservation, GameplayObjectRef owner, GameplayContext)
{
    if (!mappings_frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.mapping_not_frozen", "ability resource mappings must be frozen"));
    const auto token = reservation.provider_token.AsTrivial<ReservationToken>(TokenType());
    if (!reservation.id.IsValid() || !reservation.resource.IsValid() || !token ||
        reservation.id.value != token->reservation.value || token->amount_micro < 0)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.resource_reservation_invalid", "ability resource reservation is invalid"));
    const auto mapping = mappings_.find(reservation.resource);
    if (mapping == mappings_.end() || mapping->second != token->resource)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.resource_reservation_invalid", "ability resource reservation mapping is invalid"));
    const auto *current = combat_.FindResourceReservation(token->reservation);
    if (!current || current->subject != owner || current->resource != token->resource ||
        current->amount_micro != token->amount_micro)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.resource_reservation_missing", "authoritative combat reservation is missing or mismatched"));
    return foundation::Result<void>::Success();
}

foundation::Result<void> AbilityEffectsDispatcher::Map(ActionTypeId action, effects::EffectDefinitionId definition)
{
    if (mappings_frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.integration.mapping_frozen", "ability effect mappings are frozen"));
    if (!action.IsValid() || !definition.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.integration.mapping_invalid", "ability effect mapping is invalid"));
    if (mappings_.contains(action))
        return foundation::Result<void>::Failure(Error("gameplay.integration.mapping_duplicate", "duplicate ability effect mapping"));
    mappings_.emplace(action, definition);
    return foundation::Result<void>::Success();
}
foundation::Result<void> AbilityEffectsDispatcher::FreezeMappings()
{
    if (mappings_frozen_)
        return foundation::Result<void>::Success();
    std::vector<std::pair<ActionTypeId, effects::EffectDefinitionId>> ordered(mappings_.begin(), mappings_.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    std::uint64_t hash = 0x381F79C20BEA54D7ull;
    for (const auto &[action, definition] : ordered)
    {
        if (effects_.FindDefinition(definition) == nullptr)
            return foundation::Result<void>::Failure(
                Error("gameplay.integration.mapping_invalid", "ability effect mapping references unknown effect definition"));
        hash = HashCombine(hash, action.Raw());
        hash = HashCombine(hash, definition.value.Raw());
    }
    mapping_revision_ = NonZero(hash);
    mappings_frozen_ = true;
    return foundation::Result<void>::Success();
}
foundation::Result<std::vector<effects::EffectExecutionResult>> AbilityEffectsDispatcher::Dispatch(
    std::span<const abilities::AbilityOutput> outputs)
{
    if (!mappings_frozen_)
        return foundation::Result<std::vector<effects::EffectExecutionResult>>::Failure(
            Error("gameplay.integration.mapping_not_frozen", "ability effect mappings must be frozen"));

    std::size_t new_deliveries = 0;
    std::vector<AbilityEffectDeliveryKey> batch_keys;
    batch_keys.reserve(outputs.size());
    for (const auto &output : outputs)
    {
        if (!output.execution.IsValid() || !mappings_.contains(output.action))
            return foundation::Result<std::vector<effects::EffectExecutionResult>>::Failure(
                Error("gameplay.integration.ability_action_unmapped", "ability action has no valid frozen effect mapping"));
        const AbilityEffectDeliveryKey key{output.execution, output.occurrence_at, output.output_index};
        const auto duplicate_in_batch = std::find(batch_keys.begin(), batch_keys.end(), key);
        if (duplicate_in_batch != batch_keys.end())
            return foundation::Result<std::vector<effects::EffectExecutionResult>>::Failure(
                Error("gameplay.integration.ability_output_duplicate", "ability output delivery key is duplicated"));
        batch_keys.push_back(key);
        if (std::none_of(delivered_.begin(), delivered_.end(), [&](const auto &record) { return record.key == key; }))
            ++new_deliveries;
    }
    if (new_deliveries > kDeliveryCapacity - std::min(kDeliveryCapacity, delivered_.size()))
        return foundation::Result<std::vector<effects::EffectExecutionResult>>::Failure(
            Error("gameplay.integration.delivery_capacity", "ability effect delivery ledger is full"));

    std::vector<effects::EffectExecutionResult> results;
    results.reserve(outputs.size());
    for (const auto &output : outputs)
    {
        const AbilityEffectDeliveryKey key{output.execution, output.occurrence_at, output.output_index};
        const auto delivered = std::find_if(delivered_.begin(), delivered_.end(), [&](const auto &record) {
            return record.key == key;
        });
        if (delivered != delivered_.end())
        {
            results.push_back(delivered->result);
            continue;
        }
        const auto mapping = mappings_.find(output.action);
        effects::EffectRequest request;
        request.definition = mapping->second;
        request.source = output.owner;
        request.instigator = output.context.instigator.IsValid() ? output.context.instigator : output.owner;
        request.targets = output.targets.targets;
        if (output.targets.primary.IsValid() &&
            std::find(request.targets.begin(), request.targets.end(), output.targets.primary) == request.targets.end())
            request.targets.push_back(output.targets.primary);
        if (request.targets.empty())
            request.targets.push_back(output.owner);
        request.scale_micro = output.magnitude_micro == 0 ? 1'000'000 : output.magnitude_micro;
        request.context = output.context;
        if (!request.context.operation.IsValid())
            request.context.operation = OperationId::FromRaw(output.execution.value.High(),
                                                             random::StableMix(output.execution.value.Low() ^
                                                                               static_cast<std::uint64_t>(output.occurrence_at.ticks) ^
                                                                               output.output_index));
        auto executed = effects_.Execute(std::move(request));
        if (!executed)
            return foundation::Result<std::vector<effects::EffectExecutionResult>>::Failure(executed.GetError());
        auto value = std::move(executed).Value();
        delivered_.push_back({key, value});
        results.push_back(std::move(value));
    }
    return foundation::Result<std::vector<effects::EffectExecutionResult>>::Success(std::move(results));
}
AbilityEffectsCheckpoint AbilityEffectsDispatcher::CaptureCheckpoint() const
{
    AbilityEffectsCheckpoint checkpoint;
    checkpoint.mapping_revision = mapping_revision_;
    checkpoint.delivered = delivered_;
    std::sort(checkpoint.delivered.begin(), checkpoint.delivered.end(),
              [](const auto &a, const auto &b) { return a.key < b.key; });
    return checkpoint;
}
foundation::Result<void> AbilityEffectsDispatcher::RestoreCheckpoint(AbilityEffectsCheckpoint checkpoint)
{
    if (!mappings_frozen_ || checkpoint.mapping_revision != mapping_revision_ || checkpoint.delivered.size() > kDeliveryCapacity)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.checkpoint_invalid", "ability effect checkpoint is incompatible with frozen mappings"));
    std::sort(checkpoint.delivered.begin(), checkpoint.delivered.end(),
              [](const auto &a, const auto &b) { return a.key < b.key; });
    for (std::size_t i = 0; i < checkpoint.delivered.size(); ++i)
    {
        const auto &record = checkpoint.delivered[i];
        if (!record.key.execution.IsValid() || !record.result.execution.IsValid() ||
            (i != 0 && checkpoint.delivered[i - 1].key == record.key))
            return foundation::Result<void>::Failure(
                Error("gameplay.integration.checkpoint_invalid", "ability effect checkpoint contains invalid delivery records"));
    }
    delivered_ = std::move(checkpoint.delivered);
    return foundation::Result<void>::Success();
}
std::uint64_t AbilityEffectsDispatcher::PruneDeliveriesForExecution(abilities::AbilityExecutionId execution) noexcept
{
    const auto before = delivered_.size();
    std::erase_if(delivered_, [&](const auto &record) { return record.key.execution == execution; });
    return before - delivered_.size();
}

foundation::Result<void> ConditionProgressionAdapter::AddMapping(ConditionProgressionMapping mapping)
{
    if (mappings_frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.integration.mapping_frozen", "condition progression mappings are frozen"));
    if (!mapping.condition.IsValid() || !mapping.attribute.IsValid() || !mapping.modifier_type.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.integration.mapping_invalid", "condition progression mapping is invalid"));
    const auto duplicate = std::find_if(mappings_.begin(), mappings_.end(), [&](const auto &m) {
        return m.condition == mapping.condition && m.attribute == mapping.attribute && m.modifier_type == mapping.modifier_type;
    });
    if (duplicate != mappings_.end())
        return foundation::Result<void>::Failure(Error("gameplay.integration.mapping_duplicate", "duplicate condition progression mapping"));
    mappings_.push_back(mapping);
    return foundation::Result<void>::Success();
}
foundation::Result<void> ConditionProgressionAdapter::FreezeMappings()
{
    if (mappings_frozen_)
        return foundation::Result<void>::Success();
    std::sort(mappings_.begin(), mappings_.end(), [](const auto &a, const auto &b) {
        if (a.condition != b.condition) return a.condition < b.condition;
        if (a.attribute != b.attribute) return a.attribute < b.attribute;
        if (a.modifier_type != b.modifier_type) return a.modifier_type < b.modifier_type;
        if (a.priority != b.priority) return a.priority < b.priority;
        if (a.operation != b.operation) return a.operation < b.operation;
        return a.magnitude_scale_micro < b.magnitude_scale_micro;
    });
    std::uint64_t hash = 0xF415F29C8106D37Bull;
    for (const auto &m : mappings_)
    {
        hash = HashCombine(hash, m.condition.value.Raw());
        hash = HashCombine(hash, m.attribute.value.Raw());
        hash = HashCombine(hash, static_cast<std::uint64_t>(m.operation));
        hash = HashCombine(hash, m.modifier_type.Raw());
        hash = HashCombine(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(m.priority)));
        hash = HashCombine(hash, static_cast<std::uint64_t>(m.magnitude_scale_micro));
    }
    mapping_revision_ = NonZero(hash);
    mappings_frozen_ = true;
    return foundation::Result<void>::Success();
}
foundation::Result<void> ConditionProgressionAdapter::Project(const conditions::ConditionInstance &condition,
                                                              GameplayContext context)
{
    const auto source = ConditionSource(condition.id);
    std::vector<progression::ProgressionModifier> modifiers;
    for (const auto &mapping : mappings_)
    {
        if (mapping.condition != condition.type)
            continue;
        progression::ProgressionModifier modifier;
        modifier.target = mapping.attribute;
        modifier.operation = mapping.operation;
        modifier.modifier_type = mapping.modifier_type;
        modifier.priority = mapping.priority;
        const auto stacks = static_cast<std::int64_t>(std::max<std::uint32_t>(1, condition.stacks));
        const auto stack_scale = stacks > std::numeric_limits<std::int64_t>::max() / 1'000'000
                                     ? std::numeric_limits<std::int64_t>::max()
                                     : stacks * 1'000'000;
        modifier.value_micro = MulMicro(MulMicro(condition.magnitude_micro, stack_scale),
                                        mapping.magnitude_scale_micro);
        modifier.source = source;
        modifier.persistent = true;
        modifiers.push_back(std::move(modifier));
    }
    auto replaced = progression_.ReplaceModifiersBySource(condition.subject, source, std::move(modifiers), context);
    if (!replaced)
        return foundation::Result<void>::Failure(replaced.GetError());
    return foundation::Result<void>::Success();
}
foundation::Result<void> ConditionProgressionAdapter::ReconcileAll()
{
    if (!mappings_frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.mapping_not_frozen", "condition progression mappings must be frozen"));
    const auto conditions = conditions_.AllConditions();
    std::unordered_set<GameplayObjectRef> desired_sources;
    desired_sources.reserve(conditions.size());
    for (const auto &condition : conditions)
        desired_sources.insert(ConditionSource(condition.id));

    const auto progression_snapshot = progression_.CaptureSnapshot();
    for (const auto &profile : progression_snapshot.profiles)
    {
        std::vector<GameplayObjectRef> stale_sources;
        for (const auto &modifier : profile.modifiers)
            if (modifier.source.domain == conditions::ConditionService::Domain() && !desired_sources.contains(modifier.source) &&
                std::find(stale_sources.begin(), stale_sources.end(), modifier.source) == stale_sources.end())
                stale_sources.push_back(modifier.source);
        for (const auto source : stale_sources)
            (void)progression_.RemoveModifiersBySource(profile.subject, source, {});
    }
    for (const auto &condition : conditions)
    {
        auto projected = Project(condition, {});
        if (!projected)
            return projected;
    }
    const auto snapshot = conditions_.CaptureSnapshot();
    cursor_ = snapshot.next_change_sequence == 0 ? 0 : snapshot.next_change_sequence - 1;
    return foundation::Result<void>::Success();
}
foundation::Result<void> ConditionProgressionAdapter::ProcessChanges()
{
    if (!mappings_frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.mapping_not_frozen", "condition progression mappings must be frozen"));
    const auto batch = conditions_.ReadChangesSince(cursor_);
    if (batch.snapshot_required)
        return ReconcileAll();
    for (const auto &change : batch.changes)
    {
        const auto next_cursor = std::max(cursor_, change.sequence);
        const auto source = ConditionSource(change.instance);
        if (change.kind == conditions::ConditionChangeKind::Removed || change.kind == conditions::ConditionChangeKind::Expired)
        {
            (void)progression_.RemoveModifiersBySource(change.subject, source, change.context);
            cursor_ = next_cursor;
            continue;
        }
        if (change.kind == conditions::ConditionChangeKind::Added || change.kind == conditions::ConditionChangeKind::Refreshed ||
            change.kind == conditions::ConditionChangeKind::StackChanged)
        {
            const auto *current = conditions_.Find(change.instance);
            if (current == nullptr)
                (void)progression_.RemoveModifiersBySource(change.subject, source, change.context);
            else
            {
                auto projected = Project(*current, change.context);
                if (!projected)
                    return projected;
            }
        }
        cursor_ = next_cursor;
    }
    return foundation::Result<void>::Success();
}
foundation::Result<void> ConditionProgressionAdapter::RestoreCheckpoint(ConditionProgressionCheckpoint checkpoint)
{
    if (!mappings_frozen_ || checkpoint.mapping_revision != mapping_revision_)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.checkpoint_invalid", "condition progression checkpoint mapping revision mismatch"));
    cursor_ = checkpoint.cursor;
    return foundation::Result<void>::Success();
}

foundation::Result<ScheduleId> AbilityTimeAdapter::ScheduleExecution(abilities::AbilityExecutionId id)
{
    const auto *execution = abilities_.FindExecution(id);
    if (!execution)
        return foundation::Result<ScheduleId>::Failure(
            Error("gameplay.integration.ability_execution_missing", "ability execution missing"));
    if (execution->schedule)
        return foundation::Result<ScheduleId>::Success(*execution->schedule);
    const auto due = execution->state == abilities::AbilityExecutionState::Channeling
                         ? execution->next_channel_at
                         : execution->due_at;
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
    const time::ScheduledTrigger &trigger)
{
    if (trigger.action != action_ || trigger.owner.domain != abilities::AbilityService::Domain())
        return foundation::Result<std::vector<abilities::AbilityOutput>>::Failure(
            Error("gameplay.integration.trigger_unhandled", "scheduled trigger does not belong to abilities adapter"));
    const auto id = abilities::AbilityExecutionId{trigger.owner.id};

    auto pending = std::find_if(pending_.begin(), pending_.end(),
                                [&](const auto &entry) { return entry.trigger == trigger.schedule; });
    if (pending != pending_.end())
    {
        if (pending->execution != id || pending->observed_at != trigger.observed_at)
            return foundation::Result<std::vector<abilities::AbilityOutput>>::Failure(
                Error("gameplay.integration.trigger_conflict", "pending ability trigger conflicts with retry"));
        const auto *execution = abilities_.FindExecution(id);
        if (execution && execution->state == abilities::AbilityExecutionState::Channeling && !execution->schedule)
        {
            auto rebound = ScheduleExecution(id);
            if (!rebound)
                return foundation::Result<std::vector<abilities::AbilityOutput>>::Failure(rebound.GetError());
        }
        return foundation::Result<std::vector<abilities::AbilityOutput>>::Success(pending->outputs);
    }

    const auto *before = abilities_.FindExecution(id);
    if (!before)
        return foundation::Result<std::vector<abilities::AbilityOutput>>::Success({});
    if (before->schedule && *before->schedule != trigger.schedule)
        return foundation::Result<std::vector<abilities::AbilityOutput>>::Success({});
    if (!before->schedule && before->state == abilities::AbilityExecutionState::Channeling &&
        before->next_channel_at.ticks > trigger.observed_at.ticks)
    {
        auto rebound = ScheduleExecution(id);
        if (!rebound)
            return foundation::Result<std::vector<abilities::AbilityOutput>>::Failure(rebound.GetError());
        return foundation::Result<std::vector<abilities::AbilityOutput>>::Success({});
    }

    if (pending_.size() >= kPendingCapacity)
        return foundation::Result<std::vector<abilities::AbilityOutput>>::Failure(
            Error("gameplay.integration.delivery_capacity", "ability time pending-trigger ledger is full"));
    std::vector<abilities::AbilityOutput> outputs;
    auto handled = abilities_.NotifyScheduleDue(trigger.schedule, trigger.observed_at, outputs);
    if (!handled)
        return foundation::Result<std::vector<abilities::AbilityOutput>>::Failure(handled.GetError());

    pending_.push_back({trigger.schedule, id, trigger.observed_at, outputs});
    pending = std::prev(pending_.end());

    const auto *after = abilities_.FindExecution(id);
    if (after && after->state == abilities::AbilityExecutionState::Channeling)
    {
        auto next = ScheduleExecution(id);
        if (!next)
            return foundation::Result<std::vector<abilities::AbilityOutput>>::Failure(next.GetError());
    }
    return foundation::Result<std::vector<abilities::AbilityOutput>>::Success(pending->outputs);
}

const AbilityTimePendingTrigger* AbilityTimeAdapter::FindPendingOutputs(ScheduleId trigger) const noexcept
{
    const auto found = std::find_if(pending_.begin(), pending_.end(),
                                    [trigger](const auto& entry) { return entry.trigger == trigger; });
    return found == pending_.end() ? nullptr : &*found;
}

foundation::Result<void> AbilityTimeAdapter::AcknowledgeOutputs(ScheduleId trigger)
{
    const auto found = std::find_if(pending_.begin(), pending_.end(),
                                    [trigger](const auto& entry) { return entry.trigger == trigger; });
    if (found == pending_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.ability_output_missing", "ability output delivery is not pending"));
    pending_.erase(found);
    return foundation::Result<void>::Success();
}

bool AbilityTimeAdapter::HasPendingOutputsForExecution(abilities::AbilityExecutionId execution) const noexcept
{
    return std::any_of(pending_.begin(), pending_.end(),
                       [execution](const AbilityTimePendingTrigger& entry) { return entry.execution == execution; });
}

foundation::Result<std::vector<effects::EffectExecutionResult>> AbilityOutputDeliveryCoordinator::DeliverPendingOutputs(
    ScheduleId trigger)
{
    const auto* pending = time_adapter_.FindPendingOutputs(trigger);
    if (!pending)
        return foundation::Result<std::vector<effects::EffectExecutionResult>>::Failure(
            Error("gameplay.integration.ability_output_missing", "ability output delivery is not pending"));

    const auto execution = pending->execution;
    auto delivered = effects_dispatcher_.Dispatch(pending->outputs);
    if (!delivered)
        return foundation::Result<std::vector<effects::EffectExecutionResult>>::Failure(delivered.GetError());

    auto acknowledged = time_adapter_.AcknowledgeOutputs(trigger);
    if (!acknowledged)
        return foundation::Result<std::vector<effects::EffectExecutionResult>>::Failure(acknowledged.GetError());

    (void)PruneSafeTerminalDeliveries(execution);
    return delivered;
}

std::uint64_t AbilityOutputDeliveryCoordinator::PruneSafeTerminalDeliveries(
    abilities::AbilityExecutionId execution) noexcept
{
    if (time_adapter_.HasPendingOutputsForExecution(execution))
        return 0;

    const auto* state = abilities_.FindExecution(execution);
    if (state &&
        state->state != abilities::AbilityExecutionState::Completed &&
        state->state != abilities::AbilityExecutionState::Cancelled &&
        state->state != abilities::AbilityExecutionState::Interrupted &&
        state->state != abilities::AbilityExecutionState::Failed)
        return 0;

    return effects_dispatcher_.PruneDeliveriesForExecution(execution);
}

foundation::Result<void> AbilityTimeAdapter::RestoreCheckpoint(AbilityTimeCheckpoint checkpoint)
{
    if (checkpoint.clock != clock_ || checkpoint.action != action_ || checkpoint.pending.size() > kPendingCapacity)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.checkpoint_invalid", "ability time checkpoint does not match adapter configuration"));
    std::sort(checkpoint.pending.begin(), checkpoint.pending.end(),
              [](const auto &a, const auto &b) { return a.trigger < b.trigger; });
    for (std::size_t i = 0; i < checkpoint.pending.size(); ++i)
    {
        const auto &entry = checkpoint.pending[i];
        if (!entry.trigger.IsValid() || !entry.execution.IsValid() ||
            (i != 0 && checkpoint.pending[i - 1].trigger == entry.trigger))
            return foundation::Result<void>::Failure(
                Error("gameplay.integration.checkpoint_invalid", "ability time checkpoint contains invalid pending triggers"));
        for (const auto &output : entry.outputs)
            if (output.execution != entry.execution)
                return foundation::Result<void>::Failure(
                    Error("gameplay.integration.checkpoint_invalid", "ability time pending output belongs to another execution"));
    }
    pending_ = std::move(checkpoint.pending);
    return foundation::Result<void>::Success();
}

foundation::Result<loot::RewardDeliveryStage> ProgressionRewardHandler::Prepare(const loot::RewardOperation &operation)
{
    const auto payload = operation.payload.AsTrivial<ProgressionRewardPayload>(PayloadType());
    if (!payload)
        return foundation::Result<loot::RewardDeliveryStage>::Failure(
            Error("gameplay.integration.reward_payload_invalid", "progression reward payload invalid"));
    auto reservation = progression_.ReserveProgressGrant(operation.recipient, payload->track, operation.quantity_micro,
                                                         operation.context);
    if (!reservation)
        return foundation::Result<loot::RewardDeliveryStage>::Failure(reservation.GetError());
    loot::RewardDeliveryStage stage;
    stage.operation = operation;
    stage.operation.payload = loot::RegisteredRewardPayload::FromTrivial(StagePayloadType(),
                                                                          StagePayload{reservation.Value()});
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

foundation::Result<void> DeathRewardAdapter::SetTable(GameplayObjectRef subject, loot::LootTableId table)
{
    if (mappings_frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.integration.mapping_frozen", "death reward mappings are frozen"));
    if (!subject.IsValid() || !table.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.integration.mapping_invalid", "death reward mapping is invalid"));
    if (tables_.contains(subject))
        return foundation::Result<void>::Failure(Error("gameplay.integration.mapping_duplicate", "duplicate death reward mapping"));
    tables_.emplace(subject, table);
    return foundation::Result<void>::Success();
}
foundation::Result<void> DeathRewardAdapter::FreezeMappings()
{
    if (mappings_frozen_)
        return foundation::Result<void>::Success();
    std::vector<std::pair<GameplayObjectRef, loot::LootTableId>> ordered(tables_.begin(), tables_.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    std::uint64_t hash = 0x1B8097E465D2AC3Full;
    for (const auto &[subject, table] : ordered)
    {
        hash = HashCombine(hash, subject.domain.Raw());
        hash = HashCombine(hash, subject.id.High());
        hash = HashCombine(hash, subject.id.Low());
        hash = HashCombine(hash, table.value.Raw());
    }
    mapping_revision_ = NonZero(hash);
    mappings_frozen_ = true;
    return foundation::Result<void>::Success();
}
foundation::Result<std::vector<loot::RewardExecutionId>> DeathRewardAdapter::ProcessChanges()
{
    if (!mappings_frozen_)
        return foundation::Result<std::vector<loot::RewardExecutionId>>::Failure(
            Error("gameplay.integration.mapping_not_frozen", "death reward mappings must be frozen"));
    std::vector<loot::RewardExecutionId> out;
    const auto batch = combat_.ReadChangesSince(cursor_);
    if (batch.snapshot_required)
        return foundation::Result<std::vector<loot::RewardExecutionId>>::Failure(
            Error("gameplay.integration.change_gap", "combat change journal gap requires reconciliation from snapshot"));
    if (batch.oldest_available_sequence != 0)
        std::erase_if(deliveries_, [&](const auto &record) {
            return record.key.combat_sequence < batch.oldest_available_sequence;
        });
    for (const auto &change : batch.changes)
    {
        if (change.kind == combat::CombatChangeKind::LifeStateChanged && change.life_state == combat::CombatLifeState::Dead)
        {
            const auto table = tables_.find(change.subject);
            if (table != tables_.end() && change.context.instigator.IsValid())
            {
                const DeathRewardDeliveryKey key{change.sequence, change.subject, table->second};
                auto delivery = std::find_if(deliveries_.begin(), deliveries_.end(),
                                             [&](const auto &record) { return record.key == key; });
                if (delivery == deliveries_.end())
                {
                    if (deliveries_.size() >= kDeliveryCapacity)
                        return foundation::Result<std::vector<loot::RewardExecutionId>>::Failure(
                            Error("gameplay.integration.delivery_capacity", "death reward delivery ledger is full"));
                    loot::LootContext context;
                    context.source = change.subject;
                    context.recipient = change.context.instigator;
                    context.instigator = change.context.instigator;
                    context.gameplay = change.context;
                    context.seed = {random::StableMix(change.subject.id.Low() ^ change.resolution.value.Low() ^
                                                      change.sequence ^ table->second.value.Raw())};
                    auto bundle = loot_.GenerateTracked(table->second, context);
                    if (!bundle)
                        return foundation::Result<std::vector<loot::RewardExecutionId>>::Failure(bundle.GetError());
                    deliveries_.push_back({key, bundle.Value().id, DeathRewardDeliveryState::Generated});
                    delivery = std::prev(deliveries_.end());
                }
                if (delivery->state == DeathRewardDeliveryState::Generated)
                {
                    auto pending = loot_.MakePending(delivery->reward);
                    if (!pending)
                        return foundation::Result<std::vector<loot::RewardExecutionId>>::Failure(pending.GetError());
                    delivery->state = DeathRewardDeliveryState::Pending;
                }
                out.push_back(delivery->reward);
            }
        }
        cursor_ = change.sequence;
    }
    return foundation::Result<std::vector<loot::RewardExecutionId>>::Success(std::move(out));
}
DeathRewardCheckpoint DeathRewardAdapter::CaptureCheckpoint() const
{
    DeathRewardCheckpoint checkpoint;
    checkpoint.cursor = cursor_;
    checkpoint.mapping_revision = mapping_revision_;
    checkpoint.deliveries = deliveries_;
    std::sort(checkpoint.deliveries.begin(), checkpoint.deliveries.end(),
              [](const auto &a, const auto &b) { return a.key < b.key; });
    return checkpoint;
}
foundation::Result<void> DeathRewardAdapter::RestoreCheckpoint(DeathRewardCheckpoint checkpoint)
{
    if (!mappings_frozen_ || checkpoint.mapping_revision != mapping_revision_ || checkpoint.deliveries.size() > kDeliveryCapacity)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.checkpoint_invalid", "death reward checkpoint is incompatible with frozen mappings"));
    std::sort(checkpoint.deliveries.begin(), checkpoint.deliveries.end(),
              [](const auto &a, const auto &b) { return a.key < b.key; });
    for (std::size_t i = 0; i < checkpoint.deliveries.size(); ++i)
    {
        const auto &record = checkpoint.deliveries[i];
        if (record.key.combat_sequence == 0 || !record.key.subject.IsValid() || !record.key.table.IsValid() ||
            !record.reward.IsValid() || (i != 0 && checkpoint.deliveries[i - 1].key == record.key))
            return foundation::Result<void>::Failure(
                Error("gameplay.integration.checkpoint_invalid", "death reward checkpoint contains invalid delivery records"));
        if (record.state == DeathRewardDeliveryState::Generated && loot_.FindGenerated(record.reward) == nullptr &&
            loot_.FindPending(record.reward) == nullptr && loot_.ClaimHistoryStatus(record.reward) != loot::RewardClaimHistoryStatus::Claimed)
            return foundation::Result<void>::Failure(
                Error("gameplay.integration.checkpoint_invalid", "death reward generated delivery no longer exists in Loot"));
    }
    cursor_ = checkpoint.cursor;
    deliveries_ = std::move(checkpoint.deliveries);
    return foundation::Result<void>::Success();
}
} // namespace epidemic::gameplay::integration
