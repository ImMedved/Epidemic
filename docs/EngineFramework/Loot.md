# Loot Goal 4 B03 local audit

Status: block-local `LOCAL_READY` projection for Goal 4. This is not a whole-Framework `FROZEN` claim.

## Responsibility and ownership

Owns loot tables, generated rewards, pending delivery records, delivery lifecycle, explicit random-source results, revisions/generators and the loot journal.

The service remains the single authoritative owner of the state listed below. Derived indexes and journals are not independent semantic owners.

## Authoritative state and indexes

Admission inventory: 52 public callables, 64 mutation obligations, 1 lifecycle candidates, 15 stale-identity candidates and 0 external-boundary candidates.

Primary records, secondary indexes, ID generators, revision/change sequence and bounded journal state are reviewed together. Mutations that span several containers must complete all fallible staging before the no-fail authoritative commit point. Query ordering and index-derived views are required to agree with primary state.

## Public contracts and failure atomicity

Every admission callable below is classified and reviewed. Mutators have success, no-op, invalid/precondition and failure decisions in the B03 coverage projection. Revision, ID-generator and journal publication is part of the mutation contract. Allocation/publication failure must not expose a partially advanced generator, revision, primary record, derived index or journal entry.

B03 allocation evidence uses source-private module seams rather than the shared process-global allocator override. These seams are test-only implementation details and do not expand the public API.

## Lifecycle and identity

Lifecycle candidates and stale identity candidates from the admission inventory were reviewed. Invalid, removed, duplicate and stale IDs are required to be rejected or treated as the documented no-op without publishing false state. Terminal records are not silently resurrected by retry.

## Persistence

Snapshot/restore is reviewed as a candidate-state operation: validate first, build off-state, preserve all live state on failure, then publish the candidate. Successful restore preserves the persistent ID-generator/revision/journal boundary represented by the snapshot contract.

## External boundaries

Reward handler prepare/commit/cancel and reconciliation are explicit ports. Admission emitted no separate external-boundary candidate rows.

External callback/provider failure is converted to the module error/result contract. Retry must not duplicate an already committed semantic side effect, and failed compensation that requires reconciliation stays represented by owned state.

## Threading contract

No additional internal synchronization guarantee is introduced by B03. These owners are treated as externally serialized/owner-thread services for local correctness evidence. Later Goal 6 performs the engine-wide concurrency qualification.

## Regression evidence

Registered module target: `EpidemicGameFrameworkLootTests` from `EngineFramework/DevelopmentInfrastructure/Tests/loot_tests.cpp`.


## Goal 4 functional checklist

- [x] Loot table/definition registration.
- [x] Generate deterministic output from explicit random source.
- [x] Generated, pending, delivered, cancelled lifecycle.
- [x] Reward handler prepare/commit/cancel.
- [x] Discard generated.
- [x] Duplicate delivery protection.
- [x] External reward failure and reconciliation.
- [x] Snapshot/restore pending deliveries, generators, journal.

## Exact public API anchors

Each row is the block-local contract anchor for one admission callable. The matching test anchor is stored in `_goal4_handoff/B03/public_api_anchors.json`.

