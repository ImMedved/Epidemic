# Encounters Goal 4 local audit

## Scope

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Encounters`. Admission: 70 public callables, 68 mutation obligations, 2 lifecycle candidates, 14 stale-identity candidates, 0 external-boundary candidates.

## Public contract review

- Definition/spawn-table registration and validation.
- Encounter active/terminal lifecycle and spawn request lifecycle.
- Spawn point/entity records, respawn rules and budget limits.
- Deterministic spawn selection through explicit random streams.
- Terminal pruning, stale identities and journal gap semantics.
- Snapshot/restore validates generators/references before swap.

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

Primary regression source: `EngineFramework/DevelopmentInfrastructure/Tests/encounters_tests.cpp`.
Registered target: `EpidemicGameFrameworkEncountersTests`.
Local Linux verification in the worker environment used direct C++23 compilation with `-Wall -Wextra -Wpedantic -Werror` and executed the module test binary. The repository top-level CMake configure is Windows-only and therefore cannot be used in this Linux worker environment.

## Goal 4 result

Block-local review result: LOCAL_READY candidate for serial integration. This document does not claim whole-Framework `LOCAL_READY`, `SYSTEM_READY` or `FROZEN`.

## Exact public API anchors

Each row is the exact reviewed contract for one B04 callable. The matching assertion is recorded in `_goal4_handoff/B04/public_api_anchors.json`.

- `03ebb494449cd2b4` | `FACTORY` | `epidemic::gameplay::encounters::EncounterInstanceId` | `static constexpr EncounterInstanceId FromString(std::string_view s)noexcept`
- `05d39a567f3106dd` | `MUTATOR` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] foundation::Result<void> ExpireEncounter(EncounterInstanceId id,GameplayContext context={});`
- `0ad20ad2c0795537` | `QUERY` | `epidemic::gameplay::encounters::SpawnRequestId` | `[[nodiscard]] constexpr bool operator==(const SpawnRequestId&)const noexcept=default;`
- `107586ffb81f68e3` | `QUERY` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] const SpawnTable*GetSpawnTable(SpawnTableId id)const noexcept;`
- `155289d3a7a51504` | `QUERY` | `epidemic::gameplay::encounters::SpawnTableId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `17c2f5c63a042b89` | `QUERY` | `epidemic::gameplay::encounters::SpawnRequestId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `1d17015a2655fe2f` | `MUTATOR` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] SpawnResult SpawnEncounter(SpawnRequest request);`
- `1e640daa1e8ce2ec` | `FACTORY` | `epidemic::gameplay::encounters::SpawnRequestId` | `static constexpr SpawnRequestId FromString(std::string_view s)noexcept`
- `2032b2fcef9ddf01` | `QUERY` | `epidemic::gameplay::encounters::EncounterDefinitionId` | `[[nodiscard]] constexpr auto operator<=>(const EncounterDefinitionId&)const noexcept=default;`
- `215a133a889f422b` | `QUERY` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`
- `216c11d8ef3e13f5` | `QUERY` | `epidemic::gameplay::encounters::RespawnRuleId` | `[[nodiscard]] constexpr bool operator==(const RespawnRuleId&)const noexcept=default;`
- `27baadf340e75b05` | `QUERY` | `epidemic::gameplay::encounters::SpawnEntryId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `287f698e14496d44` | `QUERY` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] std::vector<SpawnPoint> FindSpawnPointsInArea(GameplayObjectRef area)const;`
- `2a30f84f48f9dc28` | `LIFECYCLE` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] foundation::Result<void> ActivatePopulationBackedEncounter(EncounterInstanceId id,GameplayContext context={});`
- `35566dfd94672da4` | `QUERY` | `epidemic::gameplay::encounters::SpawnedEntityRecordId` | `[[nodiscard]] constexpr bool operator==(const SpawnedEntityRecordId&)const noexcept=default;`
- `37702751761eef8c` | `QUERY` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] EncounterChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `383d4bcbea4e10a3` | `FACTORY` | `epidemic::gameplay::encounters::EncounterDefinitionId` | `static constexpr EncounterDefinitionId FromString(std::string_view s)noexcept`
- `3af8ec8f3fe649fd` | `MUTATOR` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] foundation::Result<void> BindPopulationUnit(SpawnedEntityRecordId record,GameplayObjectRef unit,GameplayContext context={});`
- `3bd6d9e528ed3e1e` | `FACTORY` | `epidemic::gameplay::encounters::SpawnedEntityRecordId` | `static constexpr SpawnedEntityRecordId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `481142a788985b45` | `QUERY` | `epidemic::gameplay::encounters::EncounterDefinitionId` | `[[nodiscard]] constexpr bool operator==(const EncounterDefinitionId&)const noexcept=default;`
- `49a3cebea5f1c100` | `FACTORY` | `epidemic::gameplay::encounters::SpawnPointId` | `static constexpr SpawnPointId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `4b37cbbe1e6c0e3d` | `QUERY` | `epidemic::gameplay::encounters::EncounterInstanceId` | `[[nodiscard]] constexpr auto operator<=>(const EncounterInstanceId&)const noexcept=default;`
- `4d7c408b01b2ad0f` | `MUTATOR` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] foundation::Result<void> RegisterEncounterDefinition(EncounterDefinition definition);`
- `4eeb1e16047386b0` | `QUERY` | `epidemic::gameplay::encounters::SpawnRequestId` | `[[nodiscard]] constexpr auto operator<=>(const SpawnRequestId&)const noexcept=default;`
- `51663c72a0e5ab7b` | `MUTATOR` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] foundation::Result<void> RegisterSpawnTable(SpawnTable table);`
- `5ad504211ecdfea5` | `MUTATOR` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(EncountersSnapshot snapshot);`
- `612fd37f745cfa7f` | `QUERY` | `epidemic::gameplay::encounters::SpawnPointId` | `[[nodiscard]] constexpr bool operator==(const SpawnPointId&)const noexcept=default;`
- `6297c29cdcd83dd3` | `QUERY` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] EncountersSnapshot CaptureSnapshot()const;`
- `635a7a44bc945339` | `MUTATOR` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] foundation::Result<void> BindSpawnedEntity(SpawnedEntityRecordId record,GameplayObjectRef entity,GameplayContext context={});`
- `73b19b184635b7ba` | `QUERY` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] EncounterDiagnostics GetDiagnostics()const noexcept;`
- `7544c34c15929300` | `QUERY` | `epidemic::gameplay::encounters::SpawnPointId` | `[[nodiscard]] constexpr auto operator<=>(const SpawnPointId&)const noexcept=default;`
- `758ad364f29451d3` | `QUERY` | `epidemic::gameplay::encounters::RespawnRuleId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `7891793046598a2b` | `QUERY` | `epidemic::gameplay::encounters::SpawnTableId` | `[[nodiscard]] constexpr auto operator<=>(const SpawnTableId&)const noexcept=default;`
- `82ad580038c52830` | `QUERY` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] const SpawnedEntityRecord*GetSpawnedEntityRecord(SpawnedEntityRecordId id)const noexcept;`
- `87f9a2f6b5cc1a59` | `FACTORY` | `epidemic::gameplay::encounters::SpawnTableId` | `static constexpr SpawnTableId FromString(std::string_view s)noexcept`
- `8b2d67f127460ed0` | `MUTATOR` | `epidemic::gameplay::encounters::EncountersService` | `void SetBudgets(EncounterBudgets budgets)noexcept`
- `8bcb1061354b7927` | `MUTATOR` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] foundation::Result<void> FailEncounter(EncounterInstanceId id,GameplayContext context={});`
- `8c26e3734847c487` | `QUERY` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `91fc2dd769619ed4` | `MUTATOR` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] foundation::Result<void> CompleteEncounter(EncounterInstanceId id,GameplayContext context={});`
- `9490bc265a056287` | `QUERY` | `epidemic::gameplay::encounters::SpawnTableId` | `[[nodiscard]] constexpr bool operator==(const SpawnTableId&)const noexcept=default;`
- `9a152fa3bd815e0c` | `QUERY` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] const EncounterInstance*GetEncounterInstance(EncounterInstanceId id)const noexcept;`
- `9d5a710cf6a44c07` | `QUERY` | `epidemic::gameplay::encounters::EncounterInstanceId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `9d81aa3eb143b86b` | `FACTORY` | `epidemic::gameplay::encounters::SpawnRequestId` | `static constexpr SpawnRequestId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `9f35210bfac8cf26` | `QUERY` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] bool DefinitionsFrozen()const noexcept`
- `a2941d0db08a5f0a` | `LIFECYCLE` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] foundation::Result<void> FreezeDefinitions();`
- `a7a2b5ec76ecc450` | `QUERY` | `epidemic::gameplay::encounters::SpawnEntryId` | `[[nodiscard]] constexpr auto operator<=>(const SpawnEntryId&)const noexcept=default;`
- `aef35774c14bafdc` | `QUERY` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] foundation::Result<std::vector<TypeId>> PreviewSpawnArchetypes(SpawnRequest request)const;`
- `b87c2cbc301018e6` | `MUTATOR` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] foundation::Result<RespawnRuleId> ScheduleRespawn(RespawnRule rule,GameplayContext context={});`
- `b97eb3b37401fb05` | `FACTORY` | `epidemic::gameplay::encounters::SpawnedEntityRecordId` | `static constexpr SpawnedEntityRecordId FromString(std::string_view s)noexcept`
- `b9b2980bd9b9976f` | `FACTORY` | `epidemic::gameplay::encounters::SpawnPointId` | `static constexpr SpawnPointId FromString(std::string_view s)noexcept`
- `baba9a0ef2a4dd90` | `QUERY` | `epidemic::gameplay::encounters::EncounterInstanceId` | `[[nodiscard]] constexpr bool operator==(const EncounterInstanceId&)const noexcept=default;`
- `bd8332a5bc9a55d7` | `QUERY` | `epidemic::gameplay::encounters::SpawnedEntityRecordId` | `[[nodiscard]] constexpr auto operator<=>(const SpawnedEntityRecordId&)const noexcept=default;`
- `bf2ab30e4def7263` | `MUTATOR` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] std::vector<SpawnResult> ProcessDueRespawns(GameplayTimePoint now,std::size_t max_rules,GameplayContext context={});`
- `bf72f4b507b8e15a` | `QUERY` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] std::vector<EncounterInstance> FindActiveEncountersInArea(GameplayObjectRef area)const;`
- `ca15530287fff492` | `QUERY` | `epidemic::gameplay::encounters::SpawnEntryId` | `[[nodiscard]] constexpr bool operator==(const SpawnEntryId&)const noexcept=default;`
- `d2055876351a7895` | `QUERY` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] Revision CurrentRevision()const noexcept`
- `d3128137e4675cf8` | `FACTORY` | `epidemic::gameplay::encounters::SpawnEntryId` | `static constexpr SpawnEntryId FromString(std::string_view s)noexcept`
- `d328f7f81c3079f3` | `QUERY` | `epidemic::gameplay::encounters::SpawnPointId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `d4b718c7cd7c856e` | `QUERY` | `epidemic::gameplay::encounters::RespawnRuleId` | `[[nodiscard]] constexpr auto operator<=>(const RespawnRuleId&)const noexcept=default;`
- `d729397d524d8be9` | `MUTATOR` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] foundation::Result<void> BeginDespawningEncounter(EncounterInstanceId id,GameplayContext context={});`
- `d789b053e733b10f` | `QUERY` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] bool CanSpawnEncounter(SpawnRequest request)const;`
- `da609052672766c6` | `QUERY` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] std::vector<EncounterDefinition> FindDefinitionsByTag(TagId tag)const;`
- `e1f8c5f65ff701bb` | `QUERY` | `epidemic::gameplay::encounters::SpawnedEntityRecordId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `e3e88fa6537d5402` | `FACTORY` | `epidemic::gameplay::encounters::RespawnRuleId` | `static constexpr RespawnRuleId FromString(std::string_view s)noexcept`
- `e767148115514a59` | `FACTORY` | `epidemic::gameplay::encounters::EncounterInstanceId` | `static constexpr EncounterInstanceId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `edc431dfeea74bc6` | `MUTATOR` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] foundation::Result<SpawnPointId> AddSpawnPoint(SpawnPoint point);`
- `ee3d726a8a393055` | `FACTORY` | `epidemic::gameplay::encounters::RespawnRuleId` | `static constexpr RespawnRuleId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `f4bfe0fdf2445d31` | `QUERY` | `epidemic::gameplay::encounters::EncounterDefinitionId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `fae89139108f659e` | `MUTATOR` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] std::size_t PruneTerminalEncounters(std::size_t max_to_prune);`
- `fb07f369a9c61e10` | `QUERY` | `epidemic::gameplay::encounters::EncountersService` | `[[nodiscard]] std::vector<EncounterInstance> FindEncountersByState(EncounterState state)const;`
