#include "Epidemic/GameFramework/Combat/combat.h"

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::combat;

namespace
{
GameplayObjectRef Obj(const char* name)
{
    return {GameplayDomainId::FromString("test"), GameplayObjectId::FromString(name)};
}

DamageRequest Request(GameplayObjectRef attacker, GameplayObjectRef target, DamageTypeId damage,
                      DamageProfileId profile, std::int64_t amount, std::int64_t now)
{
    DamageRequest req;
    req.source = attacker;
    req.instigator = attacker;
    req.target = target;
    req.damage_type = damage;
    req.profile = profile;
    req.base_amount_micro = amount;
    req.seed = {123};
    req.context.time = {now};
    return req;
}
} // namespace

int main()
{
    CombatService s;

    CombatResourceDefinition health;
    health.canonical_name = "game.health";
    health.default_maximum_micro = 100'000'000;
    health.depletion_effect = CombatResourceDepletionEffect::Dead;
    auto hp = s.RegisterResource(health);
    if (!hp)
        return 1;

    CombatResourceDefinition posture;
    posture.canonical_name = "game.posture";
    posture.default_maximum_micro = 10'000'000;
    posture.depletion_effect = CombatResourceDepletionEffect::Downed;
    auto posture_id = s.RegisterResource(posture);
    if (!posture_id)
        return 2;

    auto physical = s.RegisterDamageType("game.damage.physical");
    if (!physical)
        return 3;

    DamageProfile health_profile;
    health_profile.canonical_name = "game.damage.health";
    health_profile.target_resource = hp.Value();
    health_profile.can_miss = false;
    health_profile.can_evade = false;
    health_profile.can_block = false;
    health_profile.can_critical = true;
    health_profile.critical_chance_micro = 1'000'000;
    health_profile.critical_multiplier_micro = 2'000'000;
    auto health_profile_id = s.RegisterDamageProfile(health_profile);
    if (!health_profile_id)
        return 4;

    DamageProfile posture_profile;
    posture_profile.canonical_name = "game.damage.posture";
    posture_profile.target_resource = posture_id.Value();
    posture_profile.can_miss = false;
    posture_profile.can_evade = false;
    posture_profile.can_block = false;
    posture_profile.can_critical = false;
    auto posture_profile_id = s.RegisterDamageProfile(posture_profile);
    if (!posture_profile_id)
        return 5;

    if (!s.SetPreparePolicy({GameplayDuration{5}}))
        return 6;
    s.Freeze();

    const auto attacker = Obj("a");
    const auto target = Obj("b");
    if (!s.RegisterCombatant(target, {{hp.Value(), 100'000'000, 100'000'000}, {posture_id.Value(), 10'000'000, 10'000'000}}))
        return 7;

    auto req = Request(attacker, target, physical.Value(), health_profile_id.Value(), 10'000'000, 10);
    auto plan = s.PrepareDamage(req);
    if (!plan || plan.Value().final_amount_micro != 20'000'000 || plan.Value().prepared_at.ticks != 10 ||
        plan.Value().expires_at.ticks != 15)
        return 8;
    auto result = s.CommitDamage(plan.Value(), {10});
    if (!result || result.Value().resource_after_micro != 80'000'000)
        return 9;

    auto superseded_first = s.PrepareDamage(Request(attacker, target, physical.Value(), health_profile_id.Value(), 10'000'000, 20));
    if (!superseded_first)
        return 10;
    auto superseding_second = s.PrepareDamage(Request(attacker, target, physical.Value(), health_profile_id.Value(), 10'000'000, 20));
    if (!superseding_second)
        return 11;
    auto superseded_commit = s.CommitDamage(superseded_first.Value(), {20});
    if (superseded_commit || !superseded_commit.GetError().HasCode("gameplay.combat.plan_unknown"))
        return 12;
    auto superseding_commit = s.CommitDamage(superseding_second.Value(), {20});
    if (!superseding_commit)
        return 13;

    auto stale = s.PrepareDamage(Request(attacker, target, physical.Value(), health_profile_id.Value(), 10'000'000, 30));
    if (!stale)
        return 14;
    if (!s.ModifyResource(target, hp.Value(), 1'000'000))
        return 15;
    auto rejected = s.CommitDamage(stale.Value(), {30});
    if (rejected || !rejected.GetError().HasCode("gameplay.stale_revision"))
        return 16;

    auto expiring = s.PrepareDamage(Request(attacker, target, physical.Value(), health_profile_id.Value(), 1'000'000, 40));
    if (!expiring)
        return 17;
    auto expired_commit = s.CommitDamage(expiring.Value(), {45});
    if (expired_commit || !expired_commit.GetError().HasCode("gameplay.combat.plan_expired"))
        return 18;

    auto cancellable = s.PrepareDamage(Request(attacker, target, physical.Value(), health_profile_id.Value(), 1'000'000, 50));
    if (!cancellable || !s.CancelDamagePlan(cancellable.Value().id))
        return 19;
    auto cancelled_commit = s.CommitDamage(cancellable.Value(), {50});
    if (cancelled_commit || !cancelled_commit.GetError().HasCode("gameplay.combat.plan_unknown"))
        return 20;

    const auto downed_target = Obj("downed");
    if (!s.RegisterCombatant(downed_target,
                             {{hp.Value(), 100'000'000, 100'000'000}, {posture_id.Value(), 10'000'000, 10'000'000}}))
        return 21;
    auto down_plan = s.PrepareDamage(
        Request(attacker, downed_target, physical.Value(), posture_profile_id.Value(), 20'000'000, 60));
    if (!down_plan)
        return 22;
    auto down_result = s.CommitDamage(down_plan.Value(), {60});
    if (!down_result || down_result.Value().after != CombatLifeState::Downed ||
        !HasOutcome(down_result.Value().outcomes, CombatOutcome::Downed) ||
        HasOutcome(down_result.Value().outcomes, CombatOutcome::Killed))
        return 23;

    if (s.TransitionLifeState(downed_target, CombatLifeState::Alive))
        return 24;
    if (!s.ModifyResource(downed_target, posture_id.Value(), 10'000'000))
        return 25;
    if (!s.TransitionLifeState(downed_target, CombatLifeState::Alive))
        return 26;

    auto kill_plan = s.PrepareDamage(
        Request(attacker, downed_target, physical.Value(), health_profile_id.Value(), 100'000'000, 70));
    if (!kill_plan)
        return 27;
    auto kill_result = s.CommitDamage(kill_plan.Value(), {70});
    if (!kill_result || kill_result.Value().after != CombatLifeState::Dead ||
        !HasOutcome(kill_result.Value().outcomes, CombatOutcome::Killed))
        return 28;
    if (s.TransitionLifeState(downed_target, CombatLifeState::Alive))
        return 29;

    const auto expiry_target = Obj("expiry");
    if (!s.RegisterCombatant(expiry_target, {{hp.Value(), 100'000'000, 100'000'000}}))
        return 30;
    auto abandoned = s.PrepareDamage(
        Request(attacker, expiry_target, physical.Value(), health_profile_id.Value(), 1'000'000, 80));
    if (!abandoned || s.ExpirePreparedPlans({84}) != 0 || s.ExpirePreparedPlans({85}) != 1)
        return 31;

    auto snap = s.CaptureSnapshot();
    CombatService restored;
    auto rr = restored.RegisterResource(health);
    auto rp = restored.RegisterResource(posture);
    auto rd = restored.RegisterDamageType("game.damage.physical");
    auto rh = restored.RegisterDamageProfile(health_profile);
    auto rpost = restored.RegisterDamageProfile(posture_profile);
    if (!rr || !rp || !rd || !rh || !rpost || !restored.SetPreparePolicy({GameplayDuration{5}}))
        return 32;
    restored.Freeze();
    if (!restored.RestoreSnapshot(snap))
        return 33;
    auto state = restored.GetResource(target, hp.Value());
    if (!state || state.Value().current_micro != 61'000'000)
        return 34;

    auto invalid_snap = snap;
    invalid_snap.resolution_ids.scope ^= 0x55u;
    const auto before_invalid = restored.GetResource(target, hp.Value());
    if (restored.RestoreSnapshot(std::move(invalid_snap)))
        return 35;
    const auto after_invalid = restored.GetResource(target, hp.Value());
    if (!before_invalid || !after_invalid ||
        before_invalid.Value().current_micro != after_invalid.Value().current_micro)
        return 36;

    return 0;
}
