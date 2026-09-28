# SocialLegalIntegration

Goal 4 block: `b08_integration_adapters`.
Freeze unit: `EngineFramework/IntegrationLayer/SocialLegalIntegration`.
Admission: 28 public API, 40 mutation obligations, 1 lifecycle, 10 stale-identity, 0 external-boundary candidates.

## Public contract review

- Ownership -> Crime theft resolution.
- Crime -> Society relationship consequence.
- Authority response mapping.
- Duplicate crime input.
- Victim policy.
- Failed consequence delivery.
- Checkpoint restore без duplicate penalty.

## Failure atomicity and accepted-work review

Every local adapter mutation was reviewed for success, invalid input, retry/duplicate behavior, stale identities/cursors and publication ordering. Fallible local bookkeeping must complete before an externally owned side effect is accepted, or a durable reconciliation record must already exist.

## Persistence and reconciliation

The module exposes local snapshot/checkpoint state. Capture/restore validation and continuation are covered by the owned integration test; whole-engine ordered persistence remains Goal 5.

## Test evidence

- Source: `EngineFramework/DevelopmentInfrastructure/Tests/social_legal_integration_tests.cpp`.
- Target: `EpidemicGameFrameworkSocialLegalIntegrationTests`.
- Reviewed paths include happy path, invalid/stale input, duplicate/retry behavior and local failure/reconciliation behavior applicable to this module.
- The B08 worker compiled and executed this target under C++23 with Clang warnings-as-errors in the available Linux environment. Required Windows MSVC Debug/Release qualification remains a serial-integration gate.

## Goal 4 result

`LOCAL_READY_CANDIDATE` for block-local evidence. This is not a whole-Framework `LOCAL_READY`, `SYSTEM_READY` or `FROZEN` claim.

<!-- goal4-exact-evidence -->

## Exact Goal 4 callable contracts

- G4-API-056d09f4f73d6a97: `epidemic::gameplay::social_legal_integration::SocialLegalMappings` — `[[nodiscard]] Revision CurrentRevision()const noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-13b532bace3072e6: `epidemic::gameplay::social_legal_integration::SocialLegalMappingId` — `static constexpr SocialLegalMappingId FromString(std::string_view value)noexcept`; classification `FACTORY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-2fca9da3fc6e2916: `epidemic::gameplay::social_legal_integration::SocialLegalMappingId` — `[[nodiscard]] constexpr bool operator==(const SocialLegalMappingId&)const noexcept=default;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-3f34d04fe6e390c1: `epidemic::gameplay::social_legal_integration::CrimeSocietyAdapter` — `[[nodiscard]] bool WasRelationshipPenaltyApplied(crime::CrimeRecordId crime_id,SocialLegalMappingId mapping)const noexcept;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-46a4be8819867198: `epidemic::gameplay::social_legal_integration::SocialLegalMappings` — `[[nodiscard]] const PropertyCrimeMapping*FindPropertyCrimeMapping(SocialLegalMappingId id)const noexcept;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-4bd21378792cd1de: `epidemic::gameplay::social_legal_integration::RelationshipPenaltyDeliveryHash` — `[[nodiscard]] std::size_t operator()(const RelationshipPenaltyDelivery&value)const noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-5aaf0c8fb5cb2676: `epidemic::gameplay::social_legal_integration::OwnershipCrimeAdapter` — `OwnershipCrimeAdapter(ownership::OwnershipService&ownership,crime::CrimeService&crime,const SocialLegalMappings&mappings)noexcept;`; classification `CONSTRUCTOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-5c89087861b84c2a: `epidemic::gameplay::social_legal_integration::SocialLegalMappingId` — `[[nodiscard]] constexpr bool IsValid()const noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-62a3449af554bafe: `epidemic::gameplay::social_legal_integration::CrimeSocietyAdapter` — `[[nodiscard]] foundation::Result<void> ApplyRelationshipPenalty(crime::CrimeRecordId crime_id,SocialLegalMappingId mapping,GameplayContext context={});`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-6b5706cad80f4ae7: `epidemic::gameplay::social_legal_integration::CrimeSocietyAdapter` — `std::uint64_t PruneDeliveriesForCrime(crime::CrimeRecordId crime_id)noexcept;`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-763b407e35e4fb10: `epidemic::gameplay::social_legal_integration::SocialLegalMappings` — `[[nodiscard]] foundation::Result<void> RegisterPropertyCrimeMapping(PropertyCrimeMapping mapping);`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-770a5426d71ab9ec: `epidemic::gameplay::social_legal_integration::SocialLegalMappingId` — `[[nodiscard]] constexpr auto operator<=>(const SocialLegalMappingId&)const noexcept=default;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-7c722fcb135070d9: `epidemic::gameplay::social_legal_integration::CrimeSocietyAdapter` — `[[nodiscard]] foundation::Result<void> ProcessCrimeLifecycle();`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-806e8068acae1945: `epidemic::gameplay::social_legal_integration::CrimeSocietyAdapter` — `CrimeSocietyAdapter(crime::CrimeService&crime,society::SocietyService&society,const SocialLegalMappings&mappings)noexcept;`; classification `CONSTRUCTOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-80712746c39d2ed3: `epidemic::gameplay::social_legal_integration::OwnershipCrimeAdapter` — `[[nodiscard]] foundation::Result<TheftResolution> TryCreateCrimeFromDeniedRight(GameplayObjectRef actor,GameplayObjectRef property,SocialLegalMappingId mapping,GameplayObjectRef area,GameplayContext context={});`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-812125bbd081c362: `epidemic::gameplay::social_legal_integration::SocialLegalMappings` — `[[nodiscard]] bool IsFrozen()const noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-839af75486125015: `epidemic::gameplay::social_legal_integration::CrimeSocietyAdapter` — `[[nodiscard]] foundation::Result<void> RestoreCheckpoint(SocialLegalCheckpoint checkpoint);`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-85b026de46d751a2: `epidemic::gameplay::social_legal_integration::CrimeSocietyAdapter` — `[[nodiscard]] foundation::Result<crime::LawResponseId> RequestAuthorityResponse(crime::CrimeRecordId crime_id,SocialLegalMappingId mapping,GameplayObjectRef authority,GameplayObjectRef target={},GameplayContext context={});`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-9bf5b689dea97aba: `epidemic::gameplay::social_legal_integration::RelationshipPenaltyDelivery` — `[[nodiscard]] constexpr bool operator==(const RelationshipPenaltyDelivery&)const noexcept=default;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-acbcc1795dc48b93: `epidemic::gameplay::social_legal_integration::SocialLegalMappings` — `SocialLegalMappings(const crime::CrimeService&crime,const society::SocietyService&society)noexcept;`; classification `CONSTRUCTOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-b55f09a105e1c560: `epidemic::gameplay::social_legal_integration::SocialLegalMappings` — `[[nodiscard]] foundation::Result<void> Freeze();`; classification `LIFECYCLE`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-ce53702909e4f929: `epidemic::gameplay::social_legal_integration::MappingIdHash` — `[[nodiscard]] std::size_t operator()(SocialLegalMappingId id)const noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-d0ad53ca4f7f3f15: `epidemic::gameplay::social_legal_integration::SocialLegalMappings` — `[[nodiscard]] const AuthorityResponseMapping*FindAuthorityResponseMapping(SocialLegalMappingId id)const noexcept;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-d211f3254a7fc842: `epidemic::gameplay::social_legal_integration::SocialLegalMappings` — `[[nodiscard]] foundation::Result<void> RegisterCrimeRelationshipMapping(CrimeRelationshipConsequenceMapping mapping);`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-d336e2e2f2e198a8: `std::hash` — `size_t operator()(const epidemic::gameplay::social_legal_integration::SocialLegalMappingId&value)const noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-dda1c1ad3bd7ede5: `epidemic::gameplay::social_legal_integration::SocialLegalMappings` — `[[nodiscard]] const CrimeRelationshipConsequenceMapping*FindCrimeRelationshipMapping(SocialLegalMappingId id)const noexcept;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-e4a1924fa81622d6: `epidemic::gameplay::social_legal_integration::CrimeSocietyAdapter` — `[[nodiscard]] SocialLegalCheckpoint CaptureCheckpoint()const;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-f341e9c48b910224: `epidemic::gameplay::social_legal_integration::SocialLegalMappings` — `[[nodiscard]] foundation::Result<void> RegisterAuthorityResponseMapping(AuthorityResponseMapping mapping);`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.

