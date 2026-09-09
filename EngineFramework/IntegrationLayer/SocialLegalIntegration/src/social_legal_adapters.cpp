#include "Epidemic/GameFramework/SocialLegalIntegration/social_legal_adapters.h"
#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace epidemic::gameplay::social_legal_integration
{
namespace
{
foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(std::string(code), std::string(message));
}

bool IsViolation(const ownership::PermissionDecision decision, const PropertyCrimeMapping& mapping) noexcept
{
    switch (decision)
    {
    case ownership::PermissionDecision::Denied:
        return mapping.denied_is_violation;
    case ownership::PermissionDecision::Trespass:
        return mapping.trespass_is_violation;
    case ownership::PermissionDecision::CrimeCandidate:
        return mapping.crime_candidate_is_violation;
    case ownership::PermissionDecision::Allowed:
    case ownership::PermissionDecision::Unknown:
    case ownership::PermissionDecision::NotApplicable:
        return false;
    }
    return false;
}

foundation::Result<GameplayObjectRef> ResolveVictim(const PropertyCrimeMapping& mapping,
                                                     const ownership::OwnershipPermissionResult& permission)
{
    switch (mapping.victim_policy)
    {
    case PropertyCrimeVictimPolicy::PropertyOwnerRequired:
        if (!permission.effective_owner.IsValid())
        {
            return foundation::Result<GameplayObjectRef>::Failure(
                Error("gameplay.social_legal.required_property_owner_missing",
                      "property crime mapping requires an effective property owner"));
        }
        return foundation::Result<GameplayObjectRef>::Success(permission.effective_owner);
    case PropertyCrimeVictimPolicy::PropertyOwnerOptional:
        return foundation::Result<GameplayObjectRef>::Success(permission.effective_owner);
    case PropertyCrimeVictimPolicy::PublicSubjectRequired:
        if (!mapping.public_subject.IsValid())
        {
            return foundation::Result<GameplayObjectRef>::Failure(
                Error("gameplay.social_legal.public_subject_missing",
                      "public-subject victim policy requires a valid public subject"));
        }
        return foundation::Result<GameplayObjectRef>::Success(mapping.public_subject);
    case PropertyCrimeVictimPolicy::Victimless:
        return foundation::Result<GameplayObjectRef>::Success(GameplayObjectRef{});
    }
    return foundation::Result<GameplayObjectRef>::Failure(
        Error("gameplay.social_legal.victim_policy_invalid", "unsupported property crime victim policy"));
}

template <typename T>
bool RelationshipDefinitionExists(const T& society, society::RelationshipTypeId id)
{
    if constexpr (requires(const T& service, society::RelationshipTypeId type) { service.GetRelationshipType(type); })
    {
        return society.GetRelationshipType(id) != nullptr;
    }
    else
    {
        // Older Society revisions had no relationship-type registry. The mapping can only
        // validate the stable ID until Society is upgraded to the frozen definition contract.
        return id.IsValid();
    }
}
} // namespace

SocialLegalMappings::SocialLegalMappings(const crime::CrimeService& crime, const society::SocietyService& society) noexcept
    : crime_(crime), society_(society)
{
}

