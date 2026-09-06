#include "Epidemic/GameFramework/Crime/crime.h"

#include <cstdlib>
#include <iostream>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::crime;

namespace
{
GameplayObjectRef Ref(const char *name)
{
    return {GameplayDomainId::FromString("test"), GameplayObjectId::FromString(name)};
}

void Check(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "crime_tests: " << message << '\n';
        std::exit(1);
    }
}

struct Fixture
{
    CrimeService service;
    GameplayObjectRef player = Ref("player");
    GameplayObjectRef npc = Ref("npc");
    GameplayObjectRef city = Ref("city");
    GameplayObjectRef guards = Ref("guards");
    GameplayObjectRef clerks = Ref("clerks");
    GameplayObjectRef other_guards = Ref("other_guards");
    CrimeTypeId theft = CrimeTypeId::FromString("crime.theft");
    LawId law_id = LawId::FromString("law.theft");
    JurisdictionId jurisdiction = JurisdictionId::FromString("jur.city");
    LawResponseTypeId arrest = LawResponseTypeId::FromString("law_response.arrest");

    Fixture()
    {
        LawDefinition law;
        law.id = law_id;
        law.crime_type = theft;
        law.severity_micro = 300000;
        law.default_bounty = 50;
        law.statute_of_limitations = GameplaySeconds(50);
        Check(static_cast<bool>(service.RegisterLaw(law)), "register law");

        LawResponseDefinition response;
        response.id = arrest;
        response.required_authority_capabilities = 0x2;
        response.resulting_case_state = CrimeCaseState::Wanted;
        Check(static_cast<bool>(service.RegisterLawResponseDefinition(response)), "register response definition");

        JurisdictionRecord jurisdiction_record;
        jurisdiction_record.id = jurisdiction;
        jurisdiction_record.area = city;
        jurisdiction_record.authority_group = guards;
        jurisdiction_record.active_laws.push_back(law_id);
        jurisdiction_record.priority = 10;
        Check(static_cast<bool>(service.RegisterJurisdiction(jurisdiction_record)), "register jurisdiction");

        AuthorityRecord authority;
        authority.authority_group = guards;
        authority.jurisdiction = jurisdiction;
        authority.capabilities = 0x2;
        Check(static_cast<bool>(service.RegisterAuthority(authority)), "register authority");

        AuthorityRecord clerk_authority;
        clerk_authority.authority_group = clerks;
        clerk_authority.jurisdiction = jurisdiction;
        clerk_authority.capabilities = 0x1;
        Check(static_cast<bool>(service.RegisterAuthority(clerk_authority)), "register insufficient authority");

        JurisdictionRecord other_jurisdiction;
        other_jurisdiction.id = JurisdictionId::FromString("jur.other");
        other_jurisdiction.area = Ref("other_city");
        other_jurisdiction.authority_group = other_guards;
        other_jurisdiction.active_laws.push_back(law_id);
        Check(static_cast<bool>(service.RegisterJurisdiction(other_jurisdiction)), "register other jurisdiction");
        AuthorityRecord other_authority;
        other_authority.authority_group = other_guards;
        other_authority.jurisdiction = other_jurisdiction.id;
        other_authority.capabilities = 0x2;
        Check(static_cast<bool>(service.RegisterAuthority(other_authority)), "register authority in other jurisdiction");

        Check(static_cast<bool>(service.FreezeDefinitions()), "freeze definitions");
    }
};

CrimeCandidate MakeCandidate(const Fixture &fixture, const char *property_name, std::int64_t time)
{
    CrimeCandidate candidate;
    candidate.type = fixture.theft;
    candidate.offender = fixture.player;
    candidate.victim = fixture.npc;
    candidate.target_property = Ref(property_name);
    candidate.area = fixture.city;
    candidate.time = GameplayTimePoint{time};
    candidate.context.time = candidate.time;
    return candidate;
}
} // namespace

