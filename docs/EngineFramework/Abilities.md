# Abilities Goal 4 local audit

Status: B06 worker review complete. This document is module-local Goal 4 evidence and does not by itself promote canonical status.

## Responsibility

Owns ability definitions/instances, activation executions, cooldowns, schedule bindings, resource reservation lifecycle, outputs and ability change journal.

## Public headers and types

Public surface is rooted at `EngineFramework/GameplayWorldStateOwners/Abilities/include/Epidemic/GameFramework/Abilities/abilities.h`. Public headers/signatures are unchanged by B06.

## Dependency list

Direct production links are GameFramework Foundation and EngineBase Foundation only.

## External ports/callbacks/providers/backends

Requirement/materialization/resource providers are borrowed ports. Resource reserve/commit/release is coordinated locally without taking provider ownership.

## Authoritative state

Definitions, instances, executions, schedule index, cooldowns, ID generators, reconciliation flags and bounded change journal.

## Derived/cache/index state

Schedule-to-execution mapping is a derived consistency index over active execution schedule identity. Diagnostics are non-authoritative.

## ID spaces, generations, revisions and cursors

Instance/execution IDs use monotonic generators. Execution revision is checked before terminal and binding updates. Journal sequence is preflighted for mutations that publish changes.

## State machines

Activation covers casting/executing/channeling and terminal completed/cancelled/interrupted/failed states. Resource reservations have reserve, commit and release paths; restored reservations require explicit reconciliation.

## Persistent and transient state

Snapshot preserves instances, live executions, cooldowns, schedules, generators and journal continuity. Provider pointers are transient.

## Snapshot/restore contract

Restore validates definitions, live execution state, unique schedules/reservations, generator positions and journal continuity using candidate staging before a no-fail swap. Private `RestoreCandidateBuild` and `RestoreBeforeCommit` seams prove failed-restore preservation.

## Threading contract

Owner-thread/external-serialization state. No stronger thread-safety claim is introduced by Goal 4.

## Public mutation API

Grant/revoke/enable, begin/complete/channel/cancel/interrupt, schedule binding and cooldown updates check lifecycle/revision and resource preconditions. B06 replaces process-global allocation injection with private seams around schedule publication, activation after provider reservation, output preparation and restore publication.

## Read/query API for invariants

Definition/instance/execution lookup, availability, cooldown and snapshot/change queries witness state-machine and index consistency.

## Local invariants

A schedule maps to at most one live execution; provider reservations are released on failed local publication; output staging fails before external resource commit.

## Hard limits, budgets and complexity bounds

Revision, change sequence and generator exhaustion are explicit. Change journal retention is bounded.

## Test evidence

`EngineFramework/DevelopmentInfrastructure/Tests/abilities_tests.cpp` covers activation lifecycle, provider/reconciliation behavior, schedule binding, revision exhaustion and snapshot validation. Goal 4 adds deterministic private-fault regressions for binding, post-reservation activation, output preparation, journal append and restore staging. Target: `EpidemicGameFrameworkAbilitiesTests`.

## Exact public API anchors

Each row is the exact reviewed contract for one B06 callable. The matching assertion is recorded in `_goal4_handoff/B06/public_api_anchors.json`.

