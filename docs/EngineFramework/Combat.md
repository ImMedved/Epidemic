# Combat Goal 4 local audit

Status: B06 worker review complete. This is block-local Goal 4 evidence only. Canonical `LOCAL_READY` promotion and whole-engine `FROZEN` remain serial/Goals 5-9 responsibilities.

## Responsibility

Owns semantic combatants, resources, life-state transitions, damage resolution plans, resource reservations and combat change journal.

## Public headers and types

Public surface is rooted at `EngineFramework/GameplayWorldStateOwners/Combat/include/Epidemic/GameFramework/Combat/combat.h`. B06 does not change this public header or its signatures.

## Dependency list

Direct production links are GameFramework Foundation, SupportRandom and EngineBase Foundation. No peer state-owner dependency is added.

## External ports/callbacks/providers/backends

Combat modifier/random inputs are borrowed ports. The service does not retain external authoritative combat state.

## Authoritative state

Combatant records and resource states, reservation records, ID generators, per-record revisions, resolution state and bounded change journal.

## Derived/cache/index state

Damage planning and modifier ordering are derived from definitions, combatant state and deterministic random input; diagnostics are non-authoritative.

## ID spaces, generations, revisions and cursors

Resolution/reservation IDs use service-owned monotonic generators. Record revision and journal cursor exhaustion are checked before state-changing publication.

## State machines

Combatant life transitions and reservation/resolve lifecycles are explicit. Failed resolution does not consume reservation or mutate resources.

## Persistent and transient state

Snapshot state preserves combatants, reservations, generators and journal continuity. External provider pointers and transient resolution scratch are not reusable identity.

## Snapshot/restore contract

Restore validates definitions, identities, resource bounds, reservations, revisions, generators and journal continuity before replacing live state.

## Threading contract

Owner-thread/external-serialization gameplay state. Goal 6 remains responsible for concurrency qualification.

## Public mutation API

Register/remove combatants, modify/set resources, prepare/commit/cancel damage and reservation paths preflight identity/revision/lifecycle constraints. Preserve-ratio maximum changes use portable exact signed integer mul/div with truncation toward zero, fixing `G4-COMBAT-001` near int64 boundaries.

## Read/query API for invariants

Combatant/resource lookup, diagnostics and change reads witness resource bounds, reservation ownership, life state and deterministic publication order.

## Local invariants

A resource current value remains within its semantic minimum/maximum; one reservation has one owner/target; failed mutation leaves resource/revision/generator state unchanged; accepted damage commit is not replayed by retry.

## Hard limits, budgets and complexity bounds

Int64 resource arithmetic saturates/validates at semantic bounds. Change journal retention is bounded and exhaustion has explicit snapshot-required behavior.

## Test evidence

`EngineFramework/DevelopmentInfrastructure/Tests/combat_tests.cpp` covers resource/life transitions, reservations, damage resolution, restore and revision exhaustion. The B06 regression for `G4-COMBAT-001` exercises the concrete `INT64_MAX` preserve-ratio boundary. Target: `EpidemicGameFrameworkCombatTests`.

## Exact public API anchors

Each row is the exact reviewed contract for one B06 callable. The matching assertion is recorded in `_goal4_handoff/B06/public_api_anchors.json`.

