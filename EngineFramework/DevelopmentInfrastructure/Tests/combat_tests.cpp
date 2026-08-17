#include "Epidemic/GameFramework/Combat/combat.h"
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::combat;
int main()
{
    CombatService s;
    CombatResourceDefinition health;
    health.canonical_name = "game.health";
    health.default_maximum_micro = 100'000'000;
    health.drives_life_state = true;
    auto hp = s.RegisterResource(health);
    if (!hp)
        return 1;
    auto physical = s.RegisterDamageType("game.damage.physical");
    if (!physical)
        return 2;
    DamageProfile p;
    p.canonical_name = "game.damage.basic";
    p.target_resource = hp.Value();
    p.can_miss = false;
    p.can_evade = false;
    p.can_block = false;
    p.can_critical = true;
    p.critical_chance_micro = 1'000'000;
    p.critical_multiplier_micro = 2'000'000;
    auto profile = s.RegisterDamageProfile(p);
    if (!profile)
        return 3;
    s.Freeze();
    GameplayObjectRef attacker{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("a")},
        target{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("b")};
    if (!s.RegisterCombatant(target, {{hp.Value(), 100'000'000, 100'000'000}}))
        return 4;
    DamageRequest req;
    req.source = attacker;
    req.instigator = attacker;
    req.target = target;
    req.damage_type = physical.Value();
    req.profile = profile.Value();
    req.base_amount_micro = 10'000'000;
    req.seed = {123};
    auto plan = s.PrepareDamage(req);
    if (!plan || plan.Value().final_amount_micro != 20'000'000)
        return 5;
    auto result = s.CommitDamage(plan.Value());
    if (!result || result.Value().resource_after_micro != 80'000'000)
        return 6;
    auto stale = s.PrepareDamage(req);
    if (!stale)
        return 7;
    if (!s.ModifyResource(target, hp.Value(), 1'000'000))
        return 8;
    auto rejected = s.CommitDamage(stale.Value());
    if (rejected || !rejected.GetError().HasCode("gameplay.stale_revision"))
        return 9;
    auto snap = s.CaptureSnapshot();
    CombatService restored;
    auto rr = restored.RegisterResource(health);
    auto rd = restored.RegisterDamageType("game.damage.physical");
    auto rdp = restored.RegisterDamageProfile(p);
    restored.Freeze();
    if (!rr || !rd || !rdp || !restored.RestoreSnapshot(std::move(snap)))
        return 10;
    auto state = restored.GetResource(target, hp.Value());
    if (!state || state.Value().current_micro != 81'000'000)
        return 11;
    return 0;
}
