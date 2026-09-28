# Population Goal 4 local audit

## Scope

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Population`. Admission: 75 public callables, 80 mutation obligations, 2 lifecycle candidates, 19 stale-identity candidates, 0 external-boundary candidates.

## Public contract review

- Template registry/freeze and group/unit creation.
- Virtual/materialized/dead/removed unit lifecycle and entity binding.
- Residence and area indexes remain consistent with primary records.
- Migration start/finish/cancel/fail is multi-container atomic.
- Allocation reserve/commit/release and terminal pruning.
- Snapshot/restore preserves generators, revisions, indexes and journal epoch/cursor.

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

Primary regression source: `EngineFramework/DevelopmentInfrastructure/Tests/population_tests.cpp`.
Registered target: `EpidemicGameFrameworkPopulationTests`.
Local Linux verification in the worker environment used direct C++23 compilation with `-Wall -Wextra -Wpedantic -Werror` and executed the module test binary. The repository top-level CMake configure is Windows-only and therefore cannot be used in this Linux worker environment.

## Goal 4 result

Block-local review result: LOCAL_READY candidate for serial integration. This document does not claim whole-Framework `LOCAL_READY`, `SYSTEM_READY` or `FROZEN`.

## Exact public API anchors

Each row is the exact reviewed contract for one B04 callable. The matching assertion is recorded in `_goal4_handoff/B04/public_api_anchors.json`.

- `0275f1c8b9365621` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] const PopulationTemplate*GetTemplate(PopulationTemplateId id)const noexcept;`
- `07cbd604e77ec26d` | `QUERY` | `epidemic::gameplay::population::PopulationProfileId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `084720e20976ed09` | `QUERY` | `epidemic::gameplay::population::PopulationProfileId` | `[[nodiscard]] constexpr bool operator==(const PopulationProfileId&)const noexcept=default;`
- `09ec28925e5a1bd6` | `FACTORY` | `epidemic::gameplay::population::PopulationUnitId` | `static constexpr PopulationUnitId FromString(std::string_view s)noexcept`
- `116c5fc2752db1c1` | `MUTATOR` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] foundation::Result<void> FailMigration(PopulationMigrationId migration,GameplayContext context={});`
- `1a5513bbe1eb6ca0` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] const PopulationAllocation*GetActiveAllocationForUnit(PopulationUnitId unit)const noexcept;`
- `1e3879a6f5101c94` | `FACTORY` | `epidemic::gameplay::population::PopulationProfileId` | `static constexpr PopulationProfileId FromString(std::string_view s)noexcept`
- `20934b65ac3a90c1` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `2378af556cc91bac` | `MUTATOR` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] foundation::Result<void> CancelMigration(PopulationMigrationId migration,GameplayContext context={});`
- `27e6e805deb74749` | `QUERY` | `epidemic::gameplay::population::PopulationResidenceId` | `[[nodiscard]] constexpr bool operator==(const PopulationResidenceId&)const noexcept=default;`
- `2b7915b5e8f02bc6` | `MUTATOR` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(PopulationSnapshot snapshot);`
- `2ce693947a208e53` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] std::vector<PopulationUnit> FindUnitsInArea(GameplayObjectRef area)const;`
- `2d7c33d0fb2a9b15` | `FACTORY` | `epidemic::gameplay::population::PopulationResidenceId` | `static constexpr PopulationResidenceId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `3b3bf52ba74bbe0a` | `FACTORY` | `epidemic::gameplay::population::PopulationMigrationId` | `static constexpr PopulationMigrationId FromString(std::string_view s)noexcept`
- `3cf35db3759adc4f` | `MUTATOR` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] foundation::Result<void> MaterializeUnit(PopulationUnitId unit,GameplayObjectRef entity,GameplayContext context={});`
- `463c548633ee9f15` | `FACTORY` | `epidemic::gameplay::population::PopulationAllocationId` | `static constexpr PopulationAllocationId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `49bf2432c2e476f6` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] const PopulationUnit*GetUnit(PopulationUnitId id)const noexcept;`
- `4a9a9debe8cca6a7` | `QUERY` | `epidemic::gameplay::population::PopulationResidenceId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `4ae885dba1ff2a4d` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] PopulationSnapshot CaptureSnapshot()const;`
- `4b0fd64bbee11d7c` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] PopulationCounts GetPopulationCounts(PopulationGroupId group={})const;`
- `4b2d11cc135af0b0` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] std::vector<PopulationAllocation> FindAllocationsByCorrelation(GameplayObjectId correlation)const;`
- `4eb6bac017e8faa9` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] PopulationDiagnostics GetDiagnostics()const noexcept;`
- `4ed670457dd91698` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] const PopulationUnit*FindUnitByEntity(GameplayObjectRef entity)const noexcept;`
- `503a8e5bcf6becfd` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] std::vector<PopulationUnit> FindResidentsOfArea(GameplayObjectRef area)const;`
- `58bdd3e6afe82363` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] std::vector<PopulationUnit> FindUnitsByGroup(PopulationGroupId group)const;`
- `62bbcb930a68fbf5` | `MUTATOR` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] foundation::Result<void> CommitAllocation(PopulationAllocationId allocation,GameplayObjectRef entity,GameplayContext context={});`
- `6387e257b7121bcf` | `FACTORY` | `epidemic::gameplay::population::PopulationUnitId` | `static constexpr PopulationUnitId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `67dc27b10e455d13` | `QUERY` | `epidemic::gameplay::population::PopulationTemplateId` | `[[nodiscard]] constexpr auto operator<=>(const PopulationTemplateId&)const noexcept=default;`
- `685ac5d5132c8aac` | `LIFECYCLE` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] foundation::Result<void> FreezeDefinitions();`
- `6903aa75a43fcc84` | `MUTATOR` | `epidemic::gameplay::population::PopulationService` | `void PruneChangesThrough(std::uint64_t sequence);`
- `69b1e05587d91cf2` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] static constexpr GameplayObjectRef ResidentRef(PopulationUnitId unit)noexcept`
- `6a4f3fa4fdb09fcd` | `QUERY` | `epidemic::gameplay::population::PopulationGroupId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `6a85f665c16d7ecc` | `QUERY` | `epidemic::gameplay::population::PopulationResidenceId` | `[[nodiscard]] constexpr auto operator<=>(const PopulationResidenceId&)const noexcept=default;`
- `6f5d6eeb6c83073d` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] Revision CurrentRevision()const noexcept`
- `7163bd6435d2aef9` | `QUERY` | `epidemic::gameplay::population::PopulationMigrationId` | `[[nodiscard]] constexpr bool operator==(const PopulationMigrationId&)const noexcept=default;`
- `775592a927a1dbfb` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] bool DefinitionsFrozen()const noexcept`
- `78f14605fa72a38f` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] const PopulationAllocation*GetAllocation(PopulationAllocationId id)const noexcept;`
- `7def2fc3fa976a3b` | `FACTORY` | `epidemic::gameplay::population::PopulationGroupId` | `static constexpr PopulationGroupId FromString(std::string_view s)noexcept`
- `7ff1d5d4351f83f9` | `QUERY` | `epidemic::gameplay::population::PopulationAllocationId` | `[[nodiscard]] constexpr bool operator==(const PopulationAllocationId&)const noexcept=default;`
- `80118e30dd1e88a1` | `MUTATOR` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] foundation::Result<void> ReleaseAllocation(PopulationAllocationId allocation,GameplayContext context={});`
- `84513ee7ae0f4190` | `LIFECYCLE` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] foundation::Result<PopulationMigrationId> StartMigration(PopulationMigration migration,GameplayContext context={});`
- `88414cc1e863c557` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] PopulationChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `8b14d03ad967c5dc` | `MUTATOR` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] foundation::Result<PopulationUnitId> CreateUnit(PopulationUnit unit,GameplayContext context={});`
- `8b7d9aaba493f3f0` | `FACTORY` | `epidemic::gameplay::population::PopulationMigrationId` | `static constexpr PopulationMigrationId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `93013b82eee99572` | `MUTATOR` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] foundation::Result<void> DematerializeUnit(PopulationUnitId unit,GameplayContext context={});`
- `94b62bddcddd6745` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] std::vector<PopulationUnit> FindMaterializationCandidates(GameplayObjectRef area,std::size_t limit)const;`
- `9908223b6bd0c378` | `QUERY` | `epidemic::gameplay::population::PopulationTemplateId` | `[[nodiscard]] constexpr bool operator==(const PopulationTemplateId&)const noexcept=default;`
- `9a33dc7ade59f6bb` | `FACTORY` | `epidemic::gameplay::population::PopulationTemplateId` | `static constexpr PopulationTemplateId FromString(std::string_view s)noexcept`
- `9b5fdb3ada09bbda` | `MUTATOR` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] foundation::Result<void> SetUnitEntity(PopulationUnitId unit,GameplayObjectRef entity,GameplayContext context={});`
- `9c8a0417e1d0715b` | `MUTATOR` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] foundation::Result<PopulationAllocationBatch> ReserveAllocations(PopulationAllocationRequest request,GameplayContext context={});`
- `a030b65206363a58` | `QUERY` | `epidemic::gameplay::population::PopulationMigrationId` | `[[nodiscard]] constexpr auto operator<=>(const PopulationMigrationId&)const noexcept=default;`
- `b0b1a55f9fe6b5b8` | `QUERY` | `epidemic::gameplay::population::PopulationAllocationId` | `[[nodiscard]] constexpr auto operator<=>(const PopulationAllocationId&)const noexcept=default;`
- `b4184c6aa61997b6` | `MUTATOR` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] foundation::Result<PopulationResidenceId> AssignResidence(PopulationResidence residence,GameplayContext context={});`
- `b4933e0cbedc7771` | `FACTORY` | `epidemic::gameplay::population::PopulationGroupId` | `static constexpr PopulationGroupId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `b6dfefa430236273` | `MUTATOR` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] foundation::Result<void> MarkUnitDead(PopulationUnitId unit,GameplayContext context={});`
- `bdf18e9444ec84fc` | `MUTATOR` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] foundation::Result<void> RetireUnit(PopulationUnitId unit,GameplayContext context={});`
- `bffb13009f678003` | `FACTORY` | `epidemic::gameplay::population::PopulationResidenceId` | `static constexpr PopulationResidenceId FromString(std::string_view s)noexcept`
- `c187ccd62a3672d2` | `MUTATOR` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] foundation::Result<PopulationGroupId> CreateGroup(PopulationGroup group);`
- `c1db80bfa3c6aa17` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] const PopulationGroup*GetGroup(PopulationGroupId id)const noexcept;`
- `c2fc52d4710bbf21` | `MUTATOR` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] foundation::Result<void> RegisterTemplate(PopulationTemplate definition);`
- `c42cfe8b46c00efb` | `MUTATOR` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] foundation::Result<void> CompleteMigration(PopulationMigrationId migration,GameplayContext context={});`
- `c7757893c4cc5a06` | `QUERY` | `epidemic::gameplay::population::PopulationAllocationId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `c8668167af5713aa` | `QUERY` | `epidemic::gameplay::population::PopulationGroupId` | `[[nodiscard]] constexpr bool operator==(const PopulationGroupId&)const noexcept=default;`
- `c8a604370c4d3585` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] std::vector<PopulationGroup> FindGroupsInArea(GameplayObjectRef area)const;`
- `c9a4b200af28e5f8` | `QUERY` | `epidemic::gameplay::population::PopulationGroupId` | `[[nodiscard]] constexpr auto operator<=>(const PopulationGroupId&)const noexcept=default;`
- `cb9f28a009d6bce2` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`
- `d0f5e91aed4beebd` | `QUERY` | `epidemic::gameplay::population::PopulationService` | `[[nodiscard]] std::vector<PopulationUnit> FindUnitsByState(PopulationUnitState state)const;`
- `d4579fb88789ab50` | `MUTATOR` | `epidemic::gameplay::population::PopulationService` | `void PruneTerminalAllocations(GameplayObjectId correlation);`
- `d652bb3c84f4312d` | `QUERY` | `epidemic::gameplay::population::PopulationUnitId` | `[[nodiscard]] constexpr auto operator<=>(const PopulationUnitId&)const noexcept=default;`
- `dd0d466b9d8ab6e0` | `FACTORY` | `epidemic::gameplay::population::PopulationAllocationId` | `static constexpr PopulationAllocationId FromString(std::string_view s)noexcept`
- `e24d2bc90b632240` | `QUERY` | `epidemic::gameplay::population::PopulationTemplateId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `e9c1d00460d29739` | `QUERY` | `epidemic::gameplay::population::PopulationProfileId` | `[[nodiscard]] constexpr auto operator<=>(const PopulationProfileId&)const noexcept=default;`
- `f73d70f153d8bc41` | `QUERY` | `epidemic::gameplay::population::PopulationUnitId` | `[[nodiscard]] constexpr bool operator==(const PopulationUnitId&)const noexcept=default;`
- `f7fdb8b4c8b97805` | `QUERY` | `epidemic::gameplay::population::PopulationUnitId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `f9b38c15261ac274` | `QUERY` | `epidemic::gameplay::population::PopulationMigrationId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