int main()
{
    Fixture fixture;
    auto &crime = fixture.service;

    Check(!crime.RegisterLaw({}), "definitions cannot mutate after freeze");
    const auto preview = crime.PreviewLegality(fixture.player, fixture.theft, fixture.city);
    Check(preview.illegal && preview.jurisdiction == fixture.jurisdiction, "legality preview");

    auto witness_required = crime.EvaluateCrimeCandidate(MakeCandidate(fixture, "unseen_apple", 90),
                                                          CrimeCandidatePolicy::RecordIfWitnessed);
    Check(static_cast<bool>(witness_required), "record-if-witnessed evaluation succeeds without record");
    Check(!witness_required.Value().crime, "record-if-witnessed does not create crime without witness");
    Check(crime.FindCrimesByOffender(fixture.player).empty(), "no hidden suspect record created");

    auto candidate = MakeCandidate(fixture, "apple", 100);
    auto evaluated = crime.EvaluateCrimeCandidate(candidate, CrimeCandidatePolicy::RecordAlways);
    Check(static_cast<bool>(evaluated) && evaluated.Value().crime.has_value(), "record crime");
    const auto crime_id = *evaluated.Value().crime;
    Check(crime.FindCrime(crime_id)->proof_state == CrimeProofState::Unknown, "initial proof unknown");

    EvidenceRecord strong_evidence;
    strong_evidence.crime = crime_id;
    strong_evidence.source = Ref("stolen_item");
    strong_evidence.type = EvidenceTypeId::FromString("evidence.possession");
    strong_evidence.strength_micro = 900000;
    auto evidence_id = crime.AddEvidence(strong_evidence);
    Check(static_cast<bool>(evidence_id), "add strong evidence");
    Check(crime.FindCrime(crime_id)->proof_state == CrimeProofState::Confirmed, "strong evidence confirms crime");

    WitnessRecord weak_witness;
    weak_witness.crime = crime_id;
    weak_witness.witness = fixture.npc;
    weak_witness.witness_type = WitnessTypeId::FromString("witness.visual");
    weak_witness.confidence_micro = 100000;
    auto weak_id = crime.AddWitness(weak_witness);
    Check(static_cast<bool>(weak_id), "add weak witness");
    Check(crime.FindCrime(crime_id)->proof_state == CrimeProofState::Confirmed,
          "weak witness cannot downgrade confirmed proof");

    auto duplicate_weak = crime.AddWitness(weak_witness);
    Check(static_cast<bool>(duplicate_weak) && duplicate_weak.Value() == weak_id.Value(),
          "duplicate witness is idempotent");
    Check(crime.FindWitnesses(crime_id).size() == 1, "duplicate witness not stored twice");

    auto duplicate_evidence = crime.AddEvidence(strong_evidence);
    Check(static_cast<bool>(duplicate_evidence) && duplicate_evidence.Value() == evidence_id.Value(),
          "duplicate evidence is idempotent");
    Check(crime.FindEvidence(crime_id).size() == 1, "duplicate evidence not stored twice");

    Check(static_cast<bool>(crime.RemoveEvidence(evidence_id.Value())), "remove evidence");
    Check(crime.FindCrime(crime_id)->proof_state == CrimeProofState::Suspected,
          "removing strong evidence recalculates proof");

    WitnessRecord strong_witness;
    strong_witness.crime = crime_id;
    strong_witness.witness = Ref("guard_witness");
    strong_witness.witness_type = WitnessTypeId::FromString("witness.visual");
    strong_witness.confidence_micro = 900000;
    auto strong_witness_id = crime.AddWitness(strong_witness);
    Check(static_cast<bool>(strong_witness_id), "add strong witness");
    Check(crime.FindCrime(crime_id)->proof_state == CrimeProofState::Witnessed, "strong witness raises proof");
    Check(static_cast<bool>(crime.RemoveWitness(strong_witness_id.Value())), "remove strong witness");
    Check(crime.FindCrime(crime_id)->proof_state == CrimeProofState::Suspected,
          "removing strong witness falls back to weak witness");
    Check(static_cast<bool>(crime.RemoveWitness(weak_id.Value())), "remove weak witness");
    Check(crime.FindCrime(crime_id)->proof_state == CrimeProofState::Unknown, "proof returns to unknown");

    auto witnessed_candidate = MakeCandidate(fixture, "pear", 110);
    CrimeCandidateWitness initial;
    initial.witness = Ref("citizen_witness");
    initial.witness_type = WitnessTypeId::FromString("witness.visual");
    initial.confidence_micro = 900000;
    initial.observed_at = GameplayTimePoint{110};
    witnessed_candidate.initial_witness = initial;
    auto witnessed = crime.EvaluateCrimeCandidate(witnessed_candidate, CrimeCandidatePolicy::RecordIfWitnessed);
    Check(static_cast<bool>(witnessed) && witnessed.Value().crime.has_value(),
          "record-if-witnessed creates crime with witness");
    Check(crime.FindCrime(*witnessed.Value().crime)->proof_state == CrimeProofState::Witnessed,
          "initial witness contributes to proof");

    auto unauthorized = crime.GenerateLawResponse(
        {crime_id, fixture.arrest, Ref("fake_guards"), fixture.player, {}});
    Check(!unauthorized, "unregistered authority cannot generate response");
    auto insufficient = crime.GenerateLawResponse({crime_id, fixture.arrest, fixture.clerks, fixture.player, {}});
    Check(!insufficient, "authority without required capability rejected");
    auto wrong_jurisdiction =
        crime.GenerateLawResponse({crime_id, fixture.arrest, fixture.other_guards, fixture.player, {}});
    Check(!wrong_jurisdiction, "authority from another jurisdiction rejected");

    auto response = crime.GenerateLawResponse({crime_id, fixture.arrest, fixture.guards, fixture.player, {}});
    Check(static_cast<bool>(response), "authorized law response");
    Check(crime.FindCrime(crime_id)->state == CrimeCaseState::Wanted,
          "response definition controls case-state transition");

    BountyRecord bounty;
    bounty.offender = fixture.player;
    bounty.jurisdiction = fixture.jurisdiction;
    bounty.amount = 100;
    bounty.expires_at = GameplayTimePoint{140};
    bounty.source_crimes = {crime_id};
    auto first_bounty = crime.CreateBounty(bounty);
    Check(static_cast<bool>(first_bounty), "create aggregate bounty");
    Check(!crime.CreateBounty(bounty), "second active bounty for offender/jurisdiction rejected");
    Check(crime.GetBounty(fixture.player, fixture.jurisdiction)->id == first_bounty.Value(),
          "active bounty lookup is deterministic");
    Check(static_cast<bool>(crime.ResolveBounty(first_bounty.Value(), BountyState::Paid)), "resolve bounty");

    bounty.id = {};
    auto second_bounty = crime.CreateBounty(bounty);
    Check(static_cast<bool>(second_bounty), "new bounty allowed after previous terminal");
    Check(crime.GetBounty(fixture.player, fixture.jurisdiction)->id == second_bounty.Value(),
          "new active bounty indexed");

    const auto expired_count = crime.ExpireDue(GameplayTimePoint{151});
    Check(expired_count == 2, "due crime and bounty expire by gameplay time");
    Check(crime.FindCrime(crime_id)->state == CrimeCaseState::Expired, "crime statute expiry");
    Check(!crime.GetBounty(fixture.player, fixture.jurisdiction), "expired bounty leaves active index");

    crime.SetChangeJournalCapacity(2);
    Check(static_cast<bool>(crime.ChangeCaseState(*witnessed.Value().crime, CrimeCaseState::UnderInvestigation)),
          "change case for journal test");
    Check(static_cast<bool>(crime.ChangeCaseState(*witnessed.Value().crime, CrimeCaseState::Reported)),
          "second journal change");
    Check(static_cast<bool>(crime.ChangeCaseState(*witnessed.Value().crime, CrimeCaseState::Open)),
          "third journal change");
    const auto old_batch = crime.ReadChangesSince(0);
    Check(old_batch.snapshot_required, "bounded journal requires snapshot for stale reader");
    const auto latest_batch = crime.ReadChangesSince(crime.LatestChangeSequence());
    Check(!latest_batch.snapshot_required && latest_batch.changes.empty(), "current journal reader is up to date");

    const auto snapshot = crime.CaptureSnapshot();

    CrimeService preserved;
    LawDefinition keep_law;
    keep_law.id = LawId::FromString("law.keep");
    keep_law.crime_type = CrimeTypeId::FromString("crime.keep");
    Check(static_cast<bool>(preserved.RegisterLaw(keep_law)), "prepare transactional restore target law");
    JurisdictionRecord keep_jurisdiction;
    keep_jurisdiction.id = JurisdictionId::FromString("jur.keep");
    keep_jurisdiction.area = Ref("keep_area");
    keep_jurisdiction.authority_group = Ref("keep_authority");
    keep_jurisdiction.active_laws.push_back(keep_law.id);
    Check(static_cast<bool>(preserved.RegisterJurisdiction(keep_jurisdiction)), "prepare restore target jurisdiction");
    Check(static_cast<bool>(preserved.FreezeDefinitions()), "freeze restore target");
    const auto preserved_revision = preserved.CurrentRevision();

    auto bad_generator = snapshot;
    bad_generator.crime_ids.next = 1;
    Check(!preserved.RestoreSnapshot(std::move(bad_generator)), "stale generator snapshot rejected");
    Check(preserved.CurrentRevision() == preserved_revision, "failed restore leaves revision unchanged");
    Check(preserved.GetApplicableJurisdiction(Ref("keep_area")).has_value(),
          "failed restore leaves old state unchanged");

    auto bad_reference = snapshot;
    Check(!bad_reference.witnesses.empty(), "snapshot contains witness for corruption test");
    bad_reference.witnesses.front().crime = CrimeRecordId::FromString("missing.crime");
    Check(!preserved.RestoreSnapshot(std::move(bad_reference)), "dangling witness reference rejected");
    Check(preserved.GetApplicableJurisdiction(Ref("keep_area")).has_value(),
          "cross-reference restore failure remains transactional");

    CrimeService restored;
    Check(static_cast<bool>(restored.RestoreSnapshot(snapshot)), "valid snapshot restore");
    Check(restored.DefinitionsFrozen(), "definition freeze state restored");
    Check(restored.FindCrimesByOffender(fixture.player).size() == 2, "runtime crimes restored");
    Check(!restored.GetBounty(fixture.player, fixture.jurisdiction), "terminal bounty state restored without active index");

    Check(static_cast<bool>(restored.PruneTerminalCrime(crime_id)), "prune terminal crime");
    Check(restored.FindCrime(crime_id) == nullptr, "terminal crime removed");

    return 0;
}
