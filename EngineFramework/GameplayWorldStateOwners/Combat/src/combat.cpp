#include "Epidemic/GameFramework/Combat/combat.h"
#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <iterator>

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
    if (b > 0 && a > INT64_MAX - b)
        return INT64_MAX;
    if (b < 0 && a < INT64_MIN - b)
        return INT64_MIN;
    return a + b;
}
[[nodiscard]] std::int64_t MulSat(std::int64_t a, std::int64_t b) noexcept
{
    if (a == 0 || b == 0)
        return 0;
    if ((a == -1 && b == INT64_MIN) || (b == -1 && a == INT64_MIN))
        return INT64_MAX;
    if (a > 0)
    {
        if (b > 0 && a > INT64_MAX / b)
            return INT64_MAX;
        if (b < 0 && b < INT64_MIN / a)
            return INT64_MIN;
    }
    else
    {
        if (b > 0 && a < INT64_MIN / b)
            return INT64_MIN;
        if (b < 0 && a < INT64_MAX / b)
            return INT64_MAX;
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
} // namespace
CombatService::CombatService() : resolution_ids_(GameplayObjectId::FromString("framework.combat.resolutions").High())
{
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
        p.critical_chance_micro > 1'000'000)
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
    changes_.push_back(std::move(c));
}
foundation::Result<void> CombatService::RegisterCombatant(GameplayObjectRef subject,
                                                          std::vector<CombatResourceState> states,
                                                          GameplayContext context)
{
    if (!subject.IsValid() || combatants_.contains(subject))
        return foundation::Result<void>::Failure(
            Error("gameplay.combat.combatant_invalid", "invalid or duplicate combatant"));
    for (auto &s : states)
    {
        auto d = resources_.find(s.type);
        if (d == resources_.end())
            return foundation::Result<void>::Failure(Error("gameplay.combat.resource_unknown", "unknown resource"));
        if (s.maximum_micro == 0)
            s.maximum_micro = d->second.default_maximum_micro;
        s.maximum_micro = std::max(s.maximum_micro, d->second.minimum_micro);
        s.current_micro = std::clamp(s.current_micro, d->second.minimum_micro, s.maximum_micro);
    }
    CombatantRecord r{subject, CombatLifeState::Alive, CombatEngagementState::OutOfCombat, std::move(states), {1}};
    combatants_.emplace(subject, r);
    ++diagnostics_.combatants;
    Record({0, CombatChangeKind::CombatantRegistered, subject, {}, {}, r.life_state, r.revision, context});
    return foundation::Result<void>::Success();
}
foundation::Result<void> CombatService::RemoveCombatant(GameplayObjectRef subject, GameplayContext context)
{
    auto it = combatants_.find(subject);
    if (it == combatants_.end())
        return foundation::Result<void>::Failure(Error("gameplay.combat.combatant_missing", "combatant missing"));
    const auto rev = it->second.revision;
    combatants_.erase(it);
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
    auto *r = const_cast<CombatantRecord *>(FindCombatant(subject));
    auto d = resources_.find(type);
    if (!r || d == resources_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.combat.resource_missing", "combatant or resource missing"));
    auto it = std::find_if(r->resources.begin(), r->resources.end(), [&](const auto &s) { return s.type == type; });
    if (it == r->resources.end())
        return foundation::Result<void>::Failure(Error("gameplay.combat.resource_missing", "resource missing"));
    maximum = std::max(maximum, d->second.minimum_micro);
    const auto oldmax = it->maximum_micro, old = it->current_micro;
    it->maximum_micro = maximum;
    if (preserve_ratio && oldmax > 0)
        it->current_micro = MulMicro(maximum, old * 1'000'000 / oldmax);
    else
        it->current_micro = std::min(old, maximum);
    Bump(*r);
    Record({0, CombatChangeKind::ResourceChanged, subject, {}, type, r->life_state, r->revision, context});
    return foundation::Result<void>::Success();
}
foundation::Result<void> CombatService::ModifyResource(GameplayObjectRef subject, CombatResourceTypeId type,
                                                       std::int64_t delta, GameplayContext context)
{
    auto *r = const_cast<CombatantRecord *>(FindCombatant(subject));
    auto d = resources_.find(type);
    if (!r || d == resources_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.combat.resource_missing", "combatant or resource missing"));
    auto it = std::find_if(r->resources.begin(), r->resources.end(), [&](const auto &s) { return s.type == type; });
    if (it == r->resources.end())
        return foundation::Result<void>::Failure(Error("gameplay.combat.resource_missing", "resource missing"));
    it->current_micro = std::clamp(AddSat(it->current_micro, delta), d->second.minimum_micro, it->maximum_micro);
    Bump(*r);
    Record({0, CombatChangeKind::ResourceChanged, subject, {}, type, r->life_state, r->revision, context});
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
    const auto p = damage_profiles_.find(request.profile);
    const auto *t = FindCombatant(request.target);
    if (p == damage_profiles_.end() || !damage_types_.contains(request.damage_type) || !t)
        return foundation::Result<CombatPlan>::Failure(
            Error("gameplay.combat.damage_invalid", "damage request references unknown profile/type/target"));
    auto resource = GetResource(request.target, p->second.target_resource);
    if (!resource)
        return foundation::Result<CombatPlan>::Failure(resource.GetError());
    CombatPlan plan;
    plan.id = {resolution_ids_.Next()};
    plan.request = request;
    plan.resource = p->second.target_resource;
    plan.expected_target_revision = t->revision;
    plan.expected_modifier_revision = modifier_provider_ ? modifier_provider_->RevisionFor(request) : Revision{};
    plan.requested_amount_micro = std::max<std::int64_t>(0, request.base_amount_micro);
    auto amount = plan.requested_amount_micro;
    random::RandomSequence hit(request.seed, random::RandomStream::FromString("combat.hit"));
    random::RandomSequence evade(request.seed, random::RandomStream::FromString("combat.evade"));
    random::RandomSequence block(request.seed, random::RandomStream::FromString("combat.block"));
    random::RandomSequence crit(request.seed, random::RandomStream::FromString("combat.critical"));
    if (p->second.can_miss && !hit.RollMicro(p->second.hit_chance_micro))
    {
        plan.outcomes = plan.outcomes | CombatOutcome::Missed;
        amount = 0;
    }
    else if (p->second.can_evade && evade.RollMicro(p->second.evade_chance_micro))
    {
        plan.outcomes = plan.outcomes | CombatOutcome::Evaded;
        amount = 0;
    }
    else
    {
        if (p->second.can_block && block.RollMicro(p->second.block_chance_micro))
        {
            plan.outcomes = plan.outcomes | CombatOutcome::Blocked;
            amount = MulMicro(amount, p->second.blocked_multiplier_micro);
        }
        amount = std::max<std::int64_t>(0, amount - p->second.flat_mitigation_micro);
        if (modifier_provider_)
            amount = ApplyModifiers(amount, modifier_provider_->Collect(request));
        if (p->second.can_critical && crit.RollMicro(p->second.critical_chance_micro))
        {
            plan.outcomes = plan.outcomes | CombatOutcome::Critical;
            amount = MulMicro(amount, p->second.critical_multiplier_micro);
        }
    }
    plan.final_amount_micro = amount;
    return foundation::Result<CombatPlan>::Success(std::move(plan));
}
foundation::Result<CombatResult> CombatService::CommitDamage(const CombatPlan &plan)
{
    auto *r = const_cast<CombatantRecord *>(FindCombatant(plan.request.target));
    if (!r)
        return foundation::Result<CombatResult>::Failure(
            Error("gameplay.combat.combatant_missing", "target combatant missing"));
    if (r->revision != plan.expected_target_revision ||
        (modifier_provider_ && modifier_provider_->RevisionFor(plan.request) != plan.expected_modifier_revision))
    {
        ++diagnostics_.stale_commits;
        return foundation::Result<CombatResult>::Failure(Error("gameplay.stale_revision", "combat plan is stale"));
    }
    auto d = resources_.find(plan.resource);
    auto it =
        std::find_if(r->resources.begin(), r->resources.end(), [&](const auto &s) { return s.type == plan.resource; });
    if (d == resources_.end() || it == r->resources.end())
        return foundation::Result<CombatResult>::Failure(
            Error("gameplay.combat.resource_missing", "target resource missing"));
    CombatResult out;
    out.id = plan.id;
    out.target = r->subject;
    out.resource = plan.resource;
    out.requested_amount_micro = plan.requested_amount_micro;
    out.final_amount_micro = plan.final_amount_micro;
    out.resource_before_micro = it->current_micro;
    out.before = r->life_state;
    out.outcomes = plan.outcomes;
    out.context = plan.request.context;
    it->current_micro = std::max(d->second.minimum_micro, it->current_micro - plan.final_amount_micro);
    out.resource_after_micro = it->current_micro;
    if (d->second.drives_life_state && it->current_micro <= d->second.minimum_micro &&
        r->life_state == CombatLifeState::Alive)
    {
        r->life_state = CombatLifeState::Dead;
        out.outcomes = out.outcomes | CombatOutcome::Killed;
        ++diagnostics_.deaths;
    }
    out.after = r->life_state;
    Bump(*r);
    ++diagnostics_.damage_resolved;
    if (HasOutcome(out.outcomes, CombatOutcome::Critical))
        ++diagnostics_.criticals;
    if (HasOutcome(out.outcomes, CombatOutcome::Blocked))
        ++diagnostics_.blocks;
    if (HasOutcome(out.outcomes, CombatOutcome::Evaded))
        ++diagnostics_.evades;
    Record({0, CombatChangeKind::DamageResolved, r->subject, out.id, out.resource, r->life_state, r->revision,
            out.context});
    Record({0, CombatChangeKind::ResourceChanged, r->subject, out.id, out.resource, r->life_state, r->revision,
            out.context});
    if (out.before != out.after)
        Record({0, CombatChangeKind::LifeStateChanged, r->subject, out.id, out.resource, r->life_state, r->revision,
                out.context});
    return foundation::Result<CombatResult>::Success(std::move(out));
}
std::vector<CombatChange> CombatService::ChangesSince(std::uint64_t seq) const
{
    std::vector<CombatChange> o;
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(o),
                 [&](const auto &c) { return c.sequence > seq; });
    return o;
}
CombatSnapshot CombatService::CaptureSnapshot() const
{
    CombatSnapshot s;
    s.resolution_ids = resolution_ids_.GetSnapshot();
    for (const auto &[id, r] : combatants_)
    {
        (void)id;
        s.combatants.push_back(r);
    }
    std::sort(s.combatants.begin(), s.combatants.end(), [](auto &a, auto &b) { return a.subject < b.subject; });
    return s;
}
foundation::Result<void> CombatService::RestoreSnapshot(CombatSnapshot s)
{
    combatants_.clear();
    for (auto &r : s.combatants)
    {
        if (!r.subject.IsValid() || combatants_.contains(r.subject))
            return foundation::Result<void>::Failure(
                Error("gameplay.combat.restore_invalid", "invalid combat snapshot"));
        for (const auto &state : r.resources)
            if (!resources_.contains(state.type))
                return foundation::Result<void>::Failure(
                    Error("gameplay.combat.restore_invalid", "unknown resource in snapshot"));
        combatants_.emplace(r.subject, std::move(r));
    }
    resolution_ids_.Restore(s.resolution_ids);
    changes_.clear();
    next_change_sequence_ = 1;
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
