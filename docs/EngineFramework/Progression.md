# Progression Goal 4 local audit

Status: B06 worker review complete. This is module-local Goal 4 evidence only; canonical promotion is performed after serial convergence.

## Responsibility

Owns progression profiles, attributes, tracks/ranks, modifiers, perks, unlocks, milestones, progress-grant reservations, ID generators and change journal.

## Public headers and types

Public surface is rooted at `EngineFramework/GameplayWorldStateOwners/Progression/include/Epidemic/GameFramework/Progression/progression.h`. B06 changes no public header/signature.

## Dependency list

Direct production links are GameFramework Foundation and EngineBase Foundation only.

## External ports/callbacks/providers/backends

No durable external backend. Prerequisites and rewards are evaluated against service-owned profile/definition state.

## Authoritative state

Definition registries, profiles, modifier and reservation generators, pending progress reservations, global/profile revisions and bounded journal.

## Derived/cache/index state

Rank is derived deterministically from track thresholds. Milestone ordering is canonicalized before reward evaluation.

## ID spaces, generations, revisions and cursors

Modifier and progress-reservation IDs use monotonic generators. Global/profile/track revisions and journal cursor are checked at mutation boundaries.

## State machines

Profile mutation and reserve/commit/release progress grant form local transactions. Milestones are terminal achievements and reward deduplication is preserved on retry.

## Persistent and transient state

Snapshot persists profiles, persistent modifiers, generators, global/profile/track revisions and journal continuity. Pending progress reservations are transient local transaction state.

## Snapshot/restore contract

Restore validates definitions, duplicate IDs, track/rank consistency, persistent modifier identity, generator continuity and journal sequence before publication. Private restore seams provide candidate/pre-commit failure evidence.

## Threading contract

Owner-thread/external-serialization state. Goal 6 owns concurrency qualification.

## Public mutation API

Profile removal and all revision-bearing attribute/perk/unlock/progress paths now preflight revision before destructive mutation. `AddModifier` stages the generator and vector before commit (`G4-PROG-003`). `CommitProgressGrant` stages profile and milestone rewards, preflights the full revision fan-out, and consumes its reservation only after terminal publication (`G4-PROG-001/002`).

## Read/query API for invariants

Profile/attribute/track/perk/unlock/snapshot queries witness profile consistency, deterministic modifier ordering and reward deduplication.

## Local invariants

Failed revision/allocation publication leaves profile, generators and pending reservations coherent. Retry cannot duplicate committed milestone rewards. A late journal allocation after accepted state rotates the journal epoch rather than escaping after commit (`G4-B06-PROG-004`).

## Hard limits, budgets and complexity bounds

Revisions, monotonic IDs and change sequence exhaust deterministically. Journal retention is bounded.

## Test evidence

`EngineFramework/DevelopmentInfrastructure/Tests/progression_tests.cpp` covers profile/attribute/modifier/track/perk behavior, reservations, snapshot/generator validation and journal exhaustion. Goal 4 adds regressions for max revision, non-consumed reservation on failed commit, modifier-generator atomicity, private restore seams and journal failure. Target: `EpidemicGameFrameworkProgressionTests`.

## Exact public API anchors

Each row is the exact reviewed contract for one B06 callable. The matching assertion is recorded in `_goal4_handoff/B06/public_api_anchors.json`.