foundation::Result<void> SocialLegalMappings::RegisterPropertyCrimeMapping(PropertyCrimeMapping mapping)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.social_legal.mappings_frozen", "mappings are frozen"));
    if (!mapping.id.IsValid() || !mapping.right.IsValid() || !mapping.crime_type.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.social_legal.mapping_invalid", "property crime mapping IDs must be valid"));
    if (mapping.victim_policy == PropertyCrimeVictimPolicy::PublicSubjectRequired && !mapping.public_subject.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.social_legal.mapping_invalid", "public victim subject must be valid"));
    if (!mapping.denied_is_violation && !mapping.trespass_is_violation && !mapping.crime_candidate_is_violation)
        return foundation::Result<void>::Failure(Error("gameplay.social_legal.mapping_invalid", "property crime mapping has no violation-bearing decision"));
    if (property_crime_.contains(mapping.id) || crime_relationship_.contains(mapping.id) || authority_response_.contains(mapping.id))
        return foundation::Result<void>::Failure(Error("gameplay.social_legal.mapping_duplicate", "mapping id already registered"));
    property_crime_.emplace(mapping.id, std::move(mapping));
    Bump();
    return foundation::Result<void>::Success();
}

foundation::Result<void> SocialLegalMappings::RegisterCrimeRelationshipMapping(CrimeRelationshipConsequenceMapping mapping)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.social_legal.mappings_frozen", "mappings are frozen"));
    if (!mapping.id.IsValid() || !mapping.crime_type.IsValid() || !mapping.relationship_type.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.social_legal.mapping_invalid", "crime relationship mapping IDs must be valid"));
    if (property_crime_.contains(mapping.id) || crime_relationship_.contains(mapping.id) || authority_response_.contains(mapping.id))
        return foundation::Result<void>::Failure(Error("gameplay.social_legal.mapping_duplicate", "mapping id already registered"));
    crime_relationship_.emplace(mapping.id, std::move(mapping));
    Bump();
    return foundation::Result<void>::Success();
}

foundation::Result<void> SocialLegalMappings::RegisterAuthorityResponseMapping(AuthorityResponseMapping mapping)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.social_legal.mappings_frozen", "mappings are frozen"));
    if (!mapping.id.IsValid() || !mapping.crime_type.IsValid() || !mapping.response_type.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.social_legal.mapping_invalid", "authority response mapping IDs must be valid"));
    if (property_crime_.contains(mapping.id) || crime_relationship_.contains(mapping.id) || authority_response_.contains(mapping.id))
        return foundation::Result<void>::Failure(Error("gameplay.social_legal.mapping_duplicate", "mapping id already registered"));
    authority_response_.emplace(mapping.id, std::move(mapping));
    Bump();
    return foundation::Result<void>::Success();
}

bool SocialLegalMappings::CrimeTypeRegistered(crime::CrimeTypeId id) const
{
    const auto snapshot = crime_.CaptureSnapshot();
    return std::any_of(snapshot.laws.begin(), snapshot.laws.end(),
                       [id](const crime::LawDefinition& law) { return law.crime_type == id; });
}

bool SocialLegalMappings::RelationshipTypeRegistered(society::RelationshipTypeId id) const
{
    return RelationshipDefinitionExists(society_, id);
}

foundation::Result<void> SocialLegalMappings::Freeze()
{
    if (frozen_)
        return foundation::Result<void>::Success();

    for (const auto& [id, mapping] : property_crime_)
    {
        (void)id;
        if (!CrimeTypeRegistered(mapping.crime_type))
            return foundation::Result<void>::Failure(Error("gameplay.social_legal.crime_type_missing", "property crime mapping references an unregistered crime type"));
    }
    for (const auto& [id, mapping] : crime_relationship_)
    {
        (void)id;
        if (!CrimeTypeRegistered(mapping.crime_type))
            return foundation::Result<void>::Failure(Error("gameplay.social_legal.crime_type_missing", "relationship consequence mapping references an unregistered crime type"));
        if (!RelationshipTypeRegistered(mapping.relationship_type))
            return foundation::Result<void>::Failure(Error("gameplay.social_legal.relationship_type_missing", "relationship consequence mapping references an unregistered relationship type"));
    }
    for (const auto& [id, mapping] : authority_response_)
    {
        (void)id;
        if (!CrimeTypeRegistered(mapping.crime_type))
            return foundation::Result<void>::Failure(Error("gameplay.social_legal.crime_type_missing", "authority response mapping references an unregistered crime type"));
    }

    frozen_ = true;
    Bump();
    return foundation::Result<void>::Success();
}

const PropertyCrimeMapping* SocialLegalMappings::FindPropertyCrimeMapping(SocialLegalMappingId id) const noexcept
{
    const auto it = property_crime_.find(id);
    return it == property_crime_.end() ? nullptr : &it->second;
}

const CrimeRelationshipConsequenceMapping* SocialLegalMappings::FindCrimeRelationshipMapping(SocialLegalMappingId id) const noexcept
{
    const auto it = crime_relationship_.find(id);
    return it == crime_relationship_.end() ? nullptr : &it->second;
}

const AuthorityResponseMapping* SocialLegalMappings::FindAuthorityResponseMapping(SocialLegalMappingId id) const noexcept
{
    const auto it = authority_response_.find(id);
    return it == authority_response_.end() ? nullptr : &it->second;
}

OwnershipCrimeAdapter::OwnershipCrimeAdapter(ownership::OwnershipService& ownership, crime::CrimeService& crime,
                                             const SocialLegalMappings& mappings) noexcept
    : ownership_(ownership), crime_(crime), mappings_(mappings)
{
}

foundation::Result<TheftResolution> OwnershipCrimeAdapter::TryCreateCrimeFromDeniedRight(
    GameplayObjectRef actor, GameplayObjectRef property, SocialLegalMappingId mapping_id, GameplayObjectRef area,
    GameplayContext context)
{
    if (!mappings_.IsFrozen())
        return foundation::Result<TheftResolution>::Failure(Error("gameplay.social_legal.mappings_not_frozen", "social/legal mappings must be frozen before evaluation"));
    const auto* mapping = mappings_.FindPropertyCrimeMapping(mapping_id);
    if (!mapping)
        return foundation::Result<TheftResolution>::Failure(Error("gameplay.social_legal.mapping_missing", "property crime mapping missing"));

    auto permission = ownership_.EvaluatePermission({actor, property, mapping->right, context});
    TheftResolution out;
    out.permission = permission;
    if (!IsViolation(permission.decision, *mapping))
        return foundation::Result<TheftResolution>::Success(out);

    auto victim = ResolveVictim(*mapping, permission);
    if (!victim)
    {
        out.disposition = PropertyCrimeDisposition::MissingRequiredVictim;
        return foundation::Result<TheftResolution>::Success(out);
    }

    crime::CrimeCandidate candidate;
    candidate.type = mapping->crime_type;
    candidate.offender = actor;
    candidate.victim = victim.Value();
    candidate.target_property = property;
    candidate.area = area;
    candidate.time = context.time;
    candidate.context = context;

    auto created = crime_.EvaluateCrimeCandidate(candidate, crime::CrimeCandidatePolicy::RecordAlways);
    if (!created)
        return foundation::Result<TheftResolution>::Failure(std::move(created.GetError()));
    out.crime = created.Value().crime;
    out.disposition = out.crime ? PropertyCrimeDisposition::CrimeRecorded : PropertyCrimeDisposition::NoViolation;
    return foundation::Result<TheftResolution>::Success(out);
}

CrimeSocietyAdapter::CrimeSocietyAdapter(crime::CrimeService& crime, society::SocietyService& society,
                                         const SocialLegalMappings& mappings) noexcept
    : crime_(crime), society_(society), mappings_(mappings)
{
}

foundation::Result<void> CrimeSocietyAdapter::ApplyRelationshipPenalty(crime::CrimeRecordId crime_id,
                                                                        SocialLegalMappingId mapping_id,
                                                                        GameplayContext context)
{
    if (!mappings_.IsFrozen())
        return foundation::Result<void>::Failure(Error("gameplay.social_legal.mappings_not_frozen", "social/legal mappings must be frozen before delivery"));
    const auto* mapping = mappings_.FindCrimeRelationshipMapping(mapping_id);
    if (!mapping)
        return foundation::Result<void>::Failure(Error("gameplay.social_legal.mapping_missing", "crime relationship mapping missing"));

    const RelationshipPenaltyDelivery delivery{crime_id, mapping_id};
    if (applied_relationship_penalties_.contains(delivery))
        return foundation::Result<void>::Success();

    const auto* record = crime_.FindCrime(crime_id);
    if (!record)
        return foundation::Result<void>::Failure(Error("gameplay.social_legal.crime_missing", "crime missing"));
    if (record->type != mapping->crime_type)
        return foundation::Result<void>::Failure(Error("gameplay.social_legal.crime_type_mismatch", "crime does not match relationship consequence mapping"));
    if (!record->victim.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.social_legal.victim_missing", "relationship consequence requires a concrete crime victim"));

    auto applied = society_.ApplySocialChange({record->victim, record->offender, mapping->relationship_type,
                                                mapping->delta_micro,
                                                TypeId::FromString("framework.crime.relationship_penalty"), context});
    if (!applied)
        return applied;

    applied_relationship_penalties_.insert(delivery);
    return foundation::Result<void>::Success();
}

foundation::Result<crime::LawResponseId> CrimeSocietyAdapter::RequestAuthorityResponse(
    crime::CrimeRecordId crime_id, SocialLegalMappingId mapping_id, GameplayObjectRef authority,
    GameplayObjectRef target, GameplayContext context)
{
    if (!mappings_.IsFrozen())
        return foundation::Result<crime::LawResponseId>::Failure(Error("gameplay.social_legal.mappings_not_frozen", "social/legal mappings must be frozen before response generation"));
    const auto* mapping = mappings_.FindAuthorityResponseMapping(mapping_id);
    if (!mapping)
        return foundation::Result<crime::LawResponseId>::Failure(Error("gameplay.social_legal.mapping_missing", "authority response mapping missing"));
    const auto* record = crime_.FindCrime(crime_id);
    if (!record)
        return foundation::Result<crime::LawResponseId>::Failure(Error("gameplay.social_legal.crime_missing", "crime missing"));
    if (record->type != mapping->crime_type)
        return foundation::Result<crime::LawResponseId>::Failure(Error("gameplay.social_legal.crime_type_mismatch", "crime does not match authority response mapping"));
    if (!target.IsValid())
        target = record->offender;
    return crime_.GenerateLawResponse({crime_id, mapping->response_type, authority, target, context});
}

foundation::Result<void> CrimeSocietyAdapter::ProcessCrimeLifecycle()
{
    const auto batch = crime_.ReadChangesSince(crime_cursor_);
    if (batch.snapshot_required)
    {
        std::erase_if(applied_relationship_penalties_, [&](const RelationshipPenaltyDelivery& delivery) {
            return crime_.FindCrime(delivery.crime) == nullptr;
        });
        crime_cursor_ = batch.latest_cursor;
        return foundation::Result<void>::Success();
    }

    crime_cursor_.epoch = batch.latest_cursor.epoch;
    for (const auto& change : batch.changes)
    {
        if (change.kind == crime::CrimeChangeKind::CrimePruned && change.crime.IsValid())
            PruneDeliveriesForCrime(change.crime);
        crime_cursor_.sequence = change.sequence;
    }
    return foundation::Result<void>::Success();
}

std::uint64_t CrimeSocietyAdapter::PruneDeliveriesForCrime(crime::CrimeRecordId crime_id) noexcept
{
    if (!crime_id.IsValid() || crime_.FindCrime(crime_id) != nullptr)
        return 0;
    return std::erase_if(applied_relationship_penalties_, [&](const RelationshipPenaltyDelivery& delivery) {
        return delivery.crime == crime_id;
    });
}

SocialLegalCheckpoint CrimeSocietyAdapter::CaptureCheckpoint() const
{
    SocialLegalCheckpoint checkpoint;
    checkpoint.mapping_revision = mappings_.CurrentRevision();
    checkpoint.crime_cursor = crime_cursor_;
    checkpoint.applied_relationship_penalties.reserve(applied_relationship_penalties_.size());
    for (const auto& delivery : applied_relationship_penalties_)
        checkpoint.applied_relationship_penalties.push_back(delivery);
    std::sort(checkpoint.applied_relationship_penalties.begin(), checkpoint.applied_relationship_penalties.end(),
              [](const RelationshipPenaltyDelivery& lhs, const RelationshipPenaltyDelivery& rhs) {
                  if (lhs.crime != rhs.crime)
                      return lhs.crime < rhs.crime;
                  return lhs.mapping < rhs.mapping;
              });
    return checkpoint;
}

foundation::Result<void> CrimeSocietyAdapter::RestoreCheckpoint(SocialLegalCheckpoint checkpoint)
{
    if (!mappings_.IsFrozen())
        return foundation::Result<void>::Failure(Error("gameplay.social_legal.mappings_not_frozen", "social/legal mappings must be frozen before checkpoint restore"));
    if (checkpoint.mapping_revision != mappings_.CurrentRevision())
        return foundation::Result<void>::Failure(Error("gameplay.social_legal.mapping_revision_mismatch", "checkpoint mapping revision does not match current frozen mappings"));

    std::unordered_set<RelationshipPenaltyDelivery, RelationshipPenaltyDeliveryHash> restored;
    restored.reserve(checkpoint.applied_relationship_penalties.size());
    for (const auto& delivery : checkpoint.applied_relationship_penalties)
    {
        if (!delivery.crime.IsValid() || !delivery.mapping.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.social_legal.checkpoint_invalid", "checkpoint contains invalid delivery key"));
        if (!restored.insert(delivery).second)
            return foundation::Result<void>::Failure(Error("gameplay.social_legal.checkpoint_duplicate", "checkpoint contains duplicate delivery key"));
    }
    applied_relationship_penalties_ = std::move(restored);
    std::erase_if(applied_relationship_penalties_, [&](const RelationshipPenaltyDelivery& delivery) {
        return crime_.FindCrime(delivery.crime) == nullptr;
    });
    const auto latest = crime_.LatestChangeCursor();
    crime_cursor_ = checkpoint.crime_cursor.epoch == latest.epoch &&
                            checkpoint.crime_cursor.sequence <= latest.sequence
                        ? checkpoint.crime_cursor
                        : latest;
    return foundation::Result<void>::Success();
}

bool CrimeSocietyAdapter::WasRelationshipPenaltyApplied(crime::CrimeRecordId crime_id,
                                                         SocialLegalMappingId mapping) const noexcept
{
    return applied_relationship_penalties_.contains(RelationshipPenaltyDelivery{crime_id, mapping});
}
} // namespace epidemic::gameplay::social_legal_integration
