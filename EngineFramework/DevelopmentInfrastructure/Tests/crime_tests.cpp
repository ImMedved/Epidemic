#include "Epidemic/GameFramework/Crime/crime.h"
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::crime;
static GameplayObjectRef Ref(const char *n)
{
    return {GameplayDomainId::FromString("test"), GameplayObjectId::FromString(n)};
}
int main()
{
    CrimeService c;
    auto player = Ref("player");
    auto npc = Ref("npc");
    auto city = Ref("city");
    auto guards = Ref("guards");
    CrimeTypeId theft = CrimeTypeId::FromString("crime.theft");
    LawDefinition law;
    law.id = LawId::FromString("law.theft");
    law.crime_type = theft;
    law.severity_micro = 300000;
    law.default_bounty = 50;
    if (!c.RegisterLaw(law))
        return 1;
    JurisdictionRecord j;
    j.id = JurisdictionId::FromString("jur.city");
    j.area = city;
    j.authority_group = guards;
    j.active_laws.push_back(law.id);
    j.priority = 10;
    if (!c.RegisterJurisdiction(j))
        return 2;
    AuthorityRecord a;
    a.authority_group = guards;
    a.jurisdiction = j.id;
    a.capabilities = 1;
    auto aid = c.RegisterAuthority(a);
    if (!aid)
        return 3;
    auto preview = c.PreviewLegality(player, theft, city);
    if (!preview.illegal)
        return 4;
    auto eval = c.EvaluateCrimeCandidate({theft, player, npc, Ref("apple"), city, {100}, {}, {}},
                                         CrimeCandidatePolicy::RecordAlways);
    if (!eval || !eval.Value().crime)
        return 5;
    auto crime_id = *eval.Value().crime;
    WitnessRecord w;
    w.crime = crime_id;
    w.witness = npc;
    w.witness_type = WitnessTypeId::FromString("witness.visual");
    w.confidence_micro = 900000;
    auto wid = c.AddWitness(w);
    if (!wid)
        return 6;
    if (c.FindWitnesses(crime_id).size() != 1)
        return 7;
    if (c.FindCrime(crime_id)->proof_state != CrimeProofState::Witnessed)
        return 8;
    EvidenceRecord e;
    e.crime = crime_id;
    e.source = Ref("stolen_item");
    e.type = EvidenceTypeId::FromString("evidence.possession");
    e.strength_micro = 900000;
    if (!c.AddEvidence(e))
        return 9;
    if (c.FindCrime(crime_id)->proof_state != CrimeProofState::Confirmed)
        return 10;
    BountyRecord b;
    b.offender = player;
    b.jurisdiction = j.id;
    b.amount = 100;
    auto bid = c.CreateBounty(b);
    if (!bid)
        return 11;
    if (!c.GetBounty(player, j.id))
        return 12;
    auto response =
        c.GenerateLawResponse({crime_id, LawResponseTypeId::FromString("law_response.arrest"), guards, player, {}});
    if (!response)
        return 13;
    if (c.FindCrime(crime_id)->state != CrimeCaseState::Wanted)
        return 14;
    if (!c.ExpireCrime(crime_id))
        return 15;
    if (c.FindCrime(crime_id)->state != CrimeCaseState::Expired)
        return 16;
    auto snap = c.CaptureSnapshot();
    CrimeService r;
    if (!r.RestoreSnapshot(std::move(snap)))
        return 17;
    if (r.FindCrimesByOffender(player).size() != 1)
        return 18;
    return 0;
}
