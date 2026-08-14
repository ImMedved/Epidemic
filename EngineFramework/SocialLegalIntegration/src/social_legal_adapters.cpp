#include "Epidemic/GameFramework/SocialLegalIntegration/social_legal_adapters.h"
#include "Epidemic/Foundation/error.h"

namespace epidemic::gameplay::social_legal_integration
{
namespace{foundation::Error Error(std::string_view c,std::string_view m){return foundation::Error::Create(c,m);} }
OwnershipCrimeAdapter::OwnershipCrimeAdapter(ownership::OwnershipService& ownership, crime::CrimeService& crime) noexcept:ownership_(ownership),crime_(crime){}
foundation::Result<TheftResolution> OwnershipCrimeAdapter::TryCreateCrimeFromDeniedRight(GameplayObjectRef actor,GameplayObjectRef property,ownership::PropertyRightId right,crime::CrimeTypeId crime_type,GameplayObjectRef area,GameplayContext context)
{
    auto permission=ownership_.EvaluatePermission({actor,property,right,context});
    TheftResolution out;out.permission=permission;
    if(permission.decision==ownership::PermissionDecision::Allowed||permission.decision==ownership::PermissionDecision::NotApplicable)
    {
        return foundation::Result<TheftResolution>::Success(out);
    }
    crime::CrimeCandidate candidate;candidate.type=crime_type;candidate.offender=actor;candidate.victim=permission.effective_owner;candidate.target_property=property;candidate.area=area;candidate.time=context.tick.IsValid()?GameplayTimePoint{static_cast<std::int64_t>(context.tick.Raw())}:GameplayTimePoint{};candidate.context=context;
    auto created=crime_.EvaluateCrimeCandidate(candidate,crime::CrimeCandidatePolicy::RecordAlways);
    if(!created)return foundation::Result<TheftResolution>::Failure(std::move(created.GetError()));
    out.crime=created.Value().crime;
    return foundation::Result<TheftResolution>::Success(out);
}
CrimeSocietyAdapter::CrimeSocietyAdapter(crime::CrimeService& crime,society::SocietyService& society) noexcept:crime_(crime),society_(society){}
foundation::Result<void> CrimeSocietyAdapter::ApplyRelationshipPenalty(crime::CrimeRecordId crime_id,society::RelationshipTypeId relationship_type,std::int64_t delta_micro,GameplayContext context)
{
    auto* record=crime_.FindCrime(crime_id);
    if(!record)return foundation::Result<void>::Failure(Error("gameplay.social_legal.crime_missing","crime missing"));
    return society_.ApplySocialChange({record->victim,record->offender,relationship_type,delta_micro,TypeId::FromString("framework.crime.relationship_penalty"),context});
}
foundation::Result<crime::LawResponseId> CrimeSocietyAdapter::GenerateAuthorityIntentLikeResponse(crime::CrimeRecordId crime_id,crime::LawResponseTypeId response_type,GameplayObjectRef authority,GameplayObjectRef target,GameplayContext context)
{
    return crime_.GenerateLawResponse({crime_id,response_type,authority,target,context});
}
} // namespace epidemic::gameplay::social_legal_integration