- `0b04e28ad9b86e07` | `MUTATOR` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] foundation::Result<void> RemoveCombatant(GameplayObjectRef subject,GameplayContext context={});`
- `18894cac088fa694` | `QUERY` | `epidemic::gameplay::combat::CombatResourceTypeId` | `[[nodiscard]] constexpr auto operator<=>(const CombatResourceTypeId&)const noexcept=default;`
- `1e50e311ecf2ac63` | `QUERY` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `2c4696eb414095d3` | `QUERY` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`
- `3189a068754baf42` | `QUERY` | `epidemic::gameplay::combat::CombatResolutionId` | `[[nodiscard]] constexpr auto operator<=>(const CombatResolutionId&)const noexcept=default;`
- `3276c8786df6c30b` | `MUTATOR` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] foundation::Result<CombatResult> CommitDamage(const CombatPlan&plan,GameplayTimePoint now);`
- `3c444f6fd311429d` | `MUTATOR` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] foundation::Result<void> SetEngagement(GameplayObjectRef subject,CombatEngagementState state,GameplayContext context={});`
- `4179c29f1c417510` | `CONSTRUCTOR` | `epidemic::gameplay::combat::CombatService` | `CombatService();`
- `43cb3ba2ee6f4313` | `QUERY` | `epidemic::gameplay::combat::CombatResolutionId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `4915dcf382f8c25e` | `QUERY` | `epidemic::gameplay::combat::DamageProfileId` | `[[nodiscard]] constexpr auto operator<=>(const DamageProfileId&)const noexcept=default;`
- `4a9eab714f7ccd04` | `QUERY` | `epidemic::gameplay::combat::DamageProfileId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `4f1fc43f8b9c60b3` | `MUTATOR` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] foundation::Result<CombatResourceReservationId> ReserveResource(GameplayObjectRef subject,CombatResourceTypeId type,std::int64_t amount_micro,GameplayContext context={});`
- `4f338394cdaf3794` | `QUERY` | `epidemic::gameplay::combat::IdHash` | `template <typename T> [[nodiscard]] std::size_t operator()(const T&id)const noexcept`
- `50a6c3aa0b4dd8e8` | `FACTORY` | `epidemic::gameplay::combat::CombatResourceTypeId` | `[[nodiscard]] static constexpr CombatResourceTypeId FromString(std::string_view name)noexcept`
- `50bacd2a069087de` | `QUERY` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] const CombatantRecord*FindCombatant(GameplayObjectRef subject)const noexcept;`
- `5220a993b976bd81` | `QUERY` | `epidemic::gameplay::combat::CombatModifierTypeId` | `[[nodiscard]] constexpr auto operator<=>(const CombatModifierTypeId&)const noexcept=default;`
- `531f8b4684886d15` | `QUERY` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] bool IsFrozen()const noexcept`
- `5db10fccdb905650` | `FACTORY` | `epidemic::gameplay::combat::DamageTypeId` | `[[nodiscard]] static constexpr DamageTypeId FromString(std::string_view name)noexcept`
- `5f6e6893b20a48c6` | `FACTORY` | `epidemic::gameplay::combat::DamageProfileId` | `[[nodiscard]] static constexpr DamageProfileId FromString(std::string_view name)noexcept`
- `62b552ffab6dfb55` | `QUERY` | `epidemic::gameplay::combat::ICombatModifierProvider` | `[[nodiscard]] virtual std::vector<CombatModifier> Collect(const DamageRequest&request)const=0;`
- `6d0369d31524eeb0` | `LIFECYCLE` | `epidemic::gameplay::combat::CombatService` | `void Freeze()noexcept`
- `73eff8b1329a01f5` | `MUTATOR` | `epidemic::gameplay::combat::CombatService` | `void ReleaseResourceReservation(CombatResourceReservationId reservation,GameplayContext context={})noexcept;`
- `77d9cf2cca2ade53` | `QUERY` | `epidemic::gameplay::combat::CombatResourceTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `79c3ee7c77bb602f` | `QUERY` | `epidemic::gameplay::combat::ICombatModifierProvider` | `[[nodiscard]] virtual Revision RevisionFor(const DamageRequest&request)const noexcept=0;`
- `7e0e3a772bed4b4b` | `MUTATOR` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] foundation::Result<void> SetPreparePolicy(CombatPreparePolicy policy);`
- `8372c7332b9dd2b9` | `MUTATOR` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] foundation::Result<void> CancelDamagePlan(CombatResolutionId id);`
- `8d44326a65f70d35` | `QUERY` | `epidemic::gameplay::combat::DamageTypeId` | `[[nodiscard]] constexpr auto operator<=>(const DamageTypeId&)const noexcept=default;`
- `8d5b18ac6aedd58c` | `QUERY` | `epidemic::gameplay::combat::CombatResourceReservationId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `913b114757040d86` | `QUERY` | `epidemic::gameplay::combat::DamageTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `99a6474fbff3321f` | `QUERY` | `epidemic::gameplay::combat` | `[[nodiscard]] constexpr CombatOutcome operator|(CombatOutcome a,CombatOutcome b)noexcept`
- `9abfd47c399f2705` | `FACTORY` | `epidemic::gameplay::combat::CombatModifierTypeId` | `[[nodiscard]] static constexpr CombatModifierTypeId FromString(std::string_view name)noexcept`
- `9ec1da17c37c8bd3` | `MUTATOR` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] foundation::Result<DamageTypeId> RegisterDamageType(std::string canonical_name);`
- `a3b6d675e4d620f0` | `MUTATOR` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] foundation::Result<void> RegisterCombatant(GameplayObjectRef subject,std::vector<CombatResourceState> resources,GameplayContext context={});`
- `a76bcf13973499f3` | `QUERY` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] const CombatResourceReservation*FindResourceReservation(CombatResourceReservationId reservation)const noexcept;`
- `a9887446a52f8f95` | `QUERY` | `epidemic::gameplay::combat::CombatResourceReservationId` | `[[nodiscard]] constexpr auto operator<=>(const CombatResourceReservationId&)const noexcept=default;`
- `aa326b945f49ef11` | `QUERY` | `epidemic::gameplay::combat` | `[[nodiscard]] constexpr bool HasOutcome(CombatOutcome value,CombatOutcome flag)noexcept`
- `b279f9c60d847755` | `MUTATOR` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] foundation::Result<DamageProfileId> RegisterDamageProfile(DamageProfile profile);`
- `b8fc5cbb64bd4343` | `QUERY` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] CombatChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `bca047a0f645454b` | `DESTRUCTOR` | `epidemic::gameplay::combat::ICombatModifierProvider` | `virtual ~ICombatModifierProvider()=default;`
- `c325d83c2fa6d7fd` | `MUTATOR` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] foundation::Result<void> TransitionLifeState(GameplayObjectRef subject,CombatLifeState state,GameplayContext context={});`
- `c72f6ae17990af67` | `MUTATOR` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] foundation::Result<void> SetResourceMaximum(GameplayObjectRef subject,CombatResourceTypeId type,std::int64_t maximum_micro,bool preserve_ratio,GameplayContext context={});`
- `cae0b9d8f622ea66` | `MUTATOR` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] foundation::Result<CombatPlan> PrepareDamage(DamageRequest request);`
- `cc02e0d3359674f5` | `MUTATOR` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] foundation::Result<void> ModifyResource(GameplayObjectRef subject,CombatResourceTypeId type,std::int64_t delta_micro,GameplayContext context={});`
- `ce10991909e5ba8b` | `MUTATOR` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] foundation::Result<CombatResourceTypeId> RegisterResource(CombatResourceDefinition definition);`
- `d140baa39e9874e7` | `QUERY` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] CombatPreparePolicy GetPreparePolicy()const noexcept`
- `df9c5aa7ad2e9e3d` | `QUERY` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] CombatDiagnostics GetDiagnostics()const noexcept;`
- `e86013c31ddf1986` | `QUERY` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] CombatSnapshot CaptureSnapshot()const;`
- `ec35396d58ed6eb9` | `MUTATOR` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] std::size_t ExpirePreparedPlans(GameplayTimePoint now)noexcept;`
- `f248e5e4ce3a6994` | `MUTATOR` | `epidemic::gameplay::combat::CombatService` | `void SetModifierProvider(const ICombatModifierProvider*provider)noexcept;`
- `f2db96c54c715c36` | `QUERY` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] foundation::Result<CombatResourceState> GetResource(GameplayObjectRef subject,CombatResourceTypeId type)const;`
- `f8f3967c3d4e0d9c` | `MUTATOR` | `epidemic::gameplay::combat::CombatService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(CombatSnapshot snapshot);`
- `ff40e4496ee76a3d` | `MUTATOR` | `epidemic::gameplay::combat::CombatService` | `void CommitResourceReservation(CombatResourceReservationId reservation)noexcept;`
