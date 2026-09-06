#include "Epidemic/GameFramework/Crime/crime.h"
#include "Epidemic/Foundation/error.h"

#include <iterator>
#include <limits>
#include <unordered_set>
#include <utility>

namespace epidemic::gameplay::crime
{
namespace
{
constexpr std::int64_t kProofThresholdMicro = 800000;
constexpr std::int64_t kMicroMax = 1000000;

foundation::Error Error(std::string_view c, std::string_view m)
{
    return foundation::Error::Create(c, m);
}

[[nodiscard]] bool ValidMicro(std::int64_t value) noexcept
{
    return value >= 0 && value <= kMicroMax;
}

[[nodiscard]] bool IsTerminalCase(CrimeCaseState state) noexcept
{
    switch (state)
    {
    case CrimeCaseState::Punished:
    case CrimeCaseState::Forgiven:
    case CrimeCaseState::Expired:
    case CrimeCaseState::Dismissed:
    case CrimeCaseState::Resolved:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool IsDerivedProofState(CrimeProofState state) noexcept
{
    switch (state)
    {
    case CrimeProofState::Unknown:
    case CrimeProofState::Suspected:
    case CrimeProofState::Witnessed:
    case CrimeProofState::EvidenceFound:
    case CrimeProofState::Confirmed:
        return true;
    case CrimeProofState::Reported:
    case CrimeProofState::FalseAccusation:
    case CrimeProofState::Resolved:
        return false;
    }
    return false;
}

template <class TWrappedId>
void AdvanceGeneratorPastAcceptedId(MonotonicIdGenerator<GameplayObjectId> &generator, TWrappedId id) noexcept
{
    if (!id.IsValid())
        return;
    auto snapshot = generator.GetSnapshot();
    if (id.value.High() != snapshot.scope || snapshot.next == 0 || id.value.Low() < snapshot.next)
        return;
    snapshot.next = id.value.Low() == std::numeric_limits<std::uint64_t>::max() ? 0 : id.value.Low() + 1;
    generator.Restore(snapshot);
}

template <class TWrappedId>
void TrackMaxLowForScope(TWrappedId id, std::uint64_t scope, std::uint64_t &max_low) noexcept
{
    if (id.IsValid() && id.value.High() == scope && id.value.Low() > max_low)
        max_low = id.value.Low();
}

[[nodiscard]] bool ContainsId(const std::vector<CrimeRecordId> &ids, CrimeRecordId id) noexcept
{
    return std::binary_search(ids.begin(), ids.end(), id);
}
} // namespace

foundation::Result<void> CrimeService::RegisterLaw(LawDefinition law)
{
    if (definitions_frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.crime.definitions_frozen", "crime definitions frozen"));
    if (!law.id.IsValid() || !law.crime_type.IsValid() || laws_.contains(law.id) || law.severity_micro < 0 ||
        law.default_bounty < 0 || law.statute_of_limitations.ticks < 0)
        return foundation::Result<void>::Failure(Error("gameplay.crime.invalid_law", "invalid or duplicate law"));
    Bump();
    law.revision = revision_;
    laws_[law.id] = std::move(law);
    return foundation::Result<void>::Success();
}

foundation::Result<void> CrimeService::RegisterLawResponseDefinition(LawResponseDefinition definition)
{
    if (definitions_frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.crime.definitions_frozen", "crime definitions frozen"));
    if (!definition.id.IsValid() || response_definitions_.contains(definition.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.crime.invalid_response_definition", "invalid or duplicate law response definition"));
    Bump();
    definition.revision = revision_;
    response_definitions_[definition.id] = std::move(definition);
    return foundation::Result<void>::Success();
}

foundation::Result<void> CrimeService::RegisterJurisdiction(JurisdictionRecord jurisdiction)
{
    if (definitions_frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.crime.definitions_frozen", "crime definitions frozen"));
    if (!jurisdiction.id.IsValid() || !jurisdiction.area.IsValid() || !jurisdiction.authority_group.IsValid() ||
        jurisdictions_.contains(jurisdiction.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.crime.invalid_jurisdiction", "invalid or duplicate jurisdiction"));
    std::sort(jurisdiction.active_laws.begin(), jurisdiction.active_laws.end());
    if (std::adjacent_find(jurisdiction.active_laws.begin(), jurisdiction.active_laws.end()) !=
        jurisdiction.active_laws.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.crime.duplicate_law", "jurisdiction contains duplicate law"));
    for (auto law : jurisdiction.active_laws)
    {
        if (!laws_.contains(law))
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.unknown_law", "jurisdiction references unknown law"));
    }
    Bump();
    jurisdiction.revision = revision_;
    jurisdictions_[jurisdiction.id] = std::move(jurisdiction);
    return foundation::Result<void>::Success();
}

foundation::Result<AuthorityId> CrimeService::RegisterAuthority(AuthorityRecord authority)
{
    if (definitions_frozen_)
        return foundation::Result<AuthorityId>::Failure(
            Error("gameplay.crime.definitions_frozen", "crime definitions frozen"));
    if (!authority.authority_group.IsValid() || !jurisdictions_.contains(authority.jurisdiction))
        return foundation::Result<AuthorityId>::Failure(Error("gameplay.crime.invalid_authority", "invalid authority"));
    for (const auto &[id, existing] : authorities_)
    {
        (void)id;
        if (existing.authority_group == authority.authority_group && existing.jurisdiction == authority.jurisdiction)
            return foundation::Result<AuthorityId>::Failure(
                Error("gameplay.crime.duplicate_authority", "duplicate authority for jurisdiction"));
    }
    if (!authority.id.IsValid())
    {
        authority.id = AuthorityId{authority_ids_.Next()};
        if (!authority.id.IsValid())
            return foundation::Result<AuthorityId>::Failure(
                Error("gameplay.crime.id_exhausted", "authority id generator exhausted"));
    }
    if (authorities_.contains(authority.id))
        return foundation::Result<AuthorityId>::Failure(
            Error("gameplay.crime.duplicate_authority", "duplicate authority"));
    AdvanceGeneratorPastAcceptedId(authority_ids_, authority.id);
    Bump();
    authority.revision = revision_;
    auto id = authority.id;
    authorities_[id] = std::move(authority);
    return foundation::Result<AuthorityId>::Success(id);
}

foundation::Result<void> CrimeService::FreezeDefinitions()
{
    if (definitions_frozen_)
        return foundation::Result<void>::Success();
    for (const auto &[id, jurisdiction] : jurisdictions_)
    {
        (void)id;
        for (auto law : jurisdiction.active_laws)
        {
            if (!laws_.contains(law))
                return foundation::Result<void>::Failure(
                    Error("gameplay.crime.unknown_law", "jurisdiction references unknown law"));
        }
    }
    for (const auto &[id, authority] : authorities_)
    {
        (void)id;
        if (!jurisdictions_.contains(authority.jurisdiction))
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.invalid_authority", "authority references unknown jurisdiction"));
    }
    definitions_frozen_ = true;
    return foundation::Result<void>::Success();
}

std::optional<JurisdictionRecord> CrimeService::GetApplicableJurisdiction(GameplayObjectRef area) const
{
    ++diagnostics_.jurisdiction_resolutions;
    std::vector<JurisdictionRecord> matches;
    for (const auto &[id, jurisdiction] : jurisdictions_)
    {
        (void)id;
        if (jurisdiction.area == area)
            matches.push_back(jurisdiction);
    }
    std::sort(matches.begin(), matches.end(), [](const auto &a, const auto &b) {
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

const LawResponseDefinition *CrimeService::FindResponseDefinition(LawResponseTypeId id) const noexcept
{
    auto it = response_definitions_.find(id);
    return it == response_definitions_.end() ? nullptr : &it->second;
}

foundation::Result<CrimeEvaluationResult> CrimeService::EvaluateCrimeCandidate(CrimeCandidate candidate,
                                                                               CrimeCandidatePolicy policy)
{
    ++diagnostics_.crime_candidates;
    if (!definitions_frozen_)
        return foundation::Result<CrimeEvaluationResult>::Failure(
            Error("gameplay.crime.definitions_not_frozen", "crime definitions must be frozen before evaluation"));
    if (!candidate.type.IsValid() || !candidate.offender.IsValid())
        return foundation::Result<CrimeEvaluationResult>::Failure(
            Error("gameplay.crime.invalid_candidate", "invalid crime candidate"));
    if (candidate.initial_witness)
    {
        const auto &w = *candidate.initial_witness;
        if (!w.witness.IsValid() || !w.witness_type.IsValid() || !ValidMicro(w.confidence_micro))
            return foundation::Result<CrimeEvaluationResult>::Failure(
                Error("gameplay.crime.invalid_witness", "invalid initial crime witness"));
    }

    auto jurisdiction = GetApplicableJurisdiction(candidate.area);
    if (!jurisdiction)
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
    auto law = FindLawFor(candidate.type, jurisdiction->id);
    if (!law)
        return foundation::Result<CrimeEvaluationResult>::Failure(
            Error("gameplay.crime.no_law", "no law for crime type"));

    CrimeEvaluationResult out;
    out.jurisdiction = jurisdiction->id;
    out.revision = revision_;
    if (policy == CrimeCandidatePolicy::RecordIfWitnessed && !candidate.initial_witness)
    {
        out.reasons.push_back(TypeId::FromString("framework.crime.reason.witness_required"));
        return foundation::Result<CrimeEvaluationResult>::Success(std::move(out));
    }

    CrimeRecord record;
    record.id = CrimeRecordId{crime_ids_.Next()};
    if (!record.id.IsValid())
        return foundation::Result<CrimeEvaluationResult>::Failure(
            Error("gameplay.crime.id_exhausted", "crime id generator exhausted"));
    record.type = candidate.type;
    record.offender = candidate.offender;
    record.victim = candidate.victim;
    record.target_property = candidate.target_property;
    record.jurisdiction = jurisdiction->id;
    record.severity_micro = law->severity_micro;
    record.proof_state = CrimeProofState::Unknown;
    record.state = CrimeCaseState::Open;
    record.committed_at = candidate.time;
    if (law->statute_of_limitations.ticks > 0)
        record.expires_at = SaturatingAdd(candidate.time, law->statute_of_limitations);
    record.payload = candidate.payload;

    std::optional<WitnessRecord> initial_witness;
    if (candidate.initial_witness)
    {
        WitnessRecord witness;
        witness.id = WitnessRecordId{witness_ids_.Next()};
        if (!witness.id.IsValid())
            return foundation::Result<CrimeEvaluationResult>::Failure(
                Error("gameplay.crime.id_exhausted", "witness id generator exhausted"));
        witness.crime = record.id;
        witness.witness = candidate.initial_witness->witness;
        witness.witness_type = candidate.initial_witness->witness_type;
        witness.confidence_micro = candidate.initial_witness->confidence_micro;
        witness.observed_at = candidate.initial_witness->observed_at;
        witness.payload = candidate.initial_witness->payload;
        record.witnesses.push_back(witness.id);
        initial_witness = std::move(witness);
        record.proof_state = initial_witness->confidence_micro >= kProofThresholdMicro ? CrimeProofState::Witnessed
                                                                                     : CrimeProofState::Suspected;
    }

    Bump();
    record.revision = revision_;
    auto crime_id = record.id;
    crimes_.emplace(crime_id, record);
    if (initial_witness)
    {
        initial_witness->revision = revision_;
        auto witness_id = initial_witness->id;
        witnesses_.emplace(witness_id, *initial_witness);
        Record({0, CrimeChangeKind::WitnessAdded, crime_id, record.offender, record.victim, record.jurisdiction,
                witness_id, {}, {}, {}, candidate.context, revision_});
    }
    Record({0, CrimeChangeKind::CrimeCandidateCreated, crime_id, record.offender, record.victim, record.jurisdiction,
            {}, {}, {}, {}, candidate.context, revision_});
    Record({0, CrimeChangeKind::CrimeRecorded, crime_id, record.offender, record.victim, record.jurisdiction, {}, {},
            {}, {}, candidate.context, revision_});

    out.crime = crime_id;
    out.proof_state = record.proof_state;
    out.revision = revision_;
    return foundation::Result<CrimeEvaluationResult>::Success(std::move(out));
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

CrimeProofState CrimeService::AggregateProofState(CrimeRecordId crime) const noexcept
{
    bool any_witness = false;
    bool strong_witness = false;
    bool any_evidence = false;
    bool strong_evidence = false;
    for (const auto &[id, witness] : witnesses_)
    {
        (void)id;
        if (witness.crime != crime)
            continue;
        any_witness = any_witness || witness.confidence_micro > 0;
        strong_witness = strong_witness || witness.confidence_micro >= kProofThresholdMicro;
    }
    for (const auto &[id, evidence] : evidence_)
    {
        (void)id;
        if (evidence.crime != crime)
            continue;
        any_evidence = any_evidence || evidence.strength_micro > 0;
        strong_evidence = strong_evidence || evidence.strength_micro >= kProofThresholdMicro;
    }
    if (strong_evidence)
        return CrimeProofState::Confirmed;
    if (any_evidence)
        return CrimeProofState::EvidenceFound;
    if (strong_witness)
        return CrimeProofState::Witnessed;
    if (any_witness)
        return CrimeProofState::Suspected;
    return CrimeProofState::Unknown;
}

void CrimeService::RecalculateProofState(CrimeRecord &crime, GameplayContext context)
{
    if (!IsDerivedProofState(crime.proof_state))
        return;
    const auto next = AggregateProofState(crime.id);
    if (next == crime.proof_state)
        return;
    crime.proof_state = next;
    crime.revision = revision_;
    Record({0, CrimeChangeKind::ProofStateChanged, crime.id, crime.offender, crime.victim, crime.jurisdiction, {}, {},
            {}, {}, context, revision_});
}

foundation::Result<WitnessRecordId> CrimeService::AddWitness(WitnessRecord witness, GameplayContext context)
{
    auto *crime = FindMutableCrime(witness.crime);
    if (!crime || !witness.witness.IsValid() || !witness.witness_type.IsValid() ||
        !ValidMicro(witness.confidence_micro))
        return foundation::Result<WitnessRecordId>::Failure(Error("gameplay.crime.invalid_witness", "invalid witness"));
    for (const auto &[id, existing] : witnesses_)
    {
        if (existing.crime == witness.crime && existing.witness == witness.witness &&
            existing.witness_type == witness.witness_type)
            return foundation::Result<WitnessRecordId>::Success(id);
    }
    if (!witness.id.IsValid())
    {
        witness.id = WitnessRecordId{witness_ids_.Next()};
        if (!witness.id.IsValid())
            return foundation::Result<WitnessRecordId>::Failure(
                Error("gameplay.crime.id_exhausted", "witness id generator exhausted"));
    }
    if (witnesses_.contains(witness.id))
        return foundation::Result<WitnessRecordId>::Failure(
            Error("gameplay.crime.duplicate_witness", "duplicate witness id"));
    AdvanceGeneratorPastAcceptedId(witness_ids_, witness.id);

    Bump();
    witness.revision = revision_;
    auto id = witness.id;
    witnesses_.emplace(id, witness);
    crime->witnesses.push_back(id);
    std::sort(crime->witnesses.begin(), crime->witnesses.end());
    crime->revision = revision_;
    Record({0, CrimeChangeKind::WitnessAdded, witness.crime, crime->offender, crime->victim, crime->jurisdiction, id,
            {}, {}, {}, context, revision_});
    RecalculateProofState(*crime, context);
    return foundation::Result<WitnessRecordId>::Success(id);
}

foundation::Result<void> CrimeService::RemoveWitness(WitnessRecordId id, GameplayContext context)
{
    auto it = witnesses_.find(id);
    if (it == witnesses_.end())
        return foundation::Result<void>::Failure(Error("gameplay.crime.witness_missing", "witness missing"));
    auto *crime = FindMutableCrime(it->second.crime);
    if (!crime)
        return foundation::Result<void>::Failure(Error("gameplay.crime.crime_missing", "crime missing"));
    Bump();
    const auto crime_id = it->second.crime;
    witnesses_.erase(it);
    crime->witnesses.erase(std::remove(crime->witnesses.begin(), crime->witnesses.end(), id), crime->witnesses.end());
    crime->revision = revision_;
    Record({0, CrimeChangeKind::WitnessRemoved, crime_id, crime->offender, crime->victim, crime->jurisdiction, id, {},
            {}, {}, context, revision_});
    RecalculateProofState(*crime, context);
    return foundation::Result<void>::Success();
}

foundation::Result<EvidenceId> CrimeService::AddEvidence(EvidenceRecord evidence, GameplayContext context)
{
    auto *crime = FindMutableCrime(evidence.crime);
    if (!crime || !evidence.source.IsValid() || !evidence.type.IsValid() || !ValidMicro(evidence.strength_micro))
        return foundation::Result<EvidenceId>::Failure(Error("gameplay.crime.invalid_evidence", "invalid evidence"));
    for (const auto &[id, existing] : evidence_)
    {
        if (existing.crime == evidence.crime && existing.source == evidence.source && existing.type == evidence.type)
            return foundation::Result<EvidenceId>::Success(id);
    }
    if (!evidence.id.IsValid())
    {
        evidence.id = EvidenceId{evidence_ids_.Next()};
        if (!evidence.id.IsValid())
            return foundation::Result<EvidenceId>::Failure(
                Error("gameplay.crime.id_exhausted", "evidence id generator exhausted"));
    }
    if (evidence_.contains(evidence.id))
        return foundation::Result<EvidenceId>::Failure(
            Error("gameplay.crime.duplicate_evidence", "duplicate evidence id"));
    AdvanceGeneratorPastAcceptedId(evidence_ids_, evidence.id);

    Bump();
    evidence.revision = revision_;
    auto id = evidence.id;
    evidence_.emplace(id, evidence);
    crime->evidence.push_back(id);
    std::sort(crime->evidence.begin(), crime->evidence.end());
    crime->revision = revision_;
    Record({0, CrimeChangeKind::EvidenceAdded, evidence.crime, crime->offender, crime->victim, crime->jurisdiction, {},
            id, {}, {}, context, revision_});
    RecalculateProofState(*crime, context);
    return foundation::Result<EvidenceId>::Success(id);
}

foundation::Result<void> CrimeService::RemoveEvidence(EvidenceId id, GameplayContext context)
{
    auto it = evidence_.find(id);
    if (it == evidence_.end())
        return foundation::Result<void>::Failure(Error("gameplay.crime.evidence_missing", "evidence missing"));
    auto *crime = FindMutableCrime(it->second.crime);
    if (!crime)
        return foundation::Result<void>::Failure(Error("gameplay.crime.crime_missing", "crime missing"));
    Bump();
    const auto crime_id = it->second.crime;
    evidence_.erase(it);
    crime->evidence.erase(std::remove(crime->evidence.begin(), crime->evidence.end(), id), crime->evidence.end());
    crime->revision = revision_;
    Record({0, CrimeChangeKind::EvidenceRemoved, crime_id, crime->offender, crime->victim, crime->jurisdiction, {}, id,
            {}, {}, context, revision_});
    RecalculateProofState(*crime, context);
    return foundation::Result<void>::Success();
}

foundation::Result<BountyRecordId> CrimeService::CreateBounty(BountyRecord bounty)
{
    if (!bounty.offender.IsValid() || !bounty.jurisdiction.IsValid() || bounty.amount < 0 ||
        !jurisdictions_.contains(bounty.jurisdiction))
        return foundation::Result<BountyRecordId>::Failure(Error("gameplay.crime.invalid_bounty", "invalid bounty"));
    if (bounty.expires_at && bounty.expires_at->ticks < 0)
        return foundation::Result<BountyRecordId>::Failure(Error("gameplay.crime.invalid_bounty", "invalid bounty expiry"));
    std::sort(bounty.source_crimes.begin(), bounty.source_crimes.end());
    bounty.source_crimes.erase(std::unique(bounty.source_crimes.begin(), bounty.source_crimes.end()),
                               bounty.source_crimes.end());
    for (auto crime_id : bounty.source_crimes)
    {
        const auto *crime = FindCrime(crime_id);
        if (!crime || crime->offender != bounty.offender || crime->jurisdiction != bounty.jurisdiction)
            return foundation::Result<BountyRecordId>::Failure(
                Error("gameplay.crime.invalid_bounty_source", "bounty source crime does not match offender/jurisdiction"));
    }
    auto oit = active_bounty_index_.find(bounty.offender);
    if (oit != active_bounty_index_.end() && oit->second.contains(bounty.jurisdiction))
        return foundation::Result<BountyRecordId>::Failure(
            Error("gameplay.crime.active_bounty_exists", "active bounty already exists for offender/jurisdiction"));
    if (!bounty.id.IsValid())
    {
        bounty.id = BountyRecordId{bounty_ids_.Next()};
        if (!bounty.id.IsValid())
            return foundation::Result<BountyRecordId>::Failure(
                Error("gameplay.crime.id_exhausted", "bounty id generator exhausted"));
    }
    if (bounties_.contains(bounty.id))
        return foundation::Result<BountyRecordId>::Failure(
            Error("gameplay.crime.duplicate_bounty", "duplicate bounty id"));
    AdvanceGeneratorPastAcceptedId(bounty_ids_, bounty.id);
    bounty.state = BountyState::Active;
    Bump();
    bounty.revision = revision_;
    auto id = bounty.id;
    bounties_.emplace(id, bounty);
    active_bounty_index_[bounty.offender][bounty.jurisdiction] = id;
    Record({0, CrimeChangeKind::BountyCreated, {}, bounty.offender, {}, bounty.jurisdiction, {}, {}, id, {}, {},
            revision_});
    return foundation::Result<BountyRecordId>::Success(id);
}

foundation::Result<void> CrimeService::ResolveBounty(BountyRecordId id, BountyState state, GameplayContext context)
{
    if (state == BountyState::Active)
        return foundation::Result<void>::Failure(
            Error("gameplay.crime.invalid_bounty_transition", "ResolveBounty requires a terminal bounty state"));
    auto it = bounties_.find(id);
    if (it == bounties_.end())
        return foundation::Result<void>::Failure(Error("gameplay.crime.bounty_missing", "bounty missing"));
    if (it->second.state == state)
        return foundation::Result<void>::Success();
    if (it->second.state != BountyState::Active)
        return foundation::Result<void>::Failure(
            Error("gameplay.crime.invalid_bounty_transition", "bounty is already terminal"));
    Bump();
    it->second.state = state;
    it->second.revision = revision_;
    auto oit = active_bounty_index_.find(it->second.offender);
    if (oit != active_bounty_index_.end())
    {
        oit->second.erase(it->second.jurisdiction);
        if (oit->second.empty())
            active_bounty_index_.erase(oit);
    }
    Record({0, CrimeChangeKind::BountyResolved, {}, it->second.offender, {}, it->second.jurisdiction, {}, {}, id, {},
            context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<LawResponseId> CrimeService::GenerateLawResponse(LawResponseRequest request)
{
    if (!definitions_frozen_)
        return foundation::Result<LawResponseId>::Failure(
            Error("gameplay.crime.definitions_not_frozen", "crime definitions must be frozen before response generation"));
    auto *crime = FindMutableCrime(request.crime);
    const auto *definition = FindResponseDefinition(request.response_type);
    if (!crime || !definition || !request.authority.IsValid())
        return foundation::Result<LawResponseId>::Failure(
            Error("gameplay.crime.invalid_response", "invalid crime, response type, or authority"));
    if (IsTerminalCase(crime->state))
        return foundation::Result<LawResponseId>::Failure(
            Error("gameplay.crime.case_terminal", "cannot generate law response for terminal crime"));

    const AuthorityRecord *matching_authority = nullptr;
    for (const auto &[id, authority] : authorities_)
    {
        (void)id;
        if (authority.authority_group != request.authority || authority.jurisdiction != crime->jurisdiction)
            continue;
        if ((authority.capabilities & definition->required_authority_capabilities) !=
            definition->required_authority_capabilities)
            continue;
        matching_authority = &authority;
        break;
    }
    if (!matching_authority)
        return foundation::Result<LawResponseId>::Failure(
            Error("gameplay.crime.authority_not_allowed", "authority is not allowed to issue requested response"));
    (void)matching_authority;

    LawResponseRecord response;
    response.id = LawResponseId{response_ids_.Next()};
    if (!response.id.IsValid())
        return foundation::Result<LawResponseId>::Failure(
            Error("gameplay.crime.id_exhausted", "law response id generator exhausted"));
    response.crime = request.crime;
    response.response_type = request.response_type;
    response.authority = request.authority;
    response.target = request.target;

    Bump();
    response.revision = revision_;
    auto id = response.id;
    responses_.emplace(id, response);
    Record({0, CrimeChangeKind::LawResponseGenerated, request.crime, crime->offender, crime->victim,
            crime->jurisdiction, {}, {}, {}, id, request.context, revision_});
    if (definition->resulting_case_state && crime->state != *definition->resulting_case_state)
    {
        crime->state = *definition->resulting_case_state;
        crime->revision = revision_;
        Record({0, CrimeChangeKind::CaseStateChanged, request.crime, crime->offender, crime->victim,
                crime->jurisdiction, {}, {}, {}, id, request.context, revision_});
    }
    return foundation::Result<LawResponseId>::Success(id);
}

foundation::Result<void> CrimeService::ChangeCaseState(CrimeRecordId id, CrimeCaseState state, GameplayContext context)
{
    auto *crime = FindMutableCrime(id);
    if (!crime)
        return foundation::Result<void>::Failure(Error("gameplay.crime.crime_missing", "crime missing"));
    if (crime->state == state)
        return foundation::Result<void>::Success();
    if (IsTerminalCase(crime->state))
        return foundation::Result<void>::Failure(
            Error("gameplay.crime.case_terminal", "terminal crime state cannot transition through ChangeCaseState"));
    Bump();
    crime->state = state;
    crime->revision = revision_;
    Record({0, CrimeChangeKind::CaseStateChanged, id, crime->offender, crime->victim, crime->jurisdiction, {}, {}, {},
            {}, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> CrimeService::ChangeProofState(CrimeRecordId id, CrimeProofState state,
                                                        GameplayContext context)
{
    auto *crime = FindMutableCrime(id);
    if (!crime)
        return foundation::Result<void>::Failure(Error("gameplay.crime.crime_missing", "crime missing"));
    if (IsDerivedProofState(state) && state != AggregateProofState(id))
        return foundation::Result<void>::Failure(
            Error("gameplay.crime.proof_is_derived", "derived proof state must match active witness/evidence aggregate"));
    if (crime->proof_state == state)
        return foundation::Result<void>::Success();
    Bump();
    crime->proof_state = state;
    crime->revision = revision_;
    Record({0, CrimeChangeKind::ProofStateChanged, id, crime->offender, crime->victim, crime->jurisdiction, {}, {}, {},
            {}, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> CrimeService::ExpireCrime(CrimeRecordId id, GameplayContext context)
{
    auto *crime = FindMutableCrime(id);
    if (!crime)
        return foundation::Result<void>::Failure(Error("gameplay.crime.crime_missing", "crime missing"));
    if (crime->state == CrimeCaseState::Expired)
        return foundation::Result<void>::Success();
    if (IsTerminalCase(crime->state))
        return foundation::Result<void>::Failure(
            Error("gameplay.crime.case_terminal", "terminal crime cannot be expired again"));
    Bump();
    crime->state = CrimeCaseState::Expired;
    crime->revision = revision_;
    ++diagnostics_.expired_crimes;
    Record({0, CrimeChangeKind::CrimeExpired, id, crime->offender, crime->victim, crime->jurisdiction, {}, {}, {}, {},
            context, revision_});
    return foundation::Result<void>::Success();
}

std::size_t CrimeService::ExpireDue(GameplayTimePoint now, GameplayContext context)
{
    context.time = now;
    std::vector<CrimeRecordId> due_crimes;
    std::vector<BountyRecordId> due_bounties;
    for (const auto &[id, crime] : crimes_)
    {
        if (!IsTerminalCase(crime.state) && crime.expires_at && crime.expires_at->ticks <= now.ticks)
            due_crimes.push_back(id);
    }
    for (const auto &[id, bounty] : bounties_)
    {
        if (bounty.state == BountyState::Active && bounty.expires_at && bounty.expires_at->ticks <= now.ticks)
            due_bounties.push_back(id);
    }
    std::sort(due_crimes.begin(), due_crimes.end());
    std::sort(due_bounties.begin(), due_bounties.end());
    std::size_t changed = 0;
    for (auto id : due_crimes)
        if (ExpireCrime(id, context))
            ++changed;
    for (auto id : due_bounties)
        if (ResolveBounty(id, BountyState::Expired, context))
            ++changed;
    return changed;
}

foundation::Result<void> CrimeService::PruneTerminalCrime(CrimeRecordId id, GameplayContext context)
{
    auto it = crimes_.find(id);
    if (it == crimes_.end())
        return foundation::Result<void>::Failure(Error("gameplay.crime.crime_missing", "crime missing"));
    if (!IsTerminalCase(it->second.state))
        return foundation::Result<void>::Failure(
            Error("gameplay.crime.case_not_terminal", "only terminal crimes may be pruned"));
    for (const auto &[bid, bounty] : bounties_)
    {
        (void)bid;
        if (bounty.state == BountyState::Active && ContainsId(bounty.source_crimes, id))
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.active_bounty_reference", "active bounty still references crime"));
    }

    const auto offender = it->second.offender;
    const auto victim = it->second.victim;
    const auto jurisdiction = it->second.jurisdiction;
    Bump();
    for (auto witness_id : it->second.witnesses)
        witnesses_.erase(witness_id);
    for (auto evidence_id : it->second.evidence)
        evidence_.erase(evidence_id);
    for (auto rit = responses_.begin(); rit != responses_.end();)
        rit = rit->second.crime == id ? responses_.erase(rit) : std::next(rit);
    for (auto &[bid, bounty] : bounties_)
    {
        (void)bid;
        bounty.source_crimes.erase(std::remove(bounty.source_crimes.begin(), bounty.source_crimes.end(), id),
                                   bounty.source_crimes.end());
    }
    crimes_.erase(it);
    Record({0, CrimeChangeKind::CrimePruned, id, offender, victim, jurisdiction, {}, {}, {}, {}, context, revision_});
    return foundation::Result<void>::Success();
}

LegalityPreview CrimeService::PreviewLegality(GameplayObjectRef offender, CrimeTypeId type,
                                              GameplayObjectRef area) const
{
    (void)offender;
    LegalityPreview out;
    out.crime_type = type;
    out.revision = revision_;
    auto jurisdiction = GetApplicableJurisdiction(area);
    if (!jurisdiction)
        return out;
    auto law = FindLawFor(type, jurisdiction->id);
    if (!law)
        return out;
    out.illegal = true;
    out.jurisdiction = jurisdiction->id;
    out.severity_micro = law->severity_micro;
    return out;
}

std::vector<CrimeRecord> CrimeService::FindCrimesByOffender(GameplayObjectRef offender) const
{
    std::vector<CrimeRecord> out;
    for (const auto &[id, crime] : crimes_)
    {
        (void)id;
        if (crime.offender == offender)
            out.push_back(crime);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}

std::vector<CrimeRecord> CrimeService::FindCrimesByVictim(GameplayObjectRef victim) const
{
    std::vector<CrimeRecord> out;
    for (const auto &[id, crime] : crimes_)
    {
        (void)id;
        if (crime.victim == victim)
            out.push_back(crime);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}

std::vector<CrimeRecord> CrimeService::FindCrimesByJurisdiction(JurisdictionId jurisdiction) const
{
    std::vector<CrimeRecord> out;
    for (const auto &[id, crime] : crimes_)
    {
        (void)id;
        if (crime.jurisdiction == jurisdiction)
            out.push_back(crime);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}

std::vector<CrimeRecord> CrimeService::FindOpenCases() const
{
    std::vector<CrimeRecord> out;
    for (const auto &[id, crime] : crimes_)
    {
        (void)id;
        if (crime.state == CrimeCaseState::Open || crime.state == CrimeCaseState::Reported ||
            crime.state == CrimeCaseState::UnderInvestigation || crime.state == CrimeCaseState::Wanted)
            out.push_back(crime);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}

std::vector<WitnessRecord> CrimeService::FindWitnesses(CrimeRecordId crime) const
{
    std::vector<WitnessRecord> out;
    for (const auto &[id, witness] : witnesses_)
    {
        (void)id;
        if (witness.crime == crime)
            out.push_back(witness);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}

std::vector<EvidenceRecord> CrimeService::FindEvidence(CrimeRecordId crime) const
{
    std::vector<EvidenceRecord> out;
    for (const auto &[id, evidence] : evidence_)
    {
        (void)id;
        if (evidence.crime == crime)
            out.push_back(evidence);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}

std::optional<BountyRecord> CrimeService::GetBounty(GameplayObjectRef offender, JurisdictionId jurisdiction) const
{
    auto oit = active_bounty_index_.find(offender);
    if (oit == active_bounty_index_.end())
        return std::nullopt;
    auto jit = oit->second.find(jurisdiction);
    if (jit == oit->second.end())
        return std::nullopt;
    auto bit = bounties_.find(jit->second);
    if (bit == bounties_.end() || bit->second.state != BountyState::Active)
        return std::nullopt;
    return bit->second;
}

std::vector<CrimeChange> CrimeService::ChangesSince(std::uint64_t sequence) const
{
    return ReadChangesSince(sequence).changes;
}

CrimeChangeBatch CrimeService::ReadChangesSince(std::uint64_t sequence) const
{
    CrimeChangeBatch batch;
    batch.latest_sequence = LatestChangeSequence();
    batch.oldest_available_sequence = changes_.empty() ? 0 : changes_.front().sequence;
    if (changes_.empty())
    {
        batch.snapshot_required = sequence < batch.latest_sequence;
        return batch;
    }
    if (changes_.front().sequence > 1 && sequence < changes_.front().sequence - 1)
    {
        batch.snapshot_required = true;
        return batch;
    }
    for (const auto &change : changes_)
        if (change.sequence > sequence)
            batch.changes.push_back(change);
    return batch;
}

void CrimeService::PruneChangesThrough(std::uint64_t sequence)
{
    while (!changes_.empty() && changes_.front().sequence <= sequence)
        changes_.pop_front();
}

void CrimeService::SetChangeJournalCapacity(std::size_t capacity) noexcept
{
    change_journal_capacity_ = capacity;
    while (changes_.size() > change_journal_capacity_)
        changes_.pop_front();
}

CrimeSnapshot CrimeService::CaptureSnapshot() const
{
    CrimeSnapshot snapshot;
    for (const auto &[id, law] : laws_)
    {
        (void)id;
        snapshot.laws.push_back(law);
    }
    for (const auto &[id, definition] : response_definitions_)
    {
        (void)id;
        snapshot.response_definitions.push_back(definition);
    }
    for (const auto &[id, jurisdiction] : jurisdictions_)
    {
        (void)id;
        snapshot.jurisdictions.push_back(jurisdiction);
    }
    for (const auto &[id, authority] : authorities_)
    {
        (void)id;
        snapshot.authorities.push_back(authority);
    }
    for (const auto &[id, crime] : crimes_)
    {
        (void)id;
        snapshot.crimes.push_back(crime);
    }
    for (const auto &[id, witness] : witnesses_)
    {
        (void)id;
        snapshot.witnesses.push_back(witness);
    }
    for (const auto &[id, evidence] : evidence_)
    {
        (void)id;
        snapshot.evidence.push_back(evidence);
    }
    for (const auto &[id, bounty] : bounties_)
    {
        (void)id;
        snapshot.bounties.push_back(bounty);
    }
    for (const auto &[id, response] : responses_)
    {
        (void)id;
        snapshot.responses.push_back(response);
    }
    std::sort(snapshot.laws.begin(), snapshot.laws.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.response_definitions.begin(), snapshot.response_definitions.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.jurisdictions.begin(), snapshot.jurisdictions.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.authorities.begin(), snapshot.authorities.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.crimes.begin(), snapshot.crimes.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.witnesses.begin(), snapshot.witnesses.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.evidence.begin(), snapshot.evidence.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.bounties.begin(), snapshot.bounties.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.responses.begin(), snapshot.responses.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    snapshot.crime_ids = crime_ids_.GetSnapshot();
    snapshot.witness_ids = witness_ids_.GetSnapshot();
    snapshot.evidence_ids = evidence_ids_.GetSnapshot();
    snapshot.bounty_ids = bounty_ids_.GetSnapshot();
    snapshot.authority_ids = authority_ids_.GetSnapshot();
    snapshot.response_ids = response_ids_.GetSnapshot();
    snapshot.revision = revision_;
    snapshot.definitions_frozen = definitions_frozen_;
    return snapshot;
}

foundation::Result<void> CrimeService::RestoreSnapshot(CrimeSnapshot snapshot)
{
    using LawMap = decltype(laws_);
    using ResponseDefinitionMap = decltype(response_definitions_);
    using JurisdictionMap = decltype(jurisdictions_);
    using AuthorityMap = decltype(authorities_);
    using CrimeMap = decltype(crimes_);
    using WitnessMap = decltype(witnesses_);
    using EvidenceMap = decltype(evidence_);
    using BountyMap = decltype(bounties_);
    using ResponseMap = decltype(responses_);

    LawMap laws;
    ResponseDefinitionMap response_definitions;
    JurisdictionMap jurisdictions;
    AuthorityMap authorities;
    CrimeMap crimes;
    WitnessMap witnesses;
    EvidenceMap evidence;
    BountyMap bounties;
    ResponseMap responses;

    for (auto &law : snapshot.laws)
    {
        if (!law.id.IsValid() || !law.crime_type.IsValid() || law.severity_micro < 0 || law.default_bounty < 0 ||
            law.statute_of_limitations.ticks < 0 || law.revision.value > snapshot.revision.value ||
            !laws.emplace(law.id, law).second)
            return foundation::Result<void>::Failure(Error("gameplay.crime.restore_invalid", "invalid law snapshot"));
    }
    for (auto &definition : snapshot.response_definitions)
    {
        if (!definition.id.IsValid() || definition.revision.value > snapshot.revision.value ||
            !response_definitions.emplace(definition.id, definition).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.restore_invalid", "invalid response definition snapshot"));
    }
    for (auto &jurisdiction : snapshot.jurisdictions)
    {
        if (!jurisdiction.id.IsValid() || !jurisdiction.area.IsValid() || !jurisdiction.authority_group.IsValid() ||
            jurisdiction.revision.value > snapshot.revision.value)
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.restore_invalid", "invalid jurisdiction snapshot"));
        std::sort(jurisdiction.active_laws.begin(), jurisdiction.active_laws.end());
        if (std::adjacent_find(jurisdiction.active_laws.begin(), jurisdiction.active_laws.end()) !=
            jurisdiction.active_laws.end())
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.restore_invalid", "duplicate jurisdiction law"));
        for (auto law : jurisdiction.active_laws)
            if (!laws.contains(law))
                return foundation::Result<void>::Failure(
                    Error("gameplay.crime.restore_invalid", "jurisdiction references missing law"));
        if (!jurisdictions.emplace(jurisdiction.id, jurisdiction).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.restore_invalid", "duplicate jurisdiction snapshot"));
    }
    for (auto &authority : snapshot.authorities)
    {
        if (!authority.id.IsValid() || !authority.authority_group.IsValid() ||
            !jurisdictions.contains(authority.jurisdiction) || authority.revision.value > snapshot.revision.value ||
            !authorities.emplace(authority.id, authority).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.restore_invalid", "invalid authority snapshot"));
    }
    for (const auto &[id, authority] : authorities)
    {
        for (const auto &[other_id, other] : authorities)
        {
            if (id < other_id && authority.authority_group == other.authority_group &&
                authority.jurisdiction == other.jurisdiction)
                return foundation::Result<void>::Failure(
                    Error("gameplay.crime.restore_invalid", "duplicate authority semantic key"));
        }
    }

    auto temp_find_law = [&](CrimeTypeId type, JurisdictionId jurisdiction) -> const LawDefinition * {
        auto jit = jurisdictions.find(jurisdiction);
        if (jit == jurisdictions.end())
            return nullptr;
        for (auto law_id : jit->second.active_laws)
        {
            auto lit = laws.find(law_id);
            if (lit != laws.end() && lit->second.crime_type == type)
                return &lit->second;
        }
        return nullptr;
    };

    for (auto &crime : snapshot.crimes)
    {
        if (!crime.id.IsValid() || !crime.type.IsValid() || !crime.offender.IsValid() ||
            !jurisdictions.contains(crime.jurisdiction) || !temp_find_law(crime.type, crime.jurisdiction) ||
            crime.severity_micro < 0 || crime.revision.value > snapshot.revision.value ||
            !crimes.emplace(crime.id, crime).second)
            return foundation::Result<void>::Failure(Error("gameplay.crime.restore_invalid", "invalid crime snapshot"));
    }

    for (auto &witness : snapshot.witnesses)
    {
        if (!witness.id.IsValid() || !crimes.contains(witness.crime) || !witness.witness.IsValid() ||
            !witness.witness_type.IsValid() || !ValidMicro(witness.confidence_micro) ||
            witness.revision.value > snapshot.revision.value || !witnesses.emplace(witness.id, witness).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.restore_invalid", "invalid witness snapshot"));
    }
    for (const auto &[id, witness] : witnesses)
    {
        for (const auto &[other_id, other] : witnesses)
        {
            if (id < other_id && witness.crime == other.crime && witness.witness == other.witness &&
                witness.witness_type == other.witness_type)
                return foundation::Result<void>::Failure(
                    Error("gameplay.crime.restore_invalid", "duplicate witness semantic key"));
        }
    }

    for (auto &item : snapshot.evidence)
    {
        if (!item.id.IsValid() || !crimes.contains(item.crime) || !item.source.IsValid() || !item.type.IsValid() ||
            !ValidMicro(item.strength_micro) || item.revision.value > snapshot.revision.value ||
            !evidence.emplace(item.id, item).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.restore_invalid", "invalid evidence snapshot"));
    }
    for (const auto &[id, item] : evidence)
    {
        for (const auto &[other_id, other] : evidence)
        {
            if (id < other_id && item.crime == other.crime && item.source == other.source && item.type == other.type)
                return foundation::Result<void>::Failure(
                    Error("gameplay.crime.restore_invalid", "duplicate evidence semantic key"));
        }
    }

    for (auto &[crime_id, crime] : crimes)
    {
        std::vector<WitnessRecordId> expected_witnesses;
        std::vector<EvidenceId> expected_evidence;
        for (const auto &[id, witness] : witnesses)
            if (witness.crime == crime_id)
                expected_witnesses.push_back(id);
        for (const auto &[id, item] : evidence)
            if (item.crime == crime_id)
                expected_evidence.push_back(id);
        std::sort(expected_witnesses.begin(), expected_witnesses.end());
        std::sort(expected_evidence.begin(), expected_evidence.end());
        auto stored_witnesses = crime.witnesses;
        auto stored_evidence = crime.evidence;
        std::sort(stored_witnesses.begin(), stored_witnesses.end());
        std::sort(stored_evidence.begin(), stored_evidence.end());
        if (std::adjacent_find(stored_witnesses.begin(), stored_witnesses.end()) != stored_witnesses.end() ||
            std::adjacent_find(stored_evidence.begin(), stored_evidence.end()) != stored_evidence.end() ||
            stored_witnesses != expected_witnesses || stored_evidence != expected_evidence)
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.restore_invalid", "crime evidence references do not match records"));
        crime.witnesses = std::move(stored_witnesses);
        crime.evidence = std::move(stored_evidence);
        if (IsDerivedProofState(crime.proof_state))
        {
            bool any_witness = false, strong_witness = false, any_evidence = false, strong_evidence = false;
            for (auto id : crime.witnesses)
            {
                const auto &w = witnesses.at(id);
                any_witness = any_witness || w.confidence_micro > 0;
                strong_witness = strong_witness || w.confidence_micro >= kProofThresholdMicro;
            }
            for (auto id : crime.evidence)
            {
                const auto &e = evidence.at(id);
                any_evidence = any_evidence || e.strength_micro > 0;
                strong_evidence = strong_evidence || e.strength_micro >= kProofThresholdMicro;
            }
            const auto derived = strong_evidence   ? CrimeProofState::Confirmed
                                 : any_evidence     ? CrimeProofState::EvidenceFound
                                 : strong_witness   ? CrimeProofState::Witnessed
                                 : any_witness      ? CrimeProofState::Suspected
                                                    : CrimeProofState::Unknown;
            if (crime.proof_state != derived)
                return foundation::Result<void>::Failure(
                    Error("gameplay.crime.restore_invalid", "crime proof state does not match active evidence"));
        }
    }

    std::unordered_map<GameplayObjectRef, std::unordered_map<JurisdictionId, BountyRecordId, IdHash>> active_bounty_index;
    for (auto &bounty : snapshot.bounties)
    {
        if (!bounty.id.IsValid() || !bounty.offender.IsValid() || !jurisdictions.contains(bounty.jurisdiction) ||
            bounty.amount < 0 || bounty.revision.value > snapshot.revision.value)
            return foundation::Result<void>::Failure(Error("gameplay.crime.restore_invalid", "invalid bounty snapshot"));
        std::sort(bounty.source_crimes.begin(), bounty.source_crimes.end());
        if (std::adjacent_find(bounty.source_crimes.begin(), bounty.source_crimes.end()) != bounty.source_crimes.end())
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.restore_invalid", "duplicate bounty source crime"));
        for (auto crime_id : bounty.source_crimes)
        {
            auto cit = crimes.find(crime_id);
            if (cit == crimes.end() || cit->second.offender != bounty.offender ||
                cit->second.jurisdiction != bounty.jurisdiction)
                return foundation::Result<void>::Failure(
                    Error("gameplay.crime.restore_invalid", "invalid bounty source crime"));
        }
        if (!bounties.emplace(bounty.id, bounty).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.restore_invalid", "duplicate bounty snapshot"));
        if (bounty.state == BountyState::Active)
        {
            auto &by_jurisdiction = active_bounty_index[bounty.offender];
            if (!by_jurisdiction.emplace(bounty.jurisdiction, bounty.id).second)
                return foundation::Result<void>::Failure(
                    Error("gameplay.crime.restore_invalid", "multiple active bounties for offender/jurisdiction"));
        }
    }

    for (auto &response : snapshot.responses)
    {
        if (!response.id.IsValid() || !crimes.contains(response.crime) || !response.response_type.IsValid() ||
            !response_definitions.contains(response.response_type) || !response.authority.IsValid() ||
            response.revision.value > snapshot.revision.value || !responses.emplace(response.id, response).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.restore_invalid", "invalid response snapshot"));
        const auto &crime = crimes.at(response.crime);
        const auto &definition = response_definitions.at(response.response_type);
        bool authority_ok = false;
        for (const auto &[id, authority] : authorities)
        {
            (void)id;
            if (authority.authority_group == response.authority && authority.jurisdiction == crime.jurisdiction &&
                (authority.capabilities & definition.required_authority_capabilities) ==
                    definition.required_authority_capabilities)
            {
                authority_ok = true;
                break;
            }
        }
        if (!authority_ok)
            return foundation::Result<void>::Failure(
                Error("gameplay.crime.restore_invalid", "response authority is not valid for crime jurisdiction"));
    }

    std::uint64_t max_crime = 0, max_witness = 0, max_evidence = 0, max_bounty = 0, max_authority = 0, max_response = 0;
    const auto crime_scope = crime_ids_.Scope().Raw();
    const auto witness_scope = witness_ids_.Scope().Raw();
    const auto evidence_scope = evidence_ids_.Scope().Raw();
    const auto bounty_scope = bounty_ids_.Scope().Raw();
    const auto authority_scope = authority_ids_.Scope().Raw();
    const auto response_scope = response_ids_.Scope().Raw();
    for (const auto &[id, value] : crimes)
    {
        (void)value;
        TrackMaxLowForScope(id, crime_scope, max_crime);
    }
    for (const auto &[id, value] : witnesses)
    {
        (void)value;
        TrackMaxLowForScope(id, witness_scope, max_witness);
    }
    for (const auto &[id, value] : evidence)
    {
        (void)value;
        TrackMaxLowForScope(id, evidence_scope, max_evidence);
    }
    for (const auto &[id, value] : bounties)
    {
        (void)value;
        TrackMaxLowForScope(id, bounty_scope, max_bounty);
    }
    for (const auto &[id, value] : authorities)
    {
        (void)value;
        TrackMaxLowForScope(id, authority_scope, max_authority);
    }
    for (const auto &[id, value] : responses)
    {
        (void)value;
        TrackMaxLowForScope(id, response_scope, max_response);
    }
    if (!ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(snapshot.crime_ids, crime_ids_.Scope(), max_crime) ||
        !ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(snapshot.witness_ids, witness_ids_.Scope(), max_witness) ||
        !ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(snapshot.evidence_ids, evidence_ids_.Scope(), max_evidence) ||
        !ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(snapshot.bounty_ids, bounty_ids_.Scope(), max_bounty) ||
        !ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(snapshot.authority_ids, authority_ids_.Scope(), max_authority) ||
        !ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(snapshot.response_ids, response_ids_.Scope(), max_response))
        return foundation::Result<void>::Failure(
            Error("gameplay.crime.restore_invalid", "invalid id generator snapshot"));

    laws_ = std::move(laws);
    response_definitions_ = std::move(response_definitions);
    jurisdictions_ = std::move(jurisdictions);
    authorities_ = std::move(authorities);
    crimes_ = std::move(crimes);
    witnesses_ = std::move(witnesses);
    evidence_ = std::move(evidence);
    bounties_ = std::move(bounties);
    responses_ = std::move(responses);
    active_bounty_index_ = std::move(active_bounty_index);
    crime_ids_.Restore(snapshot.crime_ids);
    witness_ids_.Restore(snapshot.witness_ids);
    evidence_ids_.Restore(snapshot.evidence_ids);
    bounty_ids_.Restore(snapshot.bounty_ids);
    authority_ids_.Restore(snapshot.authority_ids);
    response_ids_.Restore(snapshot.response_ids);
    revision_ = snapshot.revision;
    definitions_frozen_ = snapshot.definitions_frozen;
    changes_.clear();
    next_change_sequence_ = 1;
    diagnostics_ = {};
    return foundation::Result<void>::Success();
}

void CrimeService::RebuildDerivedIndexes()
{
    active_bounty_index_.clear();
    for (const auto &[id, bounty] : bounties_)
        if (bounty.state == BountyState::Active)
            active_bounty_index_[bounty.offender][bounty.jurisdiction] = id;
}

CrimeDiagnostics CrimeService::GetDiagnostics() const noexcept
{
    auto diagnostics = diagnostics_;
    diagnostics.crime_records = crimes_.size();
    diagnostics.open_cases = FindOpenCases().size();
    diagnostics.wanted_subjects = 0;
    for (const auto &[id, crime] : crimes_)
    {
        (void)id;
        if (crime.state == CrimeCaseState::Wanted)
            ++diagnostics.wanted_subjects;
    }
    diagnostics.witnesses = witnesses_.size();
    diagnostics.evidence_records = evidence_.size();
    diagnostics.bounties = bounties_.size();
    diagnostics.law_responses = responses_.size();
    return diagnostics;
}

void CrimeService::Record(CrimeChange change)
{
    change.sequence = next_change_sequence_++;
    changes_.push_back(std::move(change));
    while (changes_.size() > change_journal_capacity_)
        changes_.pop_front();
}
} // namespace epidemic::gameplay::crime
