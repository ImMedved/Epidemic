#include "Epidemic/GameFramework/Society/society.h"
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::society;
static GameplayObjectRef Ref(const char *n)
{
    return {GameplayDomainId::FromString("test"), GameplayObjectId::FromString(n)};
}
int main()
{
    SocietyService s;
    auto player = Ref("player");
    auto npc = Ref("npc");
    auto guards = Ref("guards");
    SocialGroupDefinition group;
    group.group = guards;
    group.type = SocialGroupTypeId::FromString("group.faction");
    if (!s.RegisterGroup(group))
        return 1;
    MembershipRecord m;
    m.member = npc;
    m.group = guards;
    m.role = SocialRoleId::FromString("role.guard");
    auto mid = s.AddMembership(m);
    if (!mid)
        return 2;
    if (!s.HasRole(npc, SocialRoleId::FromString("role.guard"), guards))
        return 3;
    RelationshipTypeId trust = RelationshipTypeId::FromString("rel.trust");
    if (!s.ApplySocialChange({npc, player, trust, -500000, TypeId::FromString("test"), {}}))
        return 4;
    auto rel = s.GetRelationship(npc, player, trust);
    if (!rel || rel->state != RelationshipState::Hostile)
        return 5;
    if (s.GetEffectiveAttitude({npc, player, {}}) >= 0)
        return 6;
    ReputationRecord rep;
    rep.subject = player;
    rep.scope = guards;
    rep.track = ReputationTrackId::FromString("rep.crime");
    rep.value_micro = -200000;
    if (!s.SetReputation(rep))
        return 7;
    if (!s.GetReputation(player, guards, rep.track))
        return 8;
    auto standing = s.GetSocialStanding(player, guards);
    if (standing.reputations.size() != 1)
        return 9;
    auto snap = s.CaptureSnapshot();
    SocietyService restored;
    if (!restored.RestoreSnapshot(std::move(snap)))
        return 10;
    if (!restored.HasRole(npc, SocialRoleId::FromString("role.guard"), guards))
        return 11;
    if (restored.GetEffectiveAttitude({npc, player, {}}) >= 0)
        return 12;
    return 0;
}
