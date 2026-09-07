#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Crime/crime.h"
#include "Epidemic/GameFramework/Ownership/ownership.h"
#include "Epidemic/GameFramework/Society/society.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace epidemic::gameplay::social_legal_integration
{
struct SocialLegalMappingId
{
    TypeId value{};

    static constexpr SocialLegalMappingId FromString(std::string_view value) noexcept
    {
        return {TypeId::FromString(value)};
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const SocialLegalMappingId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const SocialLegalMappingId&) const noexcept = default;
};

struct MappingIdHash
{
    [[nodiscard]] std::size_t operator()(SocialLegalMappingId id) const noexcept
    {
        return std::hash<TypeId>{}(id.value);
    }
};

enum class PropertyCrimeVictimPolicy
{
    PropertyOwnerRequired,
    PropertyOwnerOptional,
    PublicSubjectRequired,
    Victimless
};

struct PropertyCrimeMapping
{
    SocialLegalMappingId id{};
    ownership::PropertyRightId right{};
    crime::CrimeTypeId crime_type{};
    PropertyCrimeVictimPolicy victim_policy = PropertyCrimeVictimPolicy::PropertyOwnerRequired;
    GameplayObjectRef public_subject{};
    bool denied_is_violation = false;
    bool trespass_is_violation = true;
    bool crime_candidate_is_violation = true;
};

struct CrimeRelationshipConsequenceMapping
{
    SocialLegalMappingId id{};
    crime::CrimeTypeId crime_type{};
    society::RelationshipTypeId relationship_type{};
    std::int64_t delta_micro = 0;
};

struct AuthorityResponseMapping
{
    SocialLegalMappingId id{};
    crime::CrimeTypeId crime_type{};
    crime::LawResponseTypeId response_type{};
};

enum class PropertyCrimeDisposition
{
    NoViolation,
    MissingRequiredVictim,
    CrimeRecorded
};

struct TheftResolution
{
    ownership::OwnershipPermissionResult permission;
    PropertyCrimeDisposition disposition = PropertyCrimeDisposition::NoViolation;
    std::optional<crime::CrimeRecordId> crime;
};

struct RelationshipPenaltyDelivery
{
    crime::CrimeRecordId crime{};
    SocialLegalMappingId mapping{};

    [[nodiscard]] constexpr bool operator==(const RelationshipPenaltyDelivery&) const noexcept = default;
};

struct RelationshipPenaltyDeliveryHash
{
    [[nodiscard]] std::size_t operator()(const RelationshipPenaltyDelivery& value) const noexcept
    {
        const auto a = std::hash<crime::CrimeRecordId>{}(value.crime);
        const auto b = std::hash<TypeId>{}(value.mapping.value);
        return a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6U) + (a >> 2U));
    }
};

struct SocialLegalCheckpoint
{
    Revision mapping_revision{};
    std::uint64_t crime_cursor = 0;
    std::vector<RelationshipPenaltyDelivery> applied_relationship_penalties;
};

class SocialLegalMappings
{
public:
    SocialLegalMappings(const crime::CrimeService& crime, const society::SocietyService& society) noexcept;

    [[nodiscard]] foundation::Result<void> RegisterPropertyCrimeMapping(PropertyCrimeMapping mapping);
    [[nodiscard]] foundation::Result<void> RegisterCrimeRelationshipMapping(CrimeRelationshipConsequenceMapping mapping);
    [[nodiscard]] foundation::Result<void> RegisterAuthorityResponseMapping(AuthorityResponseMapping mapping);
    [[nodiscard]] foundation::Result<void> Freeze();

    [[nodiscard]] bool IsFrozen() const noexcept { return frozen_; }
    [[nodiscard]] Revision CurrentRevision() const noexcept { return revision_; }
    [[nodiscard]] const PropertyCrimeMapping* FindPropertyCrimeMapping(SocialLegalMappingId id) const noexcept;
    [[nodiscard]] const CrimeRelationshipConsequenceMapping* FindCrimeRelationshipMapping(SocialLegalMappingId id) const noexcept;
    [[nodiscard]] const AuthorityResponseMapping* FindAuthorityResponseMapping(SocialLegalMappingId id) const noexcept;

private:
    [[nodiscard]] bool CrimeTypeRegistered(crime::CrimeTypeId id) const;
    [[nodiscard]] bool RelationshipTypeRegistered(society::RelationshipTypeId id) const;
    void Bump() noexcept { ++revision_.value; }

    const crime::CrimeService& crime_;
    const society::SocietyService& society_;
    std::unordered_map<SocialLegalMappingId, PropertyCrimeMapping, MappingIdHash> property_crime_;
    std::unordered_map<SocialLegalMappingId, CrimeRelationshipConsequenceMapping, MappingIdHash> crime_relationship_;
    std::unordered_map<SocialLegalMappingId, AuthorityResponseMapping, MappingIdHash> authority_response_;
    Revision revision_{};
    bool frozen_ = false;
};

class OwnershipCrimeAdapter
{
public:
    OwnershipCrimeAdapter(ownership::OwnershipService& ownership, crime::CrimeService& crime,
                          const SocialLegalMappings& mappings) noexcept;

    [[nodiscard]] foundation::Result<TheftResolution> TryCreateCrimeFromDeniedRight(
        GameplayObjectRef actor,
        GameplayObjectRef property,
        SocialLegalMappingId mapping,
        GameplayObjectRef area,
        GameplayContext context = {});

private:
    ownership::OwnershipService& ownership_;
    crime::CrimeService& crime_;
    const SocialLegalMappings& mappings_;
};

class CrimeSocietyAdapter
{
public:
    CrimeSocietyAdapter(crime::CrimeService& crime, society::SocietyService& society,
                        const SocialLegalMappings& mappings) noexcept;

    [[nodiscard]] foundation::Result<void> ApplyRelationshipPenalty(
        crime::CrimeRecordId crime_id,
        SocialLegalMappingId mapping,
        GameplayContext context = {});

    [[nodiscard]] foundation::Result<crime::LawResponseId> RequestAuthorityResponse(
        crime::CrimeRecordId crime_id,
        SocialLegalMappingId mapping,
        GameplayObjectRef authority,
        GameplayObjectRef target = {},
        GameplayContext context = {});

    [[nodiscard]] foundation::Result<void> ProcessCrimeLifecycle();
    [[nodiscard]] SocialLegalCheckpoint CaptureCheckpoint() const;
    [[nodiscard]] foundation::Result<void> RestoreCheckpoint(SocialLegalCheckpoint checkpoint);
    [[nodiscard]] bool WasRelationshipPenaltyApplied(crime::CrimeRecordId crime_id,
                                                      SocialLegalMappingId mapping) const noexcept;
    std::uint64_t PruneDeliveriesForCrime(crime::CrimeRecordId crime_id) noexcept;

private:
    crime::CrimeService& crime_;
    society::SocietyService& society_;
    const SocialLegalMappings& mappings_;
    std::unordered_set<RelationshipPenaltyDelivery, RelationshipPenaltyDeliveryHash> applied_relationship_penalties_;
    std::uint64_t crime_cursor_ = 0;
};
} // namespace epidemic::gameplay::social_legal_integration

namespace std
{
template <> struct hash<epidemic::gameplay::social_legal_integration::SocialLegalMappingId>
{
    size_t operator()(const epidemic::gameplay::social_legal_integration::SocialLegalMappingId& value) const noexcept
    {
        return hash<epidemic::gameplay::TypeId>{}(value.value);
    }
};
} // namespace std
