#include "Epidemic/GameFramework/Crime/crime.h"
#include "Epidemic/Foundation/error.h"
#include <iterator>
#include <utility>

namespace epidemic::gameplay::crime
{
namespace
{
foundation::Error Error(std::string_view c, std::string_view m)
{
    return foundation::Error::Create(c, m);
}
} // namespace
foundation::Result<void> CrimeService::RegisterLaw(LawDefinition l)
{
    if (!l.id.IsValid() || !l.crime_type.IsValid() || laws_.contains(l.id))
        return foundation::Result<void>::Failure(Error("gameplay.crime.invalid_law", "invalid or duplicate law"));
    Bump();
    l.revision = revision_;
    laws_[l.id] = l;
    return foundation::Result<void>::Success();
}
foundation::Result<void> CrimeService::RegisterJurisdiction(JurisdictionRecord j)
{
    if (!j.id.IsValid() || !j.area.IsValid() || jurisdictions_.contains(j.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.crime.invalid_jurisdiction", "invalid or duplicate jurisdiction"));
    std::sort(j.active_laws.begin(), j.active_laws.end());
    for (auto law : j.active_laws)
    {
        if (!laws_.contains(law))
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.unknown_law", "jurisdiction references unknown law"));
    }
    Bump();
    j.revision = revision_;
    jurisdictions_[j.id] = j;
    return foundation::Result<void>::Success();
}
foundation::Result<AuthorityId> CrimeService::RegisterAuthority(AuthorityRecord a)
{
    if (!a.authority_group.IsValid() || !jurisdictions_.contains(a.jurisdiction))
        return foundation::Result<AuthorityId>::Failure(Error("gameplay.crime.invalid_authority", "invalid authority"));
    if (!a.id.IsValid())
        a.id = AuthorityId{authority_ids_.Next()};
    if (authorities_.contains(a.id))
        return foundation::Result<AuthorityId>::Failure(
            Error("gameplay.crime.duplicate_authority", "duplicate authority"));
    Bump();
    a.revision = revision_;
    auto id = a.id;
    authorities_[id] = a;
    return foundation::Result<AuthorityId>::Success(id);
}
std::optional<JurisdictionRecord> CrimeService::GetApplicableJurisdiction(GameplayObjectRef area) const
{
    ++diagnostics_.jurisdiction_resolutions;
    std::vector<JurisdictionRecord> matches;
    for (const auto &[id, j] : jurisdictions_)
    {
        (void)id;
        if (j.area == area)
            matches.push_back(j);
    }
    std::sort(matches.begin(), matches.end(), [](auto &a, auto &b) {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        return a.id < b.id;
    });
    if (matches.empty())
        return std::nullopt;
    return matches.front();
}
const LawDefinition *CrimeService::FindLawFor(CrimeTypeId type, JurisdictionId jurisdiction) const noexcept
{
    auto jit = jurisdictions_.find(jurisdiction);
    if (jit == jurisdictions_.end())
        return nullptr;
    for (auto law_id : jit->second.active_laws)
    {
        auto lit = laws_.find(law_id);
        if (lit != laws_.end() && lit->second.crime_type == type)
            return &lit->second;
    }
    return nullptr;
}
foundation::Result<CrimeEvaluationResult> CrimeService::EvaluateCrimeCandidate(CrimeCandidate c,
                                                                               CrimeCandidatePolicy policy)
{
    ++diagnostics_.crime_candidates;
    if (!c.type.IsValid() || !c.offender.IsValid())
        return foundation::Result<CrimeEvaluationResult>::Failure(
            Error("gameplay.crime.invalid_candidate", "invalid crime candidate"));
    auto j = GetApplicableJurisdiction(c.area);
    if (!j)
    {
        if (policy == CrimeCandidatePolicy::IgnoreWithoutJurisdiction)
        {
            CrimeEvaluationResult ignored;
            ignored.revision = revision_;
            return foundation::Result<CrimeEvaluationResult>::Success(ignored);
        }
        return foundation::Result<CrimeEvaluationResult>::Failure(
            Error("gameplay.crime.no_jurisdiction", "no applicable jurisdiction"));
    }
    auto law = FindLawFor(c.type, j->id);
    if (!law)
        return foundation::Result<CrimeEvaluationResult>::Failure(
            Error("gameplay.crime.no_law", "no law for crime type"));
    Bump();
    CrimeRecord r;
    r.id = CrimeRecordId{crime_ids_.Next()};
    r.type = c.type;
    r.offender = c.offender;
    r.victim = c.victim;
    r.target_property = c.target_property;
    r.jurisdiction = j->id;
    r.severity_micro = law->severity_micro;
    r.proof_state =
        policy == CrimeCandidatePolicy::RecordIfWitnessed ? CrimeProofState::Suspected : CrimeProofState::Unknown;
    r.state = CrimeCaseState::Open;
    r.committed_at = c.time;
    r.payload = c.payload;
    r.revision = revision_;
    auto id = r.id;
    crimes_[id] = r;
    Record({0,
            CrimeChangeKind::CrimeCandidateCreated,
            id,
            c.offender,
            c.victim,
            j->id,
            {},
            {},
            {},
            {},
            c.context,
            revision_});
    Record({0, CrimeChangeKind::CrimeRecorded, id, c.offender, c.victim, j->id, {}, {}, {}, {}, c.context, revision_});
    CrimeEvaluationResult out;
    out.crime = id;
    out.proof_state = r.proof_state;
    out.jurisdiction = j->id;
    out.revision = revision_;
    return foundation::Result<CrimeEvaluationResult>::Success(out);
}
CrimeRecord *CrimeService::FindMutableCrime(CrimeRecordId id) noexcept
{
    auto it = crimes_.find(id);
    return it == crimes_.end() ? nullptr : &it->second;
}
const CrimeRecord *CrimeService::FindCrime(CrimeRecordId id) const noexcept
{
    auto it = crimes_.find(id);
    return it == crimes_.end() ? nullptr : &it->second;
}
foundation::Result<WitnessRecordId> CrimeService::AddWitness(WitnessRecord w)
{
    auto *c = FindMutableCrime(w.crime);
    if (!c || !w.witness.IsValid())
        return foundation::Result<WitnessRecordId>::Failure(Error("gameplay.crime.invalid_witness", "invalid witness"));
    if (!w.id.IsValid())
        w.id = WitnessRecordId{witness_ids_.Next()};
    if (witnesses_.contains(w.id))
        return foundation::Result<WitnessRecordId>::Failure(
            Error("gameplay.crime.duplicate_witness", "duplicate witness"));
    Bump();
    w.revision = revision_;
    auto id = w.id;
    witnesses_[id] = w;
    c->witnesses.push_back(id);
    std::sort(c->witnesses.begin(), c->witnesses.end());
    c->proof_state = w.confidence_micro >= 800000 ? CrimeProofState::Witnessed : CrimeProofState::Suspected;
    c->revision = revision_;
    Record({0,
            CrimeChangeKind::WitnessAdded,
            w.crime,
            c->offender,
            c->victim,
            c->jurisdiction,
            id,
            {},
            {},
            {},
            {},
            revision_});
    Record({0,
            CrimeChangeKind::ProofStateChanged,
            w.crime,
            c->offender,
            c->victim,
            c->jurisdiction,
            id,
            {},
            {},
            {},
            {},
            revision_});
    return foundation::Result<WitnessRecordId>::Success(id);
}
foundation::Result<EvidenceId> CrimeService::AddEvidence(EvidenceRecord e)
{
    auto *c = FindMutableCrime(e.crime);
    if (!c || !e.source.IsValid())
        return foundation::Result<EvidenceId>::Failure(Error("gameplay.crime.invalid_evidence", "invalid evidence"));
    if (!e.id.IsValid())
        e.id = EvidenceId{evidence_ids_.Next()};
    if (evidence_.contains(e.id))
        return foundation::Result<EvidenceId>::Failure(
            Error("gameplay.crime.duplicate_evidence", "duplicate evidence"));
    Bump();
    e.revision = revision_;
    auto id = e.id;
    evidence_[id] = e;
    c->evidence.push_back(id);
    std::sort(c->evidence.begin(), c->evidence.end());
    c->proof_state = e.strength_micro >= 800000 ? CrimeProofState::Confirmed : CrimeProofState::EvidenceFound;
    c->revision = revision_;
    Record({0,
            CrimeChangeKind::EvidenceAdded,
            e.crime,
            c->offender,
            c->victim,
            c->jurisdiction,
            {},
            id,
            {},
            {},
            {},
            revision_});
    Record({0,
            CrimeChangeKind::ProofStateChanged,
            e.crime,
            c->offender,
            c->victim,
            c->jurisdiction,
            {},
            id,
            {},
            {},
            {},
            revision_});
    return foundation::Result<EvidenceId>::Success(id);
}
foundation::Result<BountyRecordId> CrimeService::CreateBounty(BountyRecord b)
{
    if (!b.offender.IsValid() || !b.jurisdiction.IsValid())
        return foundation::Result<BountyRecordId>::Failure(Error("gameplay.crime.invalid_bounty", "invalid bounty"));
    if (!b.id.IsValid())
        b.id = BountyRecordId{bounty_ids_.Next()};
    if (bounties_.contains(b.id))
        return foundation::Result<BountyRecordId>::Failure(
            Error("gameplay.crime.duplicate_bounty", "duplicate bounty"));
    Bump();
    b.revision = revision_;
    auto id = b.id;
    bounties_[id] = b;
    Record({0, CrimeChangeKind::BountyCreated, {}, b.offender, {}, b.jurisdiction, {}, {}, id, {}, {}, revision_});
    return foundation::Result<BountyRecordId>::Success(id);
}
foundation::Result<void> CrimeService::ResolveBounty(BountyRecordId id, BountyState state, GameplayContext context)
{
    auto it = bounties_.find(id);
    if (it == bounties_.end())
        return foundation::Result<void>::Failure(Error("gameplay.crime.bounty_missing", "bounty missing"));
    Bump();
    it->second.state = state;
    it->second.revision = revision_;
    Record({0,
            state == BountyState::Active ? CrimeChangeKind::BountyChanged : CrimeChangeKind::BountyResolved,
            {},
            it->second.offender,
            {},
            it->second.jurisdiction,
            {},
            {},
            id,
            {},
            context,
            revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<LawResponseId> CrimeService::GenerateLawResponse(LawResponseRequest q)
{
    auto *c = FindMutableCrime(q.crime);
    if (!c || !q.response_type.IsValid())
        return foundation::Result<LawResponseId>::Failure(
            Error("gameplay.crime.invalid_response", "invalid law response"));
    Bump();
    LawResponseRecord r;
    r.id = LawResponseId{response_ids_.Next()};
    r.crime = q.crime;
    r.response_type = q.response_type;
    r.authority = q.authority;
    r.target = q.target;
    r.revision = revision_;
    auto id = r.id;
    responses_[id] = r;
    c->state = CrimeCaseState::Wanted;
    c->revision = revision_;
    Record({0,
            CrimeChangeKind::LawResponseGenerated,
            q.crime,
            c->offender,
            c->victim,
            c->jurisdiction,
            {},
            {},
            {},
            id,
            q.context,
            revision_});
    return foundation::Result<LawResponseId>::Success(id);
}
foundation::Result<void> CrimeService::ChangeCaseState(CrimeRecordId id, CrimeCaseState state, GameplayContext context)
{
    auto *c = FindMutableCrime(id);
    if (!c)
        return foundation::Result<void>::Failure(Error("gameplay.crime.crime_missing", "crime missing"));
    Bump();
    c->state = state;
    c->revision = revision_;
    Record({0,
            CrimeChangeKind::CaseStateChanged,
            id,
            c->offender,
            c->victim,
            c->jurisdiction,
            {},
            {},
            {},
            {},
            context,
            revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> CrimeService::ChangeProofState(CrimeRecordId id, CrimeProofState state,
                                                        GameplayContext context)
{
    auto *c = FindMutableCrime(id);
    if (!c)
        return foundation::Result<void>::Failure(Error("gameplay.crime.crime_missing", "crime missing"));
    Bump();
    c->proof_state = state;
    c->revision = revision_;
    Record({0,
            CrimeChangeKind::ProofStateChanged,
            id,
            c->offender,
            c->victim,
            c->jurisdiction,
            {},
            {},
            {},
            {},
            context,
            revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> CrimeService::ExpireCrime(CrimeRecordId id, GameplayContext context)
{
    auto *c = FindMutableCrime(id);
    if (!c)
        return foundation::Result<void>::Failure(Error("gameplay.crime.crime_missing", "crime missing"));
    Bump();
    c->state = CrimeCaseState::Expired;
    c->revision = revision_;
    ++diagnostics_.expired_crimes;
    Record({0,
            CrimeChangeKind::CrimeExpired,
            id,
            c->offender,
            c->victim,
            c->jurisdiction,
            {},
            {},
            {},
            {},
            context,
            revision_});
    return foundation::Result<void>::Success();
}
LegalityPreview CrimeService::PreviewLegality(GameplayObjectRef offender, CrimeTypeId type,
                                              GameplayObjectRef area) const
{
    (void)offender;
    LegalityPreview out;
    out.crime_type = type;
    out.revision = revision_;
    auto j = GetApplicableJurisdiction(area);
    if (!j)
        return out;
    auto law = FindLawFor(type, j->id);
    if (!law)
        return out;
    out.illegal = true;
    out.jurisdiction = j->id;
    out.severity_micro = law->severity_micro;
    return out;
}
std::vector<CrimeRecord> CrimeService::FindCrimesByOffender(GameplayObjectRef o) const
{
    std::vector<CrimeRecord> out;
    for (const auto &[id, c] : crimes_)
    {
        (void)id;
        if (c.offender == o)
            out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return out;
}
std::vector<CrimeRecord> CrimeService::FindCrimesByVictim(GameplayObjectRef v) const
{
    std::vector<CrimeRecord> out;
    for (const auto &[id, c] : crimes_)
    {
        (void)id;
        if (c.victim == v)
            out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return out;
}
std::vector<CrimeRecord> CrimeService::FindCrimesByJurisdiction(JurisdictionId j) const
{
    std::vector<CrimeRecord> out;
    for (const auto &[id, c] : crimes_)
    {
        (void)id;
        if (c.jurisdiction == j)
            out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return out;
}
std::vector<CrimeRecord> CrimeService::FindOpenCases() const
{
    std::vector<CrimeRecord> out;
    for (const auto &[id, c] : crimes_)
    {
        (void)id;
        if (c.state == CrimeCaseState::Open || c.state == CrimeCaseState::Reported ||
            c.state == CrimeCaseState::UnderInvestigation || c.state == CrimeCaseState::Wanted)
            out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return out;
}
std::vector<WitnessRecord> CrimeService::FindWitnesses(CrimeRecordId id) const
{
    std::vector<WitnessRecord> out;
    for (const auto &[wid, w] : witnesses_)
    {
        (void)wid;
        if (w.crime == id)
            out.push_back(w);
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return out;
}
std::vector<EvidenceRecord> CrimeService::FindEvidence(CrimeRecordId id) const
{
    std::vector<EvidenceRecord> out;
    for (const auto &[eid, e] : evidence_)
    {
        (void)eid;
        if (e.crime == id)
            out.push_back(e);
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return out;
}
std::optional<BountyRecord> CrimeService::GetBounty(GameplayObjectRef o, JurisdictionId j) const
{
    for (const auto &[id, b] : bounties_)
    {
        (void)id;
        if (b.offender == o && b.jurisdiction == j && b.state == BountyState::Active)
            return b;
    }
    return std::nullopt;
}
std::vector<CrimeChange> CrimeService::ChangesSince(std::uint64_t seq) const
{
    std::vector<CrimeChange> out;
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(out),
                 [seq](auto &c) { return c.sequence > seq; });
    return out;
}
CrimeSnapshot CrimeService::CaptureSnapshot() const
{
    CrimeSnapshot s;
    for (const auto &[id, l] : laws_)
    {
        (void)id;
        s.laws.push_back(l);
    }
    for (const auto &[id, j] : jurisdictions_)
    {
        (void)id;
        s.jurisdictions.push_back(j);
    }
    for (const auto &[id, a] : authorities_)
    {
        (void)id;
        s.authorities.push_back(a);
    }
    for (const auto &[id, c] : crimes_)
    {
        (void)id;
        s.crimes.push_back(c);
    }
    for (const auto &[id, w] : witnesses_)
    {
        (void)id;
        s.witnesses.push_back(w);
    }
    for (const auto &[id, e] : evidence_)
    {
        (void)id;
        s.evidence.push_back(e);
    }
    for (const auto &[id, b] : bounties_)
    {
        (void)id;
        s.bounties.push_back(b);
    }
    for (const auto &[id, r] : responses_)
    {
        (void)id;
        s.responses.push_back(r);
    }
    std::sort(s.laws.begin(), s.laws.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.jurisdictions.begin(), s.jurisdictions.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.authorities.begin(), s.authorities.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.crimes.begin(), s.crimes.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.witnesses.begin(), s.witnesses.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.evidence.begin(), s.evidence.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.bounties.begin(), s.bounties.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.responses.begin(), s.responses.end(), [](auto &a, auto &b) { return a.id < b.id; });
    s.crime_ids = crime_ids_.GetSnapshot();
    s.witness_ids = witness_ids_.GetSnapshot();
    s.evidence_ids = evidence_ids_.GetSnapshot();
    s.bounty_ids = bounty_ids_.GetSnapshot();
    s.authority_ids = authority_ids_.GetSnapshot();
    s.response_ids = response_ids_.GetSnapshot();
    s.revision = revision_;
    return s;
}
foundation::Result<void> CrimeService::RestoreSnapshot(CrimeSnapshot s)
{
    laws_.clear();
    jurisdictions_.clear();
    authorities_.clear();
    crimes_.clear();
    witnesses_.clear();
    evidence_.clear();
    bounties_.clear();
    responses_.clear();
    for (auto &l : s.laws)
    {
        if (!l.id.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.crime.restore_invalid", "invalid law snapshot"));
        laws_[l.id] = l;
    }
    for (auto &j : s.jurisdictions)
    {
        if (!j.id.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.restore_invalid", "invalid jurisdiction snapshot"));
        jurisdictions_[j.id] = j;
    }
    for (auto &a : s.authorities)
    {
        if (!a.id.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.restore_invalid", "invalid authority snapshot"));
        authorities_[a.id] = a;
    }
    for (auto &c : s.crimes)
    {
        if (!c.id.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.crime.restore_invalid", "invalid crime snapshot"));
        crimes_[c.id] = c;
    }
    for (auto &w : s.witnesses)
    {
        if (!w.id.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.restore_invalid", "invalid witness snapshot"));
        witnesses_[w.id] = w;
    }
    for (auto &e : s.evidence)
    {
        if (!e.id.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.restore_invalid", "invalid evidence snapshot"));
        evidence_[e.id] = e;
    }
    for (auto &b : s.bounties)
    {
        if (!b.id.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.restore_invalid", "invalid bounty snapshot"));
        bounties_[b.id] = b;
    }
    for (auto &r : s.responses)
    {
        if (!r.id.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.restore_invalid", "invalid response snapshot"));
        responses_[r.id] = r;
    }
    crime_ids_.Restore(s.crime_ids);
    witness_ids_.Restore(s.witness_ids);
    evidence_ids_.Restore(s.evidence_ids);
    bounty_ids_.Restore(s.bounty_ids);
    authority_ids_.Restore(s.authority_ids);
    response_ids_.Restore(s.response_ids);
    revision_ = s.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    return foundation::Result<void>::Success();
}
CrimeDiagnostics CrimeService::GetDiagnostics() const noexcept
{
    auto d = diagnostics_;
    d.crime_records = crimes_.size();
    d.open_cases = FindOpenCases().size();
    d.wanted_subjects = 0;
    for (const auto &[id, c] : crimes_)
    {
        (void)id;
        if (c.state == CrimeCaseState::Wanted)
            ++d.wanted_subjects;
    }
    d.witnesses = witnesses_.size();
    d.evidence_records = evidence_.size();
    d.bounties = bounties_.size();
    d.law_responses = responses_.size();
    return d;
}
void CrimeService::Record(CrimeChange c)
{
    c.sequence = next_change_sequence_++;
    changes_.push_back(c);
}
} // namespace epidemic::gameplay::crime
