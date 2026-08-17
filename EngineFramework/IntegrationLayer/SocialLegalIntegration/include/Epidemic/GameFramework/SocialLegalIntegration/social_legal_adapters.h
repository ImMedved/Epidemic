#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Crime/crime.h"
#include "Epidemic/GameFramework/Ownership/ownership.h"
#include "Epidemic/GameFramework/Society/society.h"

namespace epidemic::gameplay::social_legal_integration
{
struct TheftResolution
{
    ownership::OwnershipPermissionResult permission;
    std::optional<crime::CrimeRecordId> crime;
};

class OwnershipCrimeAdapter
{
public:
    OwnershipCrimeAdapter(ownership::OwnershipService& ownership, crime::CrimeService& crime) noexcept;
    [[nodiscard]] foundation::Result<TheftResolution> TryCreateCrimeFromDeniedRight(
        GameplayObjectRef actor,
        GameplayObjectRef property,
        ownership::PropertyRightId right,
        crime::CrimeTypeId crime_type,
        GameplayObjectRef area,
        GameplayContext context = {});
private:
    ownership::OwnershipService& ownership_;
    crime::CrimeService& crime_;
};

class CrimeSocietyAdapter
{
public:
    CrimeSocietyAdapter(crime::CrimeService& crime, society::SocietyService& society) noexcept;
    [[nodiscard]] foundation::Result<void> ApplyRelationshipPenalty(
        crime::CrimeRecordId crime_id,
        society::RelationshipTypeId relationship_type,
        std::int64_t delta_micro,
        GameplayContext context = {});
    [[nodiscard]] foundation::Result<crime::LawResponseId> GenerateAuthorityIntentLikeResponse(
        crime::CrimeRecordId crime_id,
        crime::LawResponseTypeId response_type,
        GameplayObjectRef authority,
        GameplayObjectRef target,
        GameplayContext context = {});
private:
    crime::CrimeService& crime_;
    society::SocietyService& society_;
};
} // namespace epidemic::gameplay::social_legal_integration
