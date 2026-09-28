# Society Goal 4 local audit

## Scope

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Society`. Admission: 62 public callables, 56 mutation obligations, 1 lifecycle candidates, 14 stale-identity candidates, 0 external-boundary candidates.

## Public contract review

- Social group, relationship-type and reputation-track definitions.
- Membership lifecycle and member/group derived indexes.
- Relationship semantic-key uniqueness and derived relationship state.
- Reputation records, standings and effective-attitude queries.
- Revision/generator exhaustion is rejected before mutation.
- Multi-container publication and journal publication are failure-atomic; G4-SOC-001 and G4-SOC-002 are regressed.

## Failure atomicity and boundary review

All revision-bearing mutations were reviewed for revision/generator exhaustion before authoritative mutation. Multi-container mutations were reviewed for rollback or staged publication. Snapshot restore builds/validates candidate state before live-state replacement. Allocation-failure evidence for this block no longer depends on the process-global Framework allocator helper; owned tests use module-local failure seams where allocation failure is evidence.

## Persistence and deterministic reads

The module snapshot surface is treated as local in-memory persistence evidence for Goal 4. Non-empty roundtrip, invalid restore, generator/revision continuity, journal epoch/cursor behavior and failed-restore pre-state preservation are covered by the module test executable. Whole-engine ordered restore and replay remain Goal 5.

## Dossier review

- Responsibility: REVIEWED.
- Dependency list: REVIEWED.
- Public headers and types: REVIEWED.
- Public mutation API: REVIEWED.
- Read/query API for invariants: REVIEWED.
- Authoritative state: REVIEWED.
- Derived/cache/index state: REVIEWED.
- ID spaces, generations, revisions and cursors: REVIEWED.
- State machines: REVIEWED.
- Local invariants: REVIEWED.
- Persistent and transient state: REVIEWED.
- Snapshot/restore contract: REVIEWED.
- External ports/callbacks/providers/backends: REVIEWED.
- Hard limits, budgets and complexity bounds: REVIEWED.
- Threading contract: REVIEWED.

## Test evidence

Primary regression source: `EngineFramework/DevelopmentInfrastructure/Tests/society_tests.cpp`.
Registered target: `EpidemicGameFrameworkSocietyTests`.
Local Linux verification in the worker environment used direct C++23 compilation with `-Wall -Wextra -Wpedantic -Werror` and executed the module test binary. The repository top-level CMake configure is Windows-only and therefore cannot be used in this Linux worker environment.

## Goal 4 result

Block-local review result: LOCAL_READY candidate for serial integration. This document does not claim whole-Framework `LOCAL_READY`, `SYSTEM_READY` or `FROZEN`.

## Exact public API anchors

Each row is the exact reviewed contract for one B04 callable. The matching assertion is recorded in `_goal4_handoff/B04/public_api_anchors.json`.

- `0081df8f83a717bd` | `QUERY` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `00ae3b8468c6f297` | `MUTATOR` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] foundation::Result<void> RemoveMembership(MembershipId id,GameplayContext context={});`
- `05eb4ec84b52a871` | `QUERY` | `epidemic::gameplay::society::PairHash` | `[[nodiscard]] std::size_t operator()(const std::pair<GameplayObjectRef,GameplayObjectRef>&p)const noexcept`
- `0bff703ba46af842` | `QUERY` | `epidemic::gameplay::society::MembershipId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `0d05cfeb57d200c9` | `QUERY` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`
- `0e5c2d6e066c831e` | `QUERY` | `epidemic::gameplay::society::SocialRoleId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `120b3d4c71cb92ea` | `QUERY` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] const SocialGroupDefinition*GetGroup(GameplayObjectRef group)const noexcept;`
- `155b485dd59a5203` | `FACTORY` | `epidemic::gameplay::society::MembershipId` | `static constexpr MembershipId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `1a00f586df99cdbb` | `MUTATOR` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] foundation::Result<void> SetReputation(ReputationRecord record,GameplayContext context={});`
- `1acba63dbfe97a27` | `QUERY` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] std::vector<MembershipRecord> FindMembersOf(GameplayObjectRef group)const;`
- `1c6ef317a1ac3de8` | `QUERY` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] bool HasRole(GameplayObjectRef subject,SocialRoleId role,GameplayObjectRef scope={})const;`
- `24897f3b3bad46e1` | `MUTATOR` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(SocietySnapshot snapshot);`
- `2cb16e1ddd3ec390` | `QUERY` | `epidemic::gameplay::society::SocialGroupTypeId` | `[[nodiscard]] constexpr auto operator<=>(const SocialGroupTypeId&)const noexcept=default;`
- `37a324d77e8d1207` | `QUERY` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] std::optional<ReputationRecord> GetReputation(GameplayObjectRef subject,GameplayObjectRef scope,ReputationTrackId track)const;`
- `38757b11f0158654` | `QUERY` | `epidemic::gameplay::society::RefHash` | `[[nodiscard]] std::size_t operator()(GameplayObjectRef r)const noexcept`
- `4817b659d605826a` | `MUTATOR` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] foundation::Result<void> ApplySocialChange(SocialChangeRequest request);`
- `501e5919ede283f0` | `LIFECYCLE` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] foundation::Result<void> FreezeDefinitions();`
- `5e1807b0c6e44e69` | `FACTORY` | `epidemic::gameplay::society::MembershipId` | `static constexpr MembershipId FromString(std::string_view s)noexcept`
- `610ff937c39bb598` | `QUERY` | `epidemic::gameplay::society::RelationshipTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `61876369e15398eb` | `MUTATOR` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] foundation::Result<void> RegisterRelationshipType(RelationshipTypeDefinition definition);`
- `62531bc5d2ca533c` | `FACTORY` | `epidemic::gameplay::society::ReputationTrackId` | `static constexpr ReputationTrackId FromString(std::string_view s)noexcept`
- `64eee4e26b26ff02` | `QUERY` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] SocietySnapshot CaptureSnapshot()const;`
- `656d7252678f89b7` | `QUERY` | `std::hash` | `size_t operator()(const epidemic::gameplay::society::MembershipId&v)const noexcept`
- `6c8ac15320f7fd88` | `MUTATOR` | `epidemic::gameplay::society::SocietyService` | `void PruneChangesThrough(std::uint64_t sequence);`
- `6f583d79ad6236bf` | `QUERY` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] SocialStandingSnapshot GetSocialStanding(GameplayObjectRef subject,GameplayObjectRef scope)const;`
- `6fc7e43faaa5ef3c` | `MUTATOR` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] foundation::Result<void> RegisterReputationTrack(ReputationTrackDefinition definition);`
- `71158bca2a6fc3cb` | `QUERY` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] bool DefinitionsFrozen()const noexcept`
- `72e8819b64c35bf9` | `QUERY` | `epidemic::gameplay::society::RelationshipId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `7404a80fd2b112d3` | `MUTATOR` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] foundation::Result<void> SetMembershipState(MembershipId id,MembershipState state,GameplayContext context={});`
- `775825806a217aeb` | `QUERY` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] Revision CurrentRevision()const noexcept`
- `783615c1f1d1c714` | `QUERY` | `epidemic::gameplay::society::SocialGroupTypeId` | `[[nodiscard]] constexpr bool operator==(const SocialGroupTypeId&)const noexcept=default;`
- `8284dbc7fbfc3dbb` | `QUERY` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] std::int64_t GetEffectiveAttitude(AttitudeQuery query)const;`
- `835c9c9e8f683d8a` | `FACTORY` | `epidemic::gameplay::society::SocialGroupTypeId` | `static constexpr SocialGroupTypeId FromString(std::string_view s)noexcept`
- `86bfa5247cbc5f4c` | `QUERY` | `epidemic::gameplay::society::RelationshipId` | `[[nodiscard]] constexpr bool operator==(const RelationshipId&)const noexcept=default;`
- `8d06bb5d901689e3` | `QUERY` | `epidemic::gameplay::society::ReputationTrackId` | `[[nodiscard]] constexpr bool operator==(const ReputationTrackId&)const noexcept=default;`
- `9405d445559fcdfa` | `FACTORY` | `epidemic::gameplay::society::RelationshipTypeId` | `static constexpr RelationshipTypeId FromString(std::string_view s)noexcept`
- `9cdb8308976180fc` | `MUTATOR` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] foundation::Result<void> ApplyReputationChange(ReputationChangeRequest request);`
- `ac146fcd554edc75` | `QUERY` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] SocietyDiagnostics GetDiagnostics()const noexcept;`
- `bcd8dcb5ac9721ce` | `QUERY` | `epidemic::gameplay::society::SocialGroupTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `bd8e2f0bc6545c97` | `FACTORY` | `epidemic::gameplay::society::SocialRoleId` | `static constexpr SocialRoleId FromString(std::string_view s)noexcept`
- `bfe45f82af2fd663` | `MUTATOR` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] foundation::Result<MembershipId> AddMembership(MembershipRecord record);`
- `c2d87a2fd2ca1906` | `QUERY` | `epidemic::gameplay::society::MembershipId` | `[[nodiscard]] constexpr bool operator==(const MembershipId&)const noexcept=default;`
- `c469b83b22f2f823` | `FACTORY` | `epidemic::gameplay::society::RelationshipId` | `static constexpr RelationshipId FromString(std::string_view s)noexcept`
- `c725fcf3c28ec160` | `QUERY` | `epidemic::gameplay::society::SocialRoleId` | `[[nodiscard]] constexpr auto operator<=>(const SocialRoleId&)const noexcept=default;`
- `c7ad184d97de8df0` | `QUERY` | `epidemic::gameplay::society::RelationshipId` | `[[nodiscard]] constexpr auto operator<=>(const RelationshipId&)const noexcept=default;`
- `cb418fceb75c3b37` | `QUERY` | `epidemic::gameplay::society::ReputationTrackId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `cc63aa5dfc7c1189` | `QUERY` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] const MembershipRecord*GetMembership(MembershipId id)const noexcept;`
- `d10b5b742ab5fa14` | `QUERY` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] SocietyChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `d1afa2fd3097dfc3` | `QUERY` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] std::optional<RelationshipRecord> GetRelationship(GameplayObjectRef subject,GameplayObjectRef target,RelationshipTypeId type)const;`
- `d3493f65d2f28510` | `MUTATOR` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] foundation::Result<void> SetMembershipRole(MembershipId id,SocialRoleId role,GameplayContext context={});`
- `d9aa602e6e4e2b4b` | `QUERY` | `epidemic::gameplay::society::SocialRoleId` | `[[nodiscard]] constexpr bool operator==(const SocialRoleId&)const noexcept=default;`
- `e093778552ae19e1` | `FACTORY` | `epidemic::gameplay::society::RelationshipId` | `static constexpr RelationshipId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `e22ba59b1a8a3d46` | `QUERY` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] const ReputationTrackDefinition*GetReputationTrack(ReputationTrackId id)const noexcept;`
- `e251206fa339ae5c` | `MUTATOR` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] foundation::Result<RelationshipId> SetRelationship(RelationshipRecord record,GameplayContext context={});`
- `e32ec4f3e0e06225` | `MUTATOR` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] foundation::Result<void> RegisterGroup(SocialGroupDefinition group);`
- `eb28486feb74044b` | `QUERY` | `epidemic::gameplay::society::RelationshipTypeId` | `[[nodiscard]] constexpr auto operator<=>(const RelationshipTypeId&)const noexcept=default;`
- `ec093240d894d052` | `QUERY` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] std::vector<MembershipRecord> FindGroupsOf(GameplayObjectRef member)const;`
- `ecf546f724ab93bf` | `QUERY` | `epidemic::gameplay::society::MembershipId` | `[[nodiscard]] constexpr auto operator<=>(const MembershipId&)const noexcept=default;`
- `f00e16b459c6e25b` | `QUERY` | `epidemic::gameplay::society::ReputationTrackId` | `[[nodiscard]] constexpr auto operator<=>(const ReputationTrackId&)const noexcept=default;`
- `facd41a525bb5a89` | `QUERY` | `epidemic::gameplay::society::SocietyService` | `[[nodiscard]] const RelationshipTypeDefinition*GetRelationshipType(RelationshipTypeId id)const noexcept;`
- `fc399bf4d4752ac9` | `QUERY` | `std::hash` | `size_t operator()(const epidemic::gameplay::society::RelationshipId&v)const noexcept`
- `ffabb84fb8b597c8` | `QUERY` | `epidemic::gameplay::society::RelationshipTypeId` | `[[nodiscard]] constexpr bool operator==(const RelationshipTypeId&)const noexcept=default;`
