#include "Epidemic/GameFramework/Progression/progression.h"
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::progression;
int main()
{
    ProgressionService s;
    AttributeDefinition strength;
    strength.canonical_name = "game.strength";
    strength.default_micro = 1'000'000;
    strength.min_micro = 0;
    strength.max_micro = 100'000'000;
    auto sid = s.RegisterAttribute(strength);
    if (!sid)
        return 1;
    AttributeDefinition power;
    power.canonical_name = "game.power";
    power.min_micro = 0;
    power.max_micro = 500'000'000;
    power.derived_terms.push_back({sid.Value(), 2'000'000});
    auto pid = s.RegisterAttribute(power);
    if (!pid)
        return 2;
    ProgressionTrackDefinition level;
    level.canonical_name = "game.level";
    level.rank_thresholds_micro = {100, 300};
    auto tid = s.RegisterTrack(level);
    if (!tid)
        return 3;
    PerkDefinition perk;
    perk.canonical_name = "game.perk.a";
    auto perkid = s.RegisterPerk(perk);
    if (!perkid || !s.Freeze())
        return 4;
    GameplayObjectRef actor{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("actor")};
    if (!s.EnsureProfile(actor) || !s.SetBaseAttribute(actor, sid.Value(), 10'000'000))
        return 5;
    auto value = s.GetAttribute(actor, pid.Value());
    if (!value || value.Value() != 20'000'000)
        return 6;
    ProgressionModifier m;
    m.target = pid.Value();
    m.operation = ModifierOperation::FinalAdd;
    m.value_micro = 5'000'000;
    m.modifier_type = TypeId::FromString("test.mod");
    auto mid = s.AddModifier(actor, m);
    if (!mid)
        return 7;
    value = s.GetAttribute(actor, pid.Value());
    if (!value || value.Value() != 25'000'000)
        return 8;
    auto track = s.GrantProgress(actor, tid.Value(), 150);
    if (!track || track.Value().rank != 1)
        return 9;
    if (!s.GrantPerk(actor, perkid.Value()) || !s.HasPerk(actor, perkid.Value()))
        return 10;
    auto snap = s.CaptureSnapshot();
    ProgressionService restored;
    auto rs = restored.RegisterAttribute(strength);
    auto rp = restored.RegisterAttribute(power);
    auto rt = restored.RegisterTrack(level);
    auto rk = restored.RegisterPerk(perk);
    if (!rs || !rp || !rt || !rk || !restored.Freeze() || !restored.RestoreSnapshot(std::move(snap)))
        return 11;
    auto rv = restored.GetAttribute(actor, pid.Value());
    if (!rv || rv.Value() != 25'000'000)
        return 12;
    return 0;
}