- `0225a34b29e3b3eb` | `MUTATOR` | `epidemic::gameplay::abilities::IAbilityResourceProvider` | `[[nodiscard]] virtual foundation::Result<AbilityResourceReservation> Reserve(GameplayObjectRef owner,AbilityResourceTypeId type,std::int64_t amount_micro,GameplayContext context)=0;`
- `02f07f200e565351` | `QUERY` | `epidemic::gameplay::abilities::AbilityResourceTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `1786dd7c2246ed81` | `MUTATOR` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] foundation::Result<void> Interrupt(AbilityExecutionId execution,TypeId reason,GameplayTimePoint now,GameplayContext context={});`
- `1af82f202b2c9d02` | `QUERY` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] GameplayDuration CooldownRemaining(GameplayObjectRef owner,CooldownGroupId group,GameplayTimePoint now)const noexcept;`
- `295da57eab52dcba` | `MUTATOR` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] foundation::Result<void> SetAbilityEnabled(AbilityInstanceId instance,bool enabled,GameplayContext context={});`
- `29b6622eedce00e1` | `QUERY` | `epidemic::gameplay::abilities::AbilityReservationId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `2cbb75ea2f06ee8a` | `MUTATOR` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] foundation::Result<void> NotifyScheduleDue(ScheduleId schedule,GameplayTimePoint now,std::vector<AbilityOutput>&outputs);`
- `41ad30c378db10b2` | `QUERY` | `epidemic::gameplay::abilities::AbilityInstanceId` | `[[nodiscard]] constexpr auto operator<=>(const AbilityInstanceId&)const noexcept=default;`
- `4461466bc688dffe` | `QUERY` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] AbilityAvailabilityResult CanActivate(AbilityInstanceId ability,const AbilityTargetSet&targets,GameplayTimePoint now)const;`
- `4bb55b138d88e004` | `QUERY` | `epidemic::gameplay::abilities::AbilityInstanceId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `4cf5f05df2861aef` | `QUERY` | `epidemic::gameplay::abilities::CooldownGroupId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `4d18d57b44d80149` | `MUTATOR` | `epidemic::gameplay::abilities::AbilityService` | `void SetMaterializationProvider(const IAbilityMaterializationProvider*provider)noexcept`
- `4dc79d665e8a8377` | `QUERY` | `epidemic::gameplay::abilities::AbilityResourceTypeId` | `[[nodiscard]] constexpr auto operator<=>(const AbilityResourceTypeId&)const noexcept=default;`
- `4ea0923f3d9dce3f` | `MUTATOR` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(AbilitiesSnapshot snapshot);`
- `504f441ac7bab568` | `MUTATOR` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] foundation::Result<AbilityExecutionId> BeginActivation(AbilityActivationRequest request);`
- `52d5afe0e3eccf00` | `FACTORY` | `epidemic::gameplay::abilities::AbilityResourceTypeId` | `[[nodiscard]] static constexpr AbilityResourceTypeId FromString(std::string_view n)noexcept`
- `55aa3a7f225f9f90` | `DESTRUCTOR` | `epidemic::gameplay::abilities::IAbilityMaterializationProvider` | `virtual ~IAbilityMaterializationProvider()=default;`
- `5a6b47f7ad0c197d` | `QUERY` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] const AbilityDefinition*FindDefinition(AbilityDefinitionId id)const noexcept;`
- `6c1221b672c44b9f` | `QUERY` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] const AbilityInstance*FindInstance(AbilityInstanceId id)const noexcept;`
- `7056cbf37e404bfa` | `QUERY` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `728c316907c40b3d` | `MUTATOR` | `epidemic::gameplay::abilities::AbilityService` | `void SetResourceProvider(IAbilityResourceProvider*provider)noexcept`
- `75e18540beccda17` | `DESTRUCTOR` | `epidemic::gameplay::abilities::IAbilityResourceProvider` | `virtual ~IAbilityResourceProvider()=default;`
- `880d477033e52145` | `FACTORY` | `epidemic::gameplay::abilities::CooldownGroupId` | `[[nodiscard]] static constexpr CooldownGroupId FromString(std::string_view n)noexcept`
- `88b6481bfa4a9cbf` | `QUERY` | `epidemic::gameplay::abilities::AbilityDefinitionId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `90eea5506fd23f4a` | `QUERY` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] bool NeedsResourceReconciliation()const noexcept`
- `92f9f440dbee0346` | `MUTATOR` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] foundation::Result<AbilityInstanceId> Grant(GameplayObjectRef owner,AbilityDefinitionId definition,GameplayObjectRef source={},AbilityGrantPersistence persistence=AbilityGrantPersistence::Permanent,GameplayContext context={});`
- `939e6e9a86302b5b` | `QUERY` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] std::vector<AbilityInstance> GetAbilities(GameplayObjectRef owner)const;`
- `978c88572b2177b4` | `QUERY` | `epidemic::gameplay::abilities::IAbilityRequirementProvider` | `[[nodiscard]] virtual AbilityAvailabilityResult Check(const AbilityDefinition&definition,const AbilityInstance&instance,const AbilityTargetSet&targets,GameplayTimePoint now)const=0;`
- `9d0ef8f3b09a00c9` | `MUTATOR` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] std::uint64_t RevokeBySource(GameplayObjectRef owner,GameplayObjectRef source,GameplayContext context={});`
- `a3a68b803c58d7d5` | `MUTATOR` | `epidemic::gameplay::abilities::AbilityService` | `void SweepCooldowns(GameplayTimePoint now,GameplayContext context={});`
- `a3af8e5fe4a2c1dd` | `QUERY` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`
- `a8d5abb2f2b03130` | `MUTATOR` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] foundation::Result<void> ReconcileRestoredReservations(GameplayContext context={});`
- `aec16ff342390e0f` | `QUERY` | `epidemic::gameplay::abilities::AbilityExecutionId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `aff498c0d53df373` | `MUTATOR` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] foundation::Result<AbilityDefinitionId> RegisterDefinition(AbilityDefinition definition);`
- `b05e734825cc6ad3` | `DESTRUCTOR` | `epidemic::gameplay::abilities::IAbilityRequirementProvider` | `virtual ~IAbilityRequirementProvider()=default;`
- `b270cbcf3eabf3a0` | `QUERY` | `epidemic::gameplay::abilities::AbilityDefinitionId` | `[[nodiscard]] constexpr auto operator<=>(const AbilityDefinitionId&)const noexcept=default;`
- `b762ee98519c845d` | `MUTATOR` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] foundation::Result<std::vector<AbilityOutput>> CompleteExecution(AbilityExecutionId execution,GameplayTimePoint now);`
- `b79641575d23ce40` | `MUTATOR` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] foundation::Result<void> Cancel(AbilityExecutionId execution,GameplayTimePoint now,GameplayContext context={});`
- `b7a8d2c272cfb7fe` | `QUERY` | `epidemic::gameplay::abilities::IAbilityResourceProvider` | `[[nodiscard]] virtual bool CanAfford(GameplayObjectRef owner,AbilityResourceTypeId type,std::int64_t amount_micro)const=0;`
- `bfa5b15700b70286` | `QUERY` | `epidemic::gameplay::abilities::AbilityExecutionId` | `[[nodiscard]] constexpr auto operator<=>(const AbilityExecutionId&)const noexcept=default;`
- `c0d4a2d6d01bb6bc` | `QUERY` | `epidemic::gameplay::abilities::IAbilityMaterializationProvider` | `[[nodiscard]] virtual bool IsMaterialized(GameplayObjectRef subject)const noexcept=0;`
- `c19c8c96d25fe86a` | `MUTATOR` | `epidemic::gameplay::abilities::IAbilityResourceProvider` | `[[nodiscard]] virtual foundation::Result<void> ReconcileReservation(const AbilityResourceReservation&reservation,GameplayObjectRef owner,GameplayContext context)`
- `c2302cc0b9e3ff0c` | `QUERY` | `epidemic::gameplay::abilities::AbilityReservationId` | `[[nodiscard]] constexpr auto operator<=>(const AbilityReservationId&)const noexcept=default;`
- `c673ee0c206b0484` | `QUERY` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] AbilitiesSnapshot CaptureSnapshot()const;`
- `c7757b523f261775` | `QUERY` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] AbilitiesDiagnostics GetDiagnostics()const noexcept;`
- `c9a159a2b67f2263` | `QUERY` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] const AbilityExecution*FindExecution(AbilityExecutionId id)const noexcept;`
- `ca5d6344a338c68a` | `MUTATOR` | `epidemic::gameplay::abilities::IAbilityResourceProvider` | `virtual void Release(const AbilityResourceReservation&reservation,GameplayContext context)noexcept=0;`
- `cc67046b9c20ee73` | `CONSTRUCTOR` | `epidemic::gameplay::abilities::AbilityService` | `AbilityService();`
- `db5b4ebc1ac3e439` | `MUTATOR` | `epidemic::gameplay::abilities::AbilityService` | `void SetRequirementProvider(const IAbilityRequirementProvider*provider)noexcept`
- `e0af72471edc07f4` | `MUTATOR` | `epidemic::gameplay::abilities::IAbilityResourceProvider` | `virtual void Commit(const AbilityResourceReservation&reservation,GameplayContext context)noexcept=0;`
- `e129d2b27fa15e01` | `LIFECYCLE` | `epidemic::gameplay::abilities::AbilityService` | `void Freeze()noexcept`
- `e77cf002b3183453` | `MUTATOR` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] foundation::Result<void> BindSchedule(AbilityExecutionId execution,ScheduleId schedule);`
- `e97b47afb19dcbd8` | `QUERY` | `epidemic::gameplay::abilities::CooldownGroupId` | `[[nodiscard]] constexpr auto operator<=>(const CooldownGroupId&)const noexcept=default;`
- `ee7395bc2d6ce937` | `MUTATOR` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] foundation::Result<std::vector<AbilityOutput>> ChannelTick(AbilityExecutionId execution,GameplayTimePoint now,std::uint64_t occurrences=1);`
- `f57fda32b6af7a44` | `FACTORY` | `epidemic::gameplay::abilities::AbilityDefinitionId` | `[[nodiscard]] static constexpr AbilityDefinitionId FromString(std::string_view n)noexcept`
- `f9812fa816ac47b6` | `QUERY` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] AbilityChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `fe12cd56d3cd3ee2` | `MUTATOR` | `epidemic::gameplay::abilities::AbilityService` | `[[nodiscard]] foundation::Result<void> Revoke(AbilityInstanceId instance,GameplayContext context={});`