- `02ba1d189b5f35b0` | `MUTATOR` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] foundation::Result<RewardDefinitionId> RegisterRewardDefinition(RewardDefinition definition);`
- `0f80c6f79ff34dc7` | `FACTORY` | `epidemic::gameplay::loot::RewardDefinitionId` | `[[nodiscard]] static constexpr RewardDefinitionId FromString(std::string_view n)noexcept`
- `1a885002b928f50e` | `MUTATOR` | `epidemic::gameplay::loot::IRewardHandler` | `[[nodiscard]] virtual foundation::Result<RewardDeliveryStage> Prepare(const RewardOperation&operation)=0;`
- `1c61a015a49757a5` | `QUERY` | `epidemic::gameplay::loot::RewardDefinitionId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `1cdb6ad26dd8b367` | `MUTATOR` | `epidemic::gameplay::loot::IRewardHandler` | `virtual void Cancel(RewardDeliveryStage&stage)noexcept=0;`
- `1d287b2b619ec4b4` | `MUTATOR` | `epidemic::gameplay::loot::LootService` | `void SetConditionProvider(const ILootConditionProvider*provider)noexcept`
- `1fcb09d83a1f1d51` | `QUERY` | `epidemic::gameplay::loot::RewardTypeId` | `[[nodiscard]] constexpr auto operator<=>(const RewardTypeId&)const noexcept=default;`
- `22fa17a8613c49dc` | `QUERY` | `epidemic::gameplay::loot::ILootConditionProvider` | `[[nodiscard]] virtual bool Evaluate(TypeId condition,const LootContext&context)const=0;`
- `237ee50737014fd0` | `QUERY` | `epidemic::gameplay::loot::RewardTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `2a94d77eebc9adc8` | `QUERY` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] foundation::Result<RewardExecutionId> MakePending(RewardExecutionId reward,std::optional<GameplayTimePoint> expires_at={});`
- `3846578b14d5e4e7` | `MUTATOR` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] foundation::Result<void> Expire(RewardExecutionId reward,GameplayTimePoint now,GameplayContext context={});`
- `41708044c7a95a67` | `MUTATOR` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] foundation::Result<void> DiscardGenerated(RewardExecutionId reward,GameplayContext context={});`
- `4759cdf47d9aae28` | `QUERY` | `epidemic::gameplay::loot::LootEntryId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `504cacd37f5c6354` | `QUERY` | `epidemic::gameplay::loot::LootQualityId` | `[[nodiscard]] constexpr auto operator<=>(const LootQualityId&)const noexcept=default;`
- `517caa670dddd97c` | `QUERY` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] RewardClaimHistoryStatus ClaimHistoryStatus(RewardExecutionId reward)const noexcept;`
- `5276ec161a2f0034` | `QUERY` | `epidemic::gameplay::loot::LootEntryId` | `[[nodiscard]] constexpr auto operator<=>(const LootEntryId&)const noexcept=default;`
- `54c74647513390d6` | `QUERY` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] bool WasClaimed(RewardExecutionId reward)const noexcept;`
- `5545459f8d4d4f11` | `FACTORY` | `epidemic::gameplay::loot::LootEntryId` | `[[nodiscard]] static constexpr LootEntryId FromString(std::string_view n)noexcept`
- `5de8c2b1fbd50fc3` | `MUTATOR` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] foundation::Result<RewardBundle> GenerateTracked(LootTableId table,LootContext context);`
- `5f1a2616e856e1ad` | `QUERY` | `epidemic::gameplay::loot::LootQualityId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `618d53bd942c0316` | `MUTATOR` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] foundation::Result<RewardBundle> Generate(LootTableId table,LootContext context)`
- `63bd2e719c8ae175` | `FACTORY` | `epidemic::gameplay::loot::LootTableId` | `[[nodiscard]] static constexpr LootTableId FromString(std::string_view n)noexcept`
- `6478d8b02b0b063f` | `DESTRUCTOR` | `epidemic::gameplay::loot::ILootConditionProvider` | `virtual ~ILootConditionProvider()=default;`
- `69682271b4cd6d09` | `QUERY` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] foundation::Result<RewardExecutionId> MakePending(RewardBundle bundle,std::optional<GameplayTimePoint> expires_at={});`
- `6ce923d7dafb0bda` | `QUERY` | `epidemic::gameplay::loot::LootTableId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `72e5cfe7c118ac8b` | `QUERY` | `epidemic::gameplay::loot::LootTableId` | `[[nodiscard]] constexpr auto operator<=>(const LootTableId&)const noexcept=default;`
- `7815f92a880f2e8e` | `QUERY` | `epidemic::gameplay::loot::RewardExecutionId` | `[[nodiscard]] constexpr auto operator<=>(const RewardExecutionId&)const noexcept=default;`
- `7c34808b89806740` | `QUERY` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] const RewardBundle*FindGenerated(RewardExecutionId reward)const noexcept;`
- `7d9ddccf8e0dea15` | `DESTRUCTOR` | `epidemic::gameplay::loot::IRewardHandler` | `virtual ~IRewardHandler()=default;`
- `8021d07e0599dea8` | `QUERY` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `8c51e4dc50540fe8` | `MUTATOR` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] foundation::Result<void> BindSchedule(RewardExecutionId reward,ScheduleId schedule);`
- `9214f55c568c3fb4` | `QUERY` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] foundation::Result<RewardBundle> Preview(LootTableId table,LootContext context);`
- `9ba2961d4be78b6a` | `QUERY` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] LootDiagnostics GetDiagnostics()const noexcept;`
- `a0f1f607bb942f25` | `MUTATOR` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] foundation::Result<void> Cancel(RewardExecutionId reward,GameplayContext context={});`
- `a2f9fbe6eab144d7` | `MUTATOR` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] foundation::Result<LootTableId> RegisterLootTable(LootTableDefinition definition);`
- `a6f14c14b05ca0f3` | `FACTORY` | `epidemic::gameplay::loot::LootQualityId` | `[[nodiscard]] static constexpr LootQualityId FromString(std::string_view n)noexcept`
- `a9fd9f91365dd81f` | `LIFECYCLE` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] foundation::Result<void> Freeze();`
- `b3913fdfb9783792` | `QUERY` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`
- `b84f6e0b61780fed` | `MUTATOR` | `epidemic::gameplay::loot::IRewardHandler` | `virtual void Commit(RewardDeliveryStage&stage)noexcept=0;`
- `bb4e6788327eeee0` | `FACTORY` | `epidemic::gameplay::loot::RewardTypeId` | `[[nodiscard]] static constexpr RewardTypeId FromString(std::string_view n)noexcept`
- `be29ea40b24f323e` | `CONSTRUCTOR` | `epidemic::gameplay::loot::LootService` | `LootService();`
- `c76be6a6d18b4d01` | `QUERY` | `epidemic::gameplay::loot::IRewardHandler` | `[[nodiscard]] virtual RewardTypeId Type()const noexcept=0;`
- `c7b1977bc2437c39` | `QUERY` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] std::vector<PendingReward> AllPending()const;`
- `cd72c1c79915a50f` | `QUERY` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] const PendingReward*FindPending(RewardExecutionId reward)const noexcept;`
- `cee4eca398c8fc17` | `QUERY` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] std::vector<PendingReward> PendingFor(GameplayObjectRef recipient)const;`
- `d2c58e4050c11d48` | `MUTATOR` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] foundation::Result<void> Claim(RewardExecutionId reward,GameplayContext context={});`
- `d326be7cdb1651bc` | `QUERY` | `epidemic::gameplay::loot::RewardExecutionId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `d74dac689d1ea326` | `QUERY` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] LootChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `d863e5713bc336d6` | `MUTATOR` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(LootSnapshot snapshot);`
- `e22ed50b638f1bfa` | `MUTATOR` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] foundation::Result<RewardTypeId> RegisterRewardHandler(std::string canonical_name,IRewardHandler*handler);`
- `e5540563e5898f82` | `QUERY` | `epidemic::gameplay::loot::LootService` | `[[nodiscard]] LootSnapshot CaptureSnapshot()const;`
- `f60df2d8ff677ee1` | `QUERY` | `epidemic::gameplay::loot::RewardDefinitionId` | `[[nodiscard]] constexpr auto operator<=>(const RewardDefinitionId&)const noexcept=default;`

## Local-ready projection

All 37 Goal 4 local criteria have a block-local `PASS` decision with concrete contract/state/test evidence in `_goal4_handoff/B03/local_ready.json`. All 15 dossier fields are `REVIEWED`. Canonical `docs/freeze/**` regeneration remains the serial integrator step after all eight Bxx deltas are merged.