## Exact Goal 4 criterion contracts

- G4-CRITERION-ARCH-RESPONSIBILITY: `ARCH-RESPONSIBILITY` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-ARCH-SINGLE-OWNER: `ARCH-SINGLE-OWNER` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-ARCH-DIRECT-DEPS: `ARCH-DIRECT-DEPS` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-ARCH-PORTS: `ARCH-PORTS` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-API-CLASSIFIED: `API-CLASSIFIED` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-API-PRECONDITIONS: `API-PRECONDITIONS` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-API-SUCCESS: `API-SUCCESS` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-API-FAILURE: `API-FAILURE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-API-OVERLOADS: `API-OVERLOADS` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-API-INVALID: `API-INVALID` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-STATE-PRIMARY: `STATE-PRIMARY` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-STATE-INDEXES: `STATE-INDEXES` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-STATE-COUNTERS: `STATE-COUNTERS` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-STATE-NO-FALSE-PUBLISH: `STATE-NO-FALSE-PUBLISH` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-STATE-NOOP: `STATE-NOOP` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-LIFE-ALLOWED: `LIFE-ALLOWED` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-LIFE-FORBIDDEN: `LIFE-FORBIDDEN` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-LIFE-SHUTDOWN: `LIFE-SHUTDOWN` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-LIFE-RETRY-CLEANUP: `LIFE-RETRY-CLEANUP` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-ATOMIC-SINGLE: `ATOMIC-SINGLE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-ATOMIC-MULTI: `ATOMIC-MULTI` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-ATOMIC-EXTERNAL: `ATOMIC-EXTERNAL` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-ATOMIC-RECONCILE: `ATOMIC-RECONCILE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-PERSIST-SNAPSHOT: `PERSIST-SNAPSHOT` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-PERSIST-VALIDATE: `PERSIST-VALIDATE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-PERSIST-CANDIDATE: `PERSIST-CANDIDATE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-PERSIST-FAILURE: `PERSIST-FAILURE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-PERSIST-CONTINUITY: `PERSIST-CONTINUITY` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-HAPPY: `TEST-HAPPY` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-INVALID: `TEST-INVALID` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-DUPLICATE: `TEST-DUPLICATE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-STALE: `TEST-STALE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-EMPTY: `TEST-EMPTY` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-BOUNDARY: `TEST-BOUNDARY` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-WRONG-LIFECYCLE: `TEST-WRONG-LIFECYCLE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-CALLBACK-FAILURE: `TEST-CALLBACK-FAILURE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-REGRESSION: `TEST-REGRESSION` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
