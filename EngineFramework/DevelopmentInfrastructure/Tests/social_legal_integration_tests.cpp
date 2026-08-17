#include "Epidemic/GameFramework/SocialLegalIntegration/social_legal_adapters.h"
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::ownership;
using namespace epidemic::gameplay::crime;
using namespace epidemic::gameplay::society;
using namespace epidemic::gameplay::social_legal_integration;
static GameplayObjectRef Ref(const char *n)
{
    return {GameplayDomainId::FromString("test"), GameplayObjectId::FromString(n)};
}
int main()
{
    OwnershipService own;
    CrimeService crime;
    SocietyService society;
    auto player = Ref("player");
    auto npc = Ref("npc");
    auto apple = Ref("apple");
    auto city = Ref("city");
    auto guards = Ref("guards");
    auto take = PropertyRightId::FromString("right.take");
    OwnershipRecord r;
    r.property = apple;
    r.owner = npc;
    if (!own.AssignOwnership(r))
        return 1;
    LawDefinition law;
    law.id = LawId::FromString("law.theft");
    law.crime_type = CrimeTypeId::FromString("crime.theft");
    law.severity_micro = 200000;
    if (!crime.RegisterLaw(law))
        return 2;
    JurisdictionRecord j;
    j.id = JurisdictionId::FromString("jur.city");
    j.area = city;
    j.authority_group = guards;
    j.active_laws.push_back(law.id);
    if (!crime.RegisterJurisdiction(j))
        return 3;
    OwnershipCrimeAdapter oc(own, crime);
    auto made = oc.TryCreateCrimeFromDeniedRight(player, apple, take, law.crime_type, city, {});
    if (!made || !made.Value().crime)
        return 4;
    CrimeSocietyAdapter cs(crime, society);
    RelationshipTypeId trust = RelationshipTypeId::FromString("rel.trust");
    if (!cs.ApplyRelationshipPenalty(*made.Value().crime, trust, -700000, {}))
        return 5;
    if (society.GetEffectiveAttitude({npc, player, {}}) >= 0)
        return 6;
    auto response = cs.GenerateAuthorityIntentLikeResponse(
        *made.Value().crime, LawResponseTypeId::FromString("response.arrest"), guards, player, {});
    if (!response)
        return 7;
    if (crime.FindCrime(*made.Value().crime)->state != CrimeCaseState::Wanted)
        return 8;
    auto os = own.CaptureSnapshot();
    auto ss = society.CaptureSnapshot();
    auto csnap = crime.CaptureSnapshot();
    OwnershipService own2;
    SocietyService soc2;
    CrimeService crime2;
    if (!own2.RestoreSnapshot(std::move(os)))
        return 9;
    if (!soc2.RestoreSnapshot(std::move(ss)))
        return 10;
    if (!crime2.RestoreSnapshot(std::move(csnap)))
        return 11;
    if (crime2.FindCrimesByOffender(player).size() != 1)
        return 12;
    return 0;
}
