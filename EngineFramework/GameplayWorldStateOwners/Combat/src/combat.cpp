#include "Epidemic/GameFramework/Combat/combat.h"
#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_set>

namespace epidemic::gameplay::combat
{
namespace
{
[[nodiscard]] foundation::Error Error(std::string code, std::string message)
{
    return foundation::Error::Create(std::move(code), std::move(message));
}
[[nodiscard]] std::int64_t AddSat(std::int64_t a, std::int64_t b) noexcept
{
    if (b > 0 && a > std::numeric_limits<std::int64_t>::max() - b)
        return std::numeric_limits<std::int64_t>::max();
    if (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)
        return std::numeric_limits<std::int64_t>::min();
    return a + b;
}
[[nodiscard]] std::int64_t MulSat(std::int64_t a, std::int64_t b) noexcept
{
    constexpr auto kMax = std::numeric_limits<std::int64_t>::max();
    constexpr auto kMin = std::numeric_limits<std::int64_t>::min();
    if (a == 0 || b == 0)
        return 0;
    if ((a == -1 && b == kMin) || (b == -1 && a == kMin))
        return kMax;
    if (a > 0)
    {
        if (b > 0 && a > kMax / b)
            return kMax;
        if (b < 0 && b < kMin / a)
            return kMin;
    }
    else
    {
        if (b > 0 && a < kMin / b)
            return kMin;
        if (b < 0 && a < kMax / b)
            return kMax;
    }
    return a * b;
}
[[nodiscard]] std::int64_t MulMicro(std::int64_t a, std::int64_t b) noexcept
{
    constexpr std::int64_t s = 1'000'000;
    const auto aq = a / s, ar = a % s, bq = b / s, br = b % s;
    auto r = MulSat(MulSat(aq, bq), s);
    r = AddSat(r, MulSat(aq, br));
    r = AddSat(r, MulSat(ar, bq));
    return AddSat(r, MulSat(ar, br) / s);
}
[[nodiscard]] std::int64_t ScaleRatioSat(std::int64_t value, std::int64_t new_max, std::int64_t old_max) noexcept
{
    if (old_max == 0)
        return 0;
    const long double scaled = static_cast<long double>(value) * static_cast<long double>(new_max) /
                               static_cast<long double>(old_max);
    if (scaled >= static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
        return std::numeric_limits<std::int64_t>::max();
    if (scaled <= static_cast<long double>(std::numeric_limits<std::int64_t>::min()))
        return std::numeric_limits<std::int64_t>::min();
    return static_cast<std::int64_t>(scaled);
}
[[nodiscard]] bool IsTerminalLifeState(CombatLifeState state) noexcept
{
    return state == CombatLifeState::Dead || state == CombatLifeState::Disabled;
}
} // namespace
CombatService::CombatService() : resolution_ids_(GameplayObjectId::FromString("framework.combat.resolutions").High())
{
}
void CombatService::SetModifierProvider(const ICombatModifierProvider *provider) noexcept
{
    if (modifier_provider_ == provider)
        return;
    modifier_provider_ = provider;
    if (++modifier_provider_epoch_ == 0)
        modifier_provider_epoch_ = 1;
}
foundation::Result<CombatResourceTypeId> CombatService::RegisterResource(CombatResourceDefinition d)
{
    if (frozen_)
        return foundation::Result<CombatResourceTypeId>::Failure(
            Error("gameplay.registry_frozen", "combat registry frozen"));
    if (d.canonical_name.empty())
        return foundation::Result<CombatResourceTypeId>::Failure(
            Error("gameplay.combat.resource_invalid", "resource name required"));
    const auto e = CombatResourceTypeId::FromString(d.canonical_name);
    if (!d.id.IsValid())
        d.id = e;
    if (d.id != e || d.minimum_micro > d.default_maximum_micro || resources_.contains(d.id))
        return foundation::Result<CombatResourceTypeId>::Failure(
            Error("gameplay.combat.resource_invalid", "invalid or duplicate resource"));
    const auto id = d.id;
    resources_.emplace(id, std::move(d));
    return foundation::Result<CombatResourceTypeId>::Success(id);
}
foundation::Result<DamageTypeId> CombatService::RegisterDamageType(std::string name)
{
    if (frozen_)
        return foundation::Result<DamageTypeId>::Failure(Error("gameplay.registry_frozen", "combat registry frozen"));
    const auto id = DamageTypeId::FromString(name);
    if (name.empty() || damage_types_.contains(id))
        return foundation::Result<DamageTypeId>::Failure(
            Error("gameplay.combat.damage_type_invalid", "invalid or duplicate damage type"));
    damage_types_[id] = std::move(name);
    return foundation::Result<DamageTypeId>::Success(id);
}
foundation::Result<DamageProfileId> CombatService::RegisterDamageProfile(DamageProfile p)
{
    if (frozen_)
        return foundation::Result<DamageProfileId>::Failure(
            Error("gameplay.registry_frozen", "combat registry frozen"));
    if (p.canonical_name.empty())
        return foundation::Result<DamageProfileId>::Failure(
            Error("gameplay.combat.profile_invalid", "profile name required"));
    const auto e = DamageProfileId::FromString(p.canonical_name);
    if (!p.id.IsValid())
        p.id = e;
    if (p.id != e || !resources_.contains(p.target_resource) || damage_profiles_.contains(p.id) ||
        p.hit_chance_micro > 1'000'000 || p.evade_chance_micro > 1'000'000 || p.block_chance_micro > 1'000'000 ||
        p.critical_chance_micro > 1'000'000 || p.blocked_multiplier_micro < 0 ||
        p.critical_multiplier_micro < 0 || p.flat_mitigation_micro < 0)
        return foundation::Result<DamageProfileId>::Failure(
            Error("gameplay.combat.profile_invalid", "invalid or duplicate damage profile"));
    const auto id = p.id;
    damage_profiles_.emplace(id, std::move(p));
    return foundation::Result<DamageProfileId>::Success(id);
}
void CombatService::Bump(CombatantRecord &r) noexcept
{
    ++r.revision.value;
}
void CombatService::Record(CombatChange c)
{
    c.sequence = next_change_sequence_++;
    if (changes_.size() == kChangeJournalCapacity)
        changes_.pop_front();
    changes_.push_back(std::move(c));
}
bool CombatService::ReconcileLifeState(CombatantRecord &record, CombatResourceTypeId changed_resource) noexcept
{
    if (IsTerminalLifeState(record.life_state) || record.life_state == CombatLifeState::Downed)
        return false;
    if (changed_resource.IsValid())
    {
        auto def = resources_.find(changed_resource);
        if (def == resources_.end() || !def->second.drives_life_state)
            return false;
    }
    for (const auto &state : record.resources)
    {
        auto def = resources_.find(state.type);
        if (def != resources_.end() && def->second.drives_life_state && state.current_micro <= def->second.minimum_micro)
        {
            record.life_state = CombatLifeState::Dead;
            return true;
        }
    }
    return false;
}
foundation::Result<void> CombatService::RegisterCombatant(GameplayObjectRef subject,
                                                          std::vector<CombatResourceState> states,
                                                          GameplayContext context)
{
    if (!subject.IsValid() || combatants_.contains(subject))
        return foundation::Result<void>::Failure(
            Error("gameplay.combat.combatant_invalid", "invalid or duplicate combatant"));
    std::unordered_set<CombatResourceTypeId, IdHash> seen;
    for (auto &state : states)
    {
        auto def = resources_.find(state.type);
        if (def == resources_.end() || !seen.insert(state.type).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.combat.resource_unknown", "unknown or duplicate combat resource"));
        if (state.maximum_micro == 0)
            state.maximum_micro = def->second.default_maximum_micro;
        state.maximum_micro = std::max(state.maximum_micro, def->second.minimum_micro);
        state.current_micro = std::clamp(state.current_micro, def->second.minimum_micro, state.maximum_micro);
    }
    CombatantRecord record{subject, CombatLifeState::Alive, CombatEngagementState::OutOfCombat, std::move(states), {1}};
    const bool life_changed = ReconcileLifeState(record, {});
    combatants_.emplace(subject, record);
    ++diagnostics_.combatants;
    if (life_changed)
        ++diagnostics_.deaths;
    Record({0, CombatChangeKind::CombatantRegistered, subject, {}, {}, record.life_state, record.revision, context});
    if (life_changed)
        Record({0, CombatChangeKind::LifeStateChanged, subject, {}, {}, record.life_state, record.revision, context});
    return foundation::Result<void>::Success();
}
foundation::Result<void> CombatService::RemoveCombatant(GameplayObjectRef subject, GameplayContext context)
{
    auto it = combatants_.find(subject);
    if (it == combatants_.end())
        return foundation::Result<void>::Failure(Error("gameplay.combat.combatant_missing", "combatant missing"));
    const auto rev = it->second.revision;
    combatants_.erase(it);
    std::erase_if(prepared_plans_, [&](const auto &entry) { return entry.second.request.target == subject; });
    --diagnostics_.combatants;
    Record({0, CombatChangeKind::CombatantRemoved, subject, {}, {}, CombatLifeState::Disabled, rev, context});
    return foundation::Result<void>::Success();
}
const CombatantRecord *CombatService::FindCombatant(GameplayObjectRef subject) const noexcept
{
    auto it = combatants_.find(subject);
    return it == combatants_.end() ? nullptr : &it->second;
}
foundation::Result<CombatResourceState> CombatService::GetResource(GameplayObjectRef subject,
                                                                   CombatResourceTypeId type) const
{
    const auto *r = FindCombatant(subject);
    if (!r)
        return foundation::Result<CombatResourceState>::Failure(
            Error("gameplay.combat.combatant_missing", "combatant missing"));
    auto it = std::find_if(r->resources.begin(), r->resources.end(), [&](const auto &s) { return s.type == type; });
    if (it == r->resources.end())
        return foundation::Result<CombatResourceState>::Failure(
            Error("gameplay.combat.resource_missing", "combat resource missing"));
    return foundation::Result<CombatResourceState>::Success(*it);
}
foundation::Result<void> CombatService::SetResourceMaximum(GameplayObjectRef subject, CombatResourceTypeId type,
                                                           std::int64_t maximum, bool preserve_ratio,
                                                           GameplayContext context)
{
    auto *record = const_cast<CombatantRecord *>(FindCombatant(subject));
    auto def = resources_.find(type);
    if (!record || def == resources_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.combat.resource_missing", "combatant or resource missing"));
    auto state = std::find_if(record->resources.begin(), record->resources.end(), [&](const auto &s) { return s.type == type; });
    if (state == record->resources.end())
        return foundation::Result<void>::Failure(Error("gameplay.combat.resource_missing", "resource missing"));
    maximum = std::max(maximum, def->second.minimum_micro);
    const auto old_max = state->maximum_micro;
    const auto old = state->current_micro;
    state->maximum_micro = maximum;
    if (preserve_ratio && old_max != 0)
        state->current_micro = std::clamp(ScaleRatioSat(old, maximum, old_max), def->second.minimum_micro, maximum);
    else
        state->current_micro = std::clamp(old, def->second.minimum_micro, maximum);
    const auto before = record->life_state;
    const bool life_changed = ReconcileLifeState(*record, type);
    Bump(*record);
    Record({0, CombatChangeKind::ResourceChanged, subject, {}, type, record->life_state, record->revision, context});
    if (life_changed && before != record->life_state)
    {
        ++diagnostics_.deaths;
        Record({0, CombatChangeKind::LifeStateChanged, subject, {}, type, record->life_state, record->revision, context});
    }
    return foundation::Result<void>::Success();
}
foundation::Result<void> CombatService::ModifyResource(GameplayObjectRef subject, CombatResourceTypeId type,
                                                       std::int64_t delta, GameplayContext context)
{
    auto *record = const_cast<CombatantRecord *>(FindCombatant(subject));
    auto def = resources_.find(type);
    if (!record || def == resources_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.combat.resource_missing", "combatant or resource missing"));
    auto state = std::find_if(record->resources.begin(), record->resources.end(), [&](const auto &s) { return s.type == type; });
    if (state == record->resources.end())
        return foundation::Result<void>::Failure(Error("gameplay.combat.resource_missing", "resource missing"));
    state->current_micro = std::clamp(AddSat(state->current_micro, delta), def->second.minimum_micro, state->maximum_micro);
    const auto before = record->life_state;
    const bool life_changed = ReconcileLifeState(*record, type);
    Bump(*record);
    Record({0, CombatChangeKind::ResourceChanged, subject, {}, type, record->life_state, record->revision, context});
    if (life_changed && before != record->life_state)
    {
        ++diagnostics_.deaths;
        Record({0, CombatChangeKind::LifeStateChanged, subject, {}, type, record->life_state, record->revision, context});
    }
    return foundation::Result<void>::Success();
}
foundation::Result<void> CombatService::SetEngagement(GameplayObjectRef subject, CombatEngagementState state,
                                                      GameplayContext context)
{
    auto *r = const_cast<CombatantRecord *>(FindCombatant(subject));
    if (!r)
        return foundation::Result<void>::Failure(Error("gameplay.combat.combatant_missing", "combatant missing"));
    if (r->engagement == state)
        return foundation::Result<void>::Success();
    r->engagement = state;
    Bump(*r);
    Record({0, CombatChangeKind::EngagementChanged, subject, {}, {}, r->life_state, r->revision, context});
    return foundation::Result<void>::Success();
}
std::int64_t CombatService::ApplyModifiers(std::int64_t amount, std::vector<CombatModifier> mods) noexcept
{
    std::sort(mods.begin(), mods.end(), [](const auto &a, const auto &b) {
        if (a.phase != b.phase)
            return a.phase < b.phase;
        if (a.priority != b.priority)
            return a.priority < b.priority;
        if (a.type != b.type)
            return a.type < b.type;
        return a.source < b.source;
    });
    for (const auto &m : mods)
    {
        switch (m.operation)
        {
        case CombatModifierOperation::Add:
            amount = AddSat(amount, m.value_micro);
            break;
        case CombatModifierOperation::Multiply:
            amount = MulMicro(amount, m.value_micro);
            break;
        case CombatModifierOperation::Override:
            amount = m.value_micro;
            break;
        case CombatModifierOperation::ClampMin:
            amount = std::max(amount, m.value_micro);
            break;
        case CombatModifierOperation::ClampMax:
            amount = std::min(amount, m.value_micro);
            break;
        }
    }
    return std::max<std::int64_t>(0, amount);
}
foundation::Result<CombatPlan> CombatService::PrepareDamage(DamageRequest request)
{
    ++diagnostics_.damage_requests;
    if (prepared_plans_.size() >= kPreparedPlanCapacity)
        return foundation::Result<CombatPlan>::Failure(
            Error("gameplay.combat.too_many_prepared", "too many uncommitted combat plans"));
    const auto profile = damage_profiles_.find(request.profile);
    const auto *target = FindCombatant(request.target);
    if (profile == damage_profiles_.end() || !damage_types_.contains(request.damage_type) || !target)
        return foundation::Result<CombatPlan>::Failure(
            Error("gameplay.combat.damage_invalid", "damage request references unknown profile/type/target"));
    auto resource = GetResource(request.target, profile->second.target_resource);
    if (!resource)
        return foundation::Result<CombatPlan>::Failure(resource.GetError());
    CombatPlan plan;
    plan.id = {resolution_ids_.Next()};
    plan.request = std::move(request);
    plan.resource = profile->second.target_resource;
    plan.expected_target_revision = target->revision;
    plan.expected_modifier_revision = modifier_provider_ ? modifier_provider_->RevisionFor(plan.request) : Revision{};
    plan.expected_provider_epoch = modifier_provider_epoch_;
    plan.requested_amount_micro = std::max<std::int64_t>(0, plan.request.base_amount_micro);
    auto amount = plan.requested_amount_micro;
    random::RandomSequence hit(plan.request.seed, random::RandomStream::FromString("combat.hit"));
    random::RandomSequence evade(plan.request.seed, random::RandomStream::FromString("combat.evade"));
    random::RandomSequence block(plan.request.seed, random::RandomStream::FromString("combat.block"));
    random::RandomSequence crit(plan.request.seed, random::RandomStream::FromString("combat.critical"));
    if (profile->second.can_miss && !hit.RollMicro(profile->second.hit_chance_micro))
    {
        plan.outcomes = plan.outcomes | CombatOutcome::Missed;
        amount = 0;
    }
    else if (profile->second.can_evade && evade.RollMicro(profile->second.evade_chance_micro))
    {
        plan.outcomes = plan.outcomes | CombatOutcome::Evaded;
        amount = 0;
    }
    else
    {
        if (profile->second.can_block && block.RollMicro(profile->second.block_chance_micro))
        {
            plan.outcomes = plan.outcomes | CombatOutcome::Blocked;
            amount = MulMicro(amount, profile->second.blocked_multiplier_micro);
        }
        amount = amount <= profile->second.flat_mitigation_micro ? 0 : amount - profile->second.flat_mitigation_micro;
        if (modifier_provider_)
            amount = ApplyModifiers(amount, modifier_provider_->Collect(plan.request));
        if (profile->second.can_critical && crit.RollMicro(profile->second.critical_chance_micro))
        {
            plan.outcomes = plan.outcomes | CombatOutcome::Critical;
            amount = MulMicro(amount, profile->second.critical_multiplier_micro);
        }
    }
    plan.final_amount_micro = std::max<std::int64_t>(0, amount);
    prepared_plans_.emplace(plan.id, plan);
    return foundation::Result<CombatPlan>::Success(plan);
}
foundation::Result<CombatResult> CombatService::CommitDamage(const CombatPlan &external_plan)
{
    auto prepared = prepared_plans_.find(external_plan.id);
    if (prepared == prepared_plans_.end())
        return foundation::Result<CombatResult>::Failure(
            Error("gameplay.combat.plan_unknown", "combat plan was not prepared by this service or was already consumed"));
    const CombatPlan plan = prepared->second;
    auto *record = const_cast<CombatantRecord *>(FindCombatant(plan.request.target));
    if (!record)
    {
        prepared_plans_.erase(prepared);
        return foundation::Result<CombatResult>::Failure(
            Error("gameplay.combat.combatant_missing", "target combatant missing"));
    }
    const bool provider_stale = plan.expected_provider_epoch != modifier_provider_epoch_ ||
        ((plan.expected_modifier_revision.value != 0 || modifier_provider_ != nullptr) &&
         (!modifier_provider_ || modifier_provider_->RevisionFor(plan.request) != plan.expected_modifier_revision));
    if (record->revision != plan.expected_target_revision || provider_stale)
    {
        prepared_plans_.erase(prepared);
        ++diagnostics_.stale_commits;
        return foundation::Result<CombatResult>::Failure(Error("gameplay.stale_revision", "combat plan is stale"));
    }
    auto def = resources_.find(plan.resource);
    auto state = std::find_if(record->resources.begin(), record->resources.end(), [&](const auto &s) { return s.type == plan.resource; });
    if (def == resources_.end() || state == record->resources.end())
    {
        prepared_plans_.erase(prepared);
        return foundation::Result<CombatResult>::Failure(
            Error("gameplay.combat.resource_missing", "target resource missing"));
    }
    CombatResult out;
    out.id = plan.id;
    out.target = record->subject;
    out.resource = plan.resource;
    out.requested_amount_micro = plan.requested_amount_micro;
    out.final_amount_micro = plan.final_amount_micro;
    out.resource_before_micro = state->current_micro;
    out.before = record->life_state;
    out.outcomes = plan.outcomes;
    out.context = plan.request.context;
    state->current_micro = std::max(def->second.minimum_micro, AddSat(state->current_micro, -plan.final_amount_micro));
    out.resource_after_micro = state->current_micro;
    const bool life_changed = ReconcileLifeState(*record, plan.resource);
    if (life_changed && out.before != record->life_state)
    {
        out.outcomes = out.outcomes | CombatOutcome::Killed;
        ++diagnostics_.deaths;
    }
    out.after = record->life_state;
    Bump(*record);
    prepared_plans_.erase(prepared);
    ++diagnostics_.damage_resolved;
    if (HasOutcome(out.outcomes, CombatOutcome::Critical))
        ++diagnostics_.criticals;
    if (HasOutcome(out.outcomes, CombatOutcome::Blocked))
        ++diagnostics_.blocks;
    if (HasOutcome(out.outcomes, CombatOutcome::Evaded))
        ++diagnostics_.evades;
    Record({0, CombatChangeKind::DamageResolved, record->subject, out.id, out.resource, record->life_state, record->revision,
            out.context});
    Record({0, CombatChangeKind::ResourceChanged, record->subject, out.id, out.resource, record->life_state, record->revision,
            out.context});
    if (out.before != out.after)
        Record({0, CombatChangeKind::LifeStateChanged, record->subject, out.id, out.resource, record->life_state, record->revision,
                out.context});
    return foundation::Result<CombatResult>::Success(std::move(out));
}
std::vector<CombatChange> CombatService::ChangesSince(std::uint64_t sequence) const
{
    return ReadChangesSince(sequence).changes;
}
CombatChangeBatch CombatService::ReadChangesSince(std::uint64_t sequence) const
{
    CombatChangeBatch batch;
    batch.oldest_available_sequence = changes_.empty() ? next_change_sequence_ : changes_.front().sequence;
    batch.snapshot_required = batch.oldest_available_sequence > 0 && sequence < batch.oldest_available_sequence - 1;
    if (batch.snapshot_required)
        return batch;
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(batch.changes),
                 [&](const auto &change) { return change.sequence > sequence; });
    return batch;
}
CombatSnapshot CombatService::CaptureSnapshot() const
{
    CombatSnapshot snapshot;
    snapshot.resolution_ids = resolution_ids_.GetSnapshot();
    snapshot.next_change_sequence = next_change_sequence_;
    snapshot.journal.assign(changes_.begin(), changes_.end());
    for (const auto &[id, record] : combatants_)
    {
        (void)id;
        snapshot.combatants.push_back(record);
    }
    std::sort(snapshot.combatants.begin(), snapshot.combatants.end(), [](const auto &a, const auto &b) { return a.subject < b.subject; });
    return snapshot;
}
foundation::Result<void> CombatService::RestoreSnapshot(CombatSnapshot snapshot)
{
    std::unordered_map<GameplayObjectRef, CombatantRecord> restored;
    restored.reserve(snapshot.combatants.size());
    for (auto &record : snapshot.combatants)
    {
        if (!record.subject.IsValid() || restored.contains(record.subject) || record.revision.value == 0)
            return foundation::Result<void>::Failure(
                Error("gameplay.combat.restore_invalid", "invalid or duplicate combatant in snapshot"));
        std::unordered_set<CombatResourceTypeId, IdHash> seen;
        bool driver_depleted = false;
        for (const auto &state : record.resources)
        {
            auto def = resources_.find(state.type);
            if (def == resources_.end() || !seen.insert(state.type).second || state.maximum_micro < def->second.minimum_micro ||
                state.current_micro < def->second.minimum_micro || state.current_micro > state.maximum_micro)
                return foundation::Result<void>::Failure(
                    Error("gameplay.combat.restore_invalid", "invalid combat resource in snapshot"));
            driver_depleted = driver_depleted || (def->second.drives_life_state && state.current_micro <= def->second.minimum_micro);
        }
        if (driver_depleted && record.life_state == CombatLifeState::Alive)
            return foundation::Result<void>::Failure(
                Error("gameplay.combat.restore_invalid", "alive combatant has depleted life-driving resource"));
        restored.emplace(record.subject, std::move(record));
    }
    std::deque<CombatChange> journal;
    std::uint64_t previous = 0;
    if (snapshot.journal.size() > kChangeJournalCapacity)
        return foundation::Result<void>::Failure(Error("gameplay.combat.restore_invalid", "combat journal exceeds retention"));
    for (const auto &change : snapshot.journal)
    {
        if (change.sequence == 0 || change.sequence <= previous)
            return foundation::Result<void>::Failure(Error("gameplay.combat.restore_invalid", "invalid combat journal sequence"));
        previous = change.sequence;
        journal.push_back(change);
    }
    if (snapshot.next_change_sequence == 0 || (!journal.empty() && snapshot.next_change_sequence <= journal.back().sequence))
        return foundation::Result<void>::Failure(Error("gameplay.combat.restore_invalid", "invalid next combat change sequence"));

    combatants_.swap(restored);
    changes_.swap(journal);
    resolution_ids_.Restore(snapshot.resolution_ids);
    next_change_sequence_ = snapshot.next_change_sequence;
    prepared_plans_.clear();
    diagnostics_.combatants = combatants_.size();
    return foundation::Result<void>::Success();
}
CombatDiagnostics CombatService::GetDiagnostics() const noexcept
{
    auto d = diagnostics_;
    d.combatants = combatants_.size();
    return d;
}
} // namespace epidemic::gameplay::combat
