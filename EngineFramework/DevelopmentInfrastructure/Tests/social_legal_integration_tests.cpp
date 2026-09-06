#include "Epidemic/GameFramework/SocialLegalIntegration/social_legal_adapters.h"

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::ownership;
using namespace epidemic::gameplay::crime;
using namespace epidemic::gameplay::society;
using namespace epidemic::gameplay::social_legal_integration;

static GameplayObjectRef Ref(const char* name)
{
    return {GameplayDomainId::FromString("test"), GameplayObjectId::FromString(name)};
}

template <typename TSociety>
static bool ConfigureSociety(TSociety& society, RelationshipTypeId relationship_type)
{
    if constexpr (requires(TSociety& service, RelationshipTypeDefinition definition) {
                      service.RegisterRelationshipType(definition);
                      service.FreezeDefinitions();
                  })
    {
        RelationshipTypeDefinition definition;
        definition.id = relationship_type;
        definition.minimum_value_micro = -1'000'000;
        definition.maximum_value_micro = 1'000'000;
        definition.default_value_micro = 0;
        definition.state_thresholds = {
            {-1'000'000, RelationshipState::Hostile},
            {-300'000, RelationshipState::Hostile},
            {0, RelationshipState::Neutral},
            {300'000, RelationshipState::Friendly},
        };
        if (!society.RegisterRelationshipType(definition))
            return false;
        if (!society.FreezeDefinitions())
            return false;
    }
    return true;
}

static bool RegisterTheftLaw(CrimeService& crime, GameplayObjectRef city, GameplayObjectRef guards,
                             LawDefinition& law, JurisdictionRecord& jurisdiction)
{
    law.id = LawId::FromString("law.theft");
    law.crime_type = CrimeTypeId::FromString("crime.theft");
    law.severity_micro = 200000;
    if (!crime.RegisterLaw(law))
        return false;

    jurisdiction.id = JurisdictionId::FromString("jur.city");
    jurisdiction.area = city;
    jurisdiction.authority_group = guards;
    jurisdiction.active_laws.push_back(law.id);
    return static_cast<bool>(crime.RegisterJurisdiction(jurisdiction));
}

int main()
{
    OwnershipService ownership;
    CrimeService crime;
    SocietyService society;

    const auto player = Ref("player");
    const auto npc = Ref("npc");
    const auto apple = Ref("apple");
    const auto locked_box = Ref("locked_box");
    const auto public_gate = Ref("public_gate");
    const auto city = Ref("city");
    const auto guards = Ref("guards");
    const auto take = PropertyRightId::FromString("right.take");
    const auto trust = RelationshipTypeId::FromString("rel.trust");

    OwnershipRecord apple_owner;
    apple_owner.property = apple;
    apple_owner.owner = npc;
    if (!ownership.AssignOwnership(apple_owner))
        return 1;

    OwnershipRecord box_owner;
    box_owner.property = locked_box;
    box_owner.owner = npc;
    if (!ownership.AssignOwnership(box_owner))
        return 2;

    AccessRule denied_rule;
    denied_rule.property = locked_box;
    denied_rule.right = take;
    denied_rule.decision = AccessDecision::Deny;
    if (!ownership.AddAccessRule(denied_rule))
        return 3;

    AccessRule public_crime_rule;
    public_crime_rule.property = public_gate;
    public_crime_rule.right = take;
    public_crime_rule.decision = AccessDecision::CrimeIfViolated;
    if (!ownership.AddAccessRule(public_crime_rule))
        return 4;

    LawDefinition law;
    JurisdictionRecord jurisdiction;
    if (!RegisterTheftLaw(crime, city, guards, law, jurisdiction))
        return 5;
    LawResponseDefinition arrest_response;
    arrest_response.id = LawResponseTypeId::FromString("response.arrest");
    if (!crime.RegisterLawResponseDefinition(arrest_response))
        return 6; // register arrest response definition
    AuthorityRecord authority;
    authority.authority_group = guards;
    authority.jurisdiction = jurisdiction.id;
    if (!crime.RegisterAuthority(authority))
        return 7;
    if (!crime.FreezeDefinitions())
        return 8;
    if (!ConfigureSociety(society, trust))
        return 10;

    const auto property_mapping_id = SocialLegalMappingId::FromString("mapping.property.theft");
    const auto public_mapping_id = SocialLegalMappingId::FromString("mapping.property.public_offence");
    const auto penalty_mapping_id = SocialLegalMappingId::FromString("mapping.crime.trust_penalty");
    const auto response_mapping_id = SocialLegalMappingId::FromString("mapping.crime.arrest");

    SocialLegalMappings mappings(crime, society);

    PropertyCrimeMapping property_mapping;
    property_mapping.id = property_mapping_id;
    property_mapping.right = take;
    property_mapping.crime_type = law.crime_type;
    property_mapping.victim_policy = PropertyCrimeVictimPolicy::PropertyOwnerRequired;
    if (!mappings.RegisterPropertyCrimeMapping(property_mapping))
        return 9;

    PropertyCrimeMapping public_mapping;
    public_mapping.id = public_mapping_id;
    public_mapping.right = take;
    public_mapping.crime_type = law.crime_type;
    public_mapping.victim_policy = PropertyCrimeVictimPolicy::PublicSubjectRequired;
    public_mapping.public_subject = guards;
    if (!mappings.RegisterPropertyCrimeMapping(public_mapping))
        return 10;

    CrimeRelationshipConsequenceMapping penalty_mapping;
    penalty_mapping.id = penalty_mapping_id;
    penalty_mapping.crime_type = law.crime_type;
    penalty_mapping.relationship_type = trust;
    penalty_mapping.delta_micro = -700000;
    if (!mappings.RegisterCrimeRelationshipMapping(penalty_mapping))
        return 11;

    AuthorityResponseMapping response_mapping;
    response_mapping.id = response_mapping_id;
    response_mapping.crime_type = law.crime_type;
    response_mapping.response_type = LawResponseTypeId::FromString("response.arrest");
    if (!mappings.RegisterAuthorityResponseMapping(response_mapping))
        return 12;

    PropertyCrimeMapping duplicate = property_mapping;
    duplicate.right = PropertyRightId::FromString("right.other");
    if (mappings.RegisterPropertyCrimeMapping(duplicate))
        return 13;

    if (!mappings.Freeze() || !mappings.IsFrozen())
        return 14;
    if (mappings.RegisterAuthorityResponseMapping({SocialLegalMappingId::FromString("mapping.late"),
                                                   law.crime_type,
                                                   LawResponseTypeId::FromString("response.warn")}))
        return 15;

    OwnershipCrimeAdapter ownership_crime(ownership, crime, mappings);

    GameplayContext context;
    context.time = GameplayTimePoint{12'345};
    context.tick = GameplayTickId{999'999};

    // H87: a plain Denied access decision blocks the action but is not automatically criminal.
    auto denied = ownership_crime.TryCreateCrimeFromDeniedRight(player, locked_box, property_mapping_id, city, context);
    if (!denied || denied.Value().permission.decision != PermissionDecision::Denied || denied.Value().crime ||
        denied.Value().disposition != PropertyCrimeDisposition::NoViolation)
        return 16;

    // H86: the legal timestamp is gameplay time, never the causal tick id.
    auto made = ownership_crime.TryCreateCrimeFromDeniedRight(player, apple, property_mapping_id, city, context);
    if (!made || !made.Value().crime || made.Value().disposition != PropertyCrimeDisposition::CrimeRecorded)
        return 17;
    const auto* crime_record = crime.FindCrime(*made.Value().crime);
    if (!crime_record || crime_record->committed_at != context.time || crime_record->committed_at.ticks == 999'999)
        return 18;
    if (crime_record->victim != npc)
        return 19;

    // H88: a property-owner-required law cannot silently become victimless.
    auto missing_owner = ownership_crime.TryCreateCrimeFromDeniedRight(player, public_gate, property_mapping_id, city, context);
    if (!missing_owner || missing_owner.Value().crime ||
        missing_owner.Value().disposition != PropertyCrimeDisposition::MissingRequiredVictim)
        return 20;

    // Public/victimless legal semantics are explicit in the mapping, not inferred from missing ownership.
    auto public_offence = ownership_crime.TryCreateCrimeFromDeniedRight(player, public_gate, public_mapping_id, city, context);
    if (!public_offence || !public_offence.Value().crime)
        return 21;
    const auto* public_record = crime.FindCrime(*public_offence.Value().crime);
    if (!public_record || public_record->victim != guards)
        return 22;

    CrimeSocietyAdapter crime_society(crime, society, mappings);
    if (!crime_society.ApplyRelationshipPenalty(*made.Value().crime, penalty_mapping_id, context))
        return 23;
    const auto first = society.GetRelationship(npc, player, trust);
    if (!first || first->value_micro != -700000)
        return 24;

    // H89: retrying the same crime consequence is idempotent.
    if (!crime_society.ApplyRelationshipPenalty(*made.Value().crime, penalty_mapping_id, context))
        return 25;
    const auto repeated = society.GetRelationship(npc, player, trust);
    if (!repeated || repeated->value_micro != -700000)
        return 26;
    if (!crime_society.WasRelationshipPenaltyApplied(*made.Value().crime, penalty_mapping_id))
        return 27;

    // The idempotency marker is explicit technical persistence state.
    const auto checkpoint = crime_society.CaptureCheckpoint();
    CrimeSocietyAdapter restored_adapter(crime, society, mappings);
    if (!restored_adapter.RestoreCheckpoint(checkpoint))
        return 28;
    if (!restored_adapter.ApplyRelationshipPenalty(*made.Value().crime, penalty_mapping_id, context))
        return 29;
    const auto after_restore_retry = society.GetRelationship(npc, player, trust);
    if (!after_restore_retry || after_restore_retry->value_micro != -700000)
        return 30;

    SocialLegalCheckpoint duplicate_checkpoint = checkpoint;
    if (!duplicate_checkpoint.applied_relationship_penalties.empty())
        duplicate_checkpoint.applied_relationship_penalties.push_back(duplicate_checkpoint.applied_relationship_penalties.front());
    if (restored_adapter.RestoreCheckpoint(std::move(duplicate_checkpoint)))
        return 31;

    // M33/M34: authority response is explicit and mapped; target defaults to the offender.
    auto response = crime_society.RequestAuthorityResponse(*made.Value().crime, response_mapping_id, guards, {}, context);
    if (!response)
        return 32;

    // M34: mappings cannot be frozen when they reference an unknown crime type.
    SocialLegalMappings invalid_mappings(crime, society);
    PropertyCrimeMapping invalid_mapping;
    invalid_mapping.id = SocialLegalMappingId::FromString("mapping.invalid");
    invalid_mapping.right = take;
    invalid_mapping.crime_type = CrimeTypeId::FromString("crime.not_registered");
    if (!invalid_mappings.RegisterPropertyCrimeMapping(invalid_mapping))
        return 33;
    if (invalid_mappings.Freeze())
        return 34;

    return 0;
}