- `01d0fe0cb35fabdc` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<AttributeTypeId> RegisterAttribute(AttributeDefinition definition);`
- `0c810f6837035df9` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<void> RevokePerk(GameplayObjectRef subject,PerkDefinitionId perk,GameplayContext context={});`
- `0d0216d312257fe6` | `CONSTRUCTOR` | `epidemic::gameplay::progression::ProgressionService` | `ProgressionService();`
- `0de304f8afd1c5f1` | `QUERY` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<std::int64_t> GetAttribute(GameplayObjectRef subject,AttributeTypeId attribute)const;`
- `0f0a01605c90478a` | `QUERY` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] PrerequisiteEvaluation QueryPrerequisites(GameplayObjectRef subject,const std::vector<ProgressionPrerequisite>&prerequisites)const;`
- `11a8afe1c03b9537` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<void> GrantUnlock(GameplayObjectRef subject,UnlockRecord unlock,GameplayContext context={});`
- `192c5f29b25e2983` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<ProgressionTrackId> RegisterTrack(ProgressionTrackDefinition definition);`
- `1d466b2503964d05` | `QUERY` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] ProgressionDiagnostics GetDiagnostics()const noexcept;`
- `1f686a74f01ae499` | `QUERY` | `epidemic::gameplay::progression::UnlockDefinitionId` | `[[nodiscard]] constexpr auto operator<=>(const UnlockDefinitionId&)const noexcept=default;`
- `215c09447e93468b` | `QUERY` | `epidemic::gameplay::progression::ProgressionGrantReservationId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `26271dc59e4c4d24` | `QUERY` | `epidemic::gameplay::progression::UnlockDefinitionId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `2e7a94678e161690` | `FACTORY` | `epidemic::gameplay::progression::PerkDefinitionId` | `[[nodiscard]] static constexpr PerkDefinitionId FromString(std::string_view name)noexcept`
- `304299041f8631e4` | `QUERY` | `epidemic::gameplay::progression::ProgressionModifierId` | `[[nodiscard]] constexpr auto operator<=>(const ProgressionModifierId&)const noexcept=default;`
- `34b87a8a8adea307` | `QUERY` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] ProgressionChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `366f9356b0ca3bc5` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<void> RevokeUnlock(GameplayObjectRef subject,UnlockTypeId type,TypeId value,GameplayContext context={});`
- `369e326db757d773` | `QUERY` | `epidemic::gameplay::progression::IdHash` | `template <typename T> [[nodiscard]] std::size_t operator()(const T&id)const noexcept`
- `398102ffb1fd1253` | `QUERY` | `epidemic::gameplay::progression::ProgressionGrantReservationId` | `[[nodiscard]] constexpr auto operator<=>(const ProgressionGrantReservationId&)const noexcept=default;`
- `39d2e9ba80fc2980` | `QUERY` | `epidemic::gameplay::progression::PerkDefinitionId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `3a628aebdf781124` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<ProgressionTrackState> GrantProgress(GameplayObjectRef subject,ProgressionTrackId track,std::int64_t amount_micro,GameplayContext context={});`
- `3f4d8d8bc153390a` | `QUERY` | `epidemic::gameplay::progression::MilestoneId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `44f12e914e298f9e` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `void CommitProgressGrant(ProgressionGrantReservationId reservation)noexcept;`
- `457ba986797f5af4` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<void> SetBaseAttribute(GameplayObjectRef subject,AttributeTypeId attribute,std::int64_t value_micro,GameplayContext context={});`
- `48bddc28a3bbc8e1` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `void ReleaseProgressGrant(ProgressionGrantReservationId reservation)noexcept;`
- `51952e72deb442ab` | `QUERY` | `epidemic::gameplay::progression::PerkDefinitionId` | `[[nodiscard]] constexpr auto operator<=>(const PerkDefinitionId&)const noexcept=default;`
- `535312378cc015f4` | `QUERY` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `57b71699aa33c13e` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<std::vector<MilestoneId>> EvaluateMilestones(GameplayObjectRef subject,GameplayContext context={});`
- `59fe61de5f14791d` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<void> EnsureProfile(GameplayObjectRef subject,GameplayContext context={});`
- `5f9596075488f7cc` | `QUERY` | `epidemic::gameplay::progression::UnlockRecord` | `[[nodiscard]] bool operator==(const UnlockRecord&)const noexcept=default;`
- `5fee27872d06e075` | `FACTORY` | `epidemic::gameplay::progression::MilestoneId` | `[[nodiscard]] static constexpr MilestoneId FromString(std::string_view name)noexcept`
- `68069d92463cef89` | `QUERY` | `epidemic::gameplay::progression::ProgressionTrackId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `68928ebc5656d295` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<void> RemoveProfile(GameplayObjectRef subject,GameplayContext context={});`
- `7d6df1778c5b51f2` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<ProgressionModifierId> AddModifier(GameplayObjectRef subject,ProgressionModifier modifier,GameplayContext context={});`
- `7f801d5c29afe3ba` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<void> RemoveModifier(GameplayObjectRef subject,ProgressionModifierId modifier,GameplayContext context={});`
- `81421af06cb06eaa` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<ProgressionTrackState> SetProgress(GameplayObjectRef subject,ProgressionTrackId track,std::int64_t progress_micro,GameplayContext context={});`
- `83098b54094f3ce1` | `FACTORY` | `epidemic::gameplay::progression::ProgressionTrackId` | `[[nodiscard]] static constexpr ProgressionTrackId FromString(std::string_view name)noexcept`
- `85bc7a089ae4cb45` | `QUERY` | `epidemic::gameplay::progression::ProgressionModifierId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `88b8c52ed6d6cb55` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<std::vector<ProgressionModifierId>> ReplaceModifiersBySource(GameplayObjectRef subject,GameplayObjectRef source,std::vector<ProgressionModifier> modifiers,GameplayContext context={});`
- `8d31eb6b5c6aa101` | `QUERY` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] bool IsFrozen()const noexcept`
- `936baff0f9b3b471` | `QUERY` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] bool HasProfile(GameplayObjectRef subject)const noexcept;`
- `a7fa1e2e012833c4` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<void> RevokeUnlockFromSource(GameplayObjectRef subject,UnlockTypeId type,TypeId value,GameplayObjectRef source,GameplayContext context={});`
- `bf2ae86797a0ca59` | `FACTORY` | `epidemic::gameplay::progression::UnlockDefinitionId` | `[[nodiscard]] static constexpr UnlockDefinitionId FromString(std::string_view name)noexcept`
- `c2c7359e13c5afbb` | `QUERY` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] bool HasPerk(GameplayObjectRef subject,PerkDefinitionId perk)const noexcept;`
- `c43dfc7d292c9cbf` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<MilestoneId> RegisterMilestone(MilestoneDefinition definition);`
- `c62e3f919bbbe4c3` | `FACTORY` | `epidemic::gameplay::progression::AttributeTypeId` | `[[nodiscard]] static constexpr AttributeTypeId FromString(std::string_view name)noexcept`
- `c918d8fd9b52b724` | `QUERY` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`
- `cd8ba0f8c2c58701` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] std::uint64_t RemoveModifiersBySource(GameplayObjectRef subject,GameplayObjectRef source,GameplayContext context={});`
- `d09192fd35b67f11` | `QUERY` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] bool HasUnlock(GameplayObjectRef subject,UnlockTypeId type,TypeId value)const noexcept;`
- `d2323f094663fb53` | `QUERY` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] ProgressionSnapshot CaptureSnapshot()const;`
- `d396b702364fb613` | `QUERY` | `epidemic::gameplay::progression::AttributeTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `d5df374ee4e19c1a` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<void> GrantPerk(GameplayObjectRef subject,PerkDefinitionId perk,GameplayContext context={});`
- `df29837fd5fb5f31` | `QUERY` | `epidemic::gameplay::progression::MilestoneId` | `[[nodiscard]] constexpr auto operator<=>(const MilestoneId&)const noexcept=default;`
- `e265f3f811ddd5c6` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<ProgressionGrantReservationId> ReserveProgressGrant(GameplayObjectRef subject,ProgressionTrackId track,std::int64_t amount_micro,GameplayContext context={});`
- `e2d296366f85bfc7` | `QUERY` | `epidemic::gameplay::progression::UnlockTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `e5b4615c1b575811` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<UnlockDefinitionId> RegisterUnlockDefinition(UnlockDefinition definition);`
- `e6bfcb38fec94b25` | `FACTORY` | `epidemic::gameplay::progression::UnlockTypeId` | `[[nodiscard]] static constexpr UnlockTypeId FromString(std::string_view name)noexcept`
- `f15a165d90512aec` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<PerkDefinitionId> RegisterPerk(PerkDefinition definition);`
- `f1b355e07db5afa6` | `QUERY` | `epidemic::gameplay::progression::AttributeTypeId` | `[[nodiscard]] constexpr auto operator<=>(const AttributeTypeId&)const noexcept=default;`
- `f5c4f3a4fac243be` | `QUERY` | `epidemic::gameplay::progression::ProgressionTrackId` | `[[nodiscard]] constexpr auto operator<=>(const ProgressionTrackId&)const noexcept=default;`
- `f907d66159610b2a` | `QUERY` | `epidemic::gameplay::progression::UnlockTypeId` | `[[nodiscard]] constexpr auto operator<=>(const UnlockTypeId&)const noexcept=default;`
- `fadd0f86b6d22d2d` | `QUERY` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<ProgressionTrackState> GetTrack(GameplayObjectRef subject,ProgressionTrackId track)const;`
- `fae9c76002dd2497` | `LIFECYCLE` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<void> Freeze();`
- `fbb0e2418d8d6a78` | `QUERY` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<ProgressionProfileSnapshot> GetProfileSnapshot(GameplayObjectRef subject)const;`
- `fc25b51b829570d3` | `MUTATOR` | `epidemic::gameplay::progression::ProgressionService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(ProgressionSnapshot snapshot);`
