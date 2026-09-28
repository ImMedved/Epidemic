# NeedsLife Goal 4 local audit

## Scope

Freeze unit: `EngineFramework/GameplayWorldStateOwners/NeedsLife`. Admission: 65 public callables, 72 mutation obligations, 1 lifecycle candidates, 17 stale-identity candidates, 0 external-boundary candidates.

## Public contract review

- Need definitions, profiles and committed need state.
- Satisfy/decay numeric boundaries and saturating arithmetic.
- Life pressure create/resolve/expire/prune lifecycle.
- Large simulation intervals and terminal pressure pruning.
- Revision/generator/journal exhaustion and no-false-publish semantics.
- Snapshot/restore validates first and preserves live state on failure.

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

Primary regression source: `EngineFramework/DevelopmentInfrastructure/Tests/needs_life_tests.cpp`.
Registered target: `EpidemicGameFrameworkNeedsLifeTests`.
Local Linux verification in the worker environment used direct C++23 compilation with `-Wall -Wextra -Wpedantic -Werror` and executed the module test binary. The repository top-level CMake configure is Windows-only and therefore cannot be used in this Linux worker environment.

## Goal 4 result

Block-local review result: LOCAL_READY candidate for serial integration. This document does not claim whole-Framework `LOCAL_READY`, `SYSTEM_READY` or `FROZEN`.

## Exact public API anchors

Each row is the exact reviewed contract for one B04 callable. The matching assertion is recorded in `_goal4_handoff/B04/public_api_anchors.json`.

- `01bcdb9e8e863023` | `QUERY` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`
- `03c36824950e3580` | `MUTATOR` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] foundation::Result<std::size_t> SweepExpiredPressures(GameplayTimePoint now,GameplayContext context={});`
- `0781b6c796831b85` | `MUTATOR` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] foundation::Result<void> RegisterNeedDefinition(NeedDefinition definition);`
- `087c62ecc874062b` | `QUERY` | `epidemic::gameplay::needs_life::NeedDecayRuleId` | `[[nodiscard]] constexpr bool operator==(const NeedDecayRuleId&)const noexcept=default;`
- `0bac36b152393695` | `QUERY` | `epidemic::gameplay::needs_life::NeedProfileId` | `[[nodiscard]] constexpr bool operator==(const NeedProfileId&)const noexcept=default;`
- `15a9c7e43f3a5020` | `MUTATOR` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] foundation::Result<void> RegisterSimulationProfile(LifeSimulationProfile profile);`
- `16167c89d6d310f8` | `QUERY` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] NeedsLifeChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `1f537d1fcd6719f6` | `QUERY` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] bool DefinitionsFrozen()const noexcept`
- `25963bf5b02639e6` | `QUERY` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] std::vector<LifeRoutineOccurrence> FindRoutineEntriesInInterval(GameplayObjectRef subject,GameplayTimePoint from,GameplayTimePoint to)const;`
- `3078c8ee581c278e` | `MUTATOR` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] foundation::Result<void> RemoveNeedProfile(GameplayObjectRef subject,GameplayContext context={});`
- `332b8f3d3ced4e5c` | `MUTATOR` | `epidemic::gameplay::needs_life::NeedsLifeService` | `void SetChangeJournalCapacity(std::size_t capacity)noexcept;`
- `3873609972ab59f8` | `QUERY` | `epidemic::gameplay::needs_life::NeedProfileId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `3af61860ad923adb` | `MUTATOR` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] foundation::Result<void> RegisterDecayRule(NeedDecayRule rule);`
- `4f082048e0b5a0d1` | `QUERY` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] Revision CurrentRevision()const noexcept`
- `51ccea6902c9e9a3` | `MUTATOR` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] foundation::Result<LifePressureId> CreateLifePressure(LifePressure pressure,GameplayContext context={});`
- `522b3a8ead52ec3b` | `QUERY` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] NeedsLifeSnapshot CaptureSnapshot()const;`
- `547a31f0f58f8f01` | `QUERY` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] const LifeRoutine*GetRoutine(GameplayObjectRef subject)const noexcept;`
- `578c4022ee2877be` | `QUERY` | `epidemic::gameplay::needs_life::LifePressureId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `584258e79cf6b29e` | `FACTORY` | `epidemic::gameplay::needs_life::LifeSimulationProfileId` | `static constexpr LifeSimulationProfileId FromString(std::string_view s)noexcept`
- `5e365349deb485cb` | `QUERY` | `epidemic::gameplay::needs_life::LifeRoutineOccurrence` | `[[nodiscard]] constexpr bool operator==(const LifeRoutineOccurrence&other)const noexcept`
- `6282509a9e496fec` | `MUTATOR` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] foundation::Result<void> CommitNeedEvaluation(GameplayObjectRef subject,NeedTypeId need,GameplayTimePoint now,GameplayContext context={});`
- `6b8e1352c6e905fc` | `MUTATOR` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] std::size_t PruneTerminalPressures(GameplayObjectRef subject={})noexcept;`
- `74149b83fe08be5d` | `QUERY` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] const NeedProfile*GetNeedProfile(GameplayObjectRef subject)const noexcept;`
- `76a903466fd8b4a3` | `QUERY` | `epidemic::gameplay::needs_life::LifeRoutineId` | `[[nodiscard]] constexpr bool operator==(const LifeRoutineId&)const noexcept=default;`
- `7774b51d24d2aa63` | `QUERY` | `epidemic::gameplay::needs_life::LifeSimulationProfileId` | `[[nodiscard]] constexpr bool operator==(const LifeSimulationProfileId&)const noexcept=default;`
- `7e597262618ec7fe` | `FACTORY` | `epidemic::gameplay::needs_life::NeedTypeId` | `static constexpr NeedTypeId FromString(std::string_view s)noexcept`
- `7e70382f5d849367` | `QUERY` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] foundation::Result<void> ResolveLifePressure(LifePressureId id,GameplayContext context={});`
- `7f65ed1f0739ef11` | `FACTORY` | `epidemic::gameplay::needs_life::LifeRoutineId` | `static constexpr LifeRoutineId FromString(std::string_view s)noexcept`
- `8498274380d8de1c` | `MUTATOR` | `epidemic::gameplay::needs_life::NeedsLifeService` | `void PruneChangesThrough(std::uint64_t sequence)noexcept;`
- `892a35d90ae425c1` | `QUERY` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] std::vector<NeedState> FindCriticalNeeds(GameplayTimePoint now)const;`
- `89b50f74478581ad` | `QUERY` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] NeedState EvaluateNeed(GameplayObjectRef subject,NeedTypeId need,GameplayTimePoint now)const;`
- `8cd87c58936952a0` | `MUTATOR` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] foundation::Result<void> SatisfyNeed(SatisfyNeedRequest request);`
- `91a2f95353b2a3e0` | `QUERY` | `epidemic::gameplay::needs_life::LifePressureId` | `[[nodiscard]] constexpr auto operator<=>(const LifePressureId&)const noexcept=default;`
- `940ff6f8470c850d` | `QUERY` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] std::int64_t GetNeedUrgency(GameplayObjectRef subject,NeedTypeId need,GameplayTimePoint now)const;`
- `95bd023fceb678c0` | `MUTATOR` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] foundation::Result<NeedProfileId> CreateNeedProfile(NeedProfile profile,GameplayContext context={});`
- `9b492e8a22b9d51d` | `FACTORY` | `epidemic::gameplay::needs_life::NeedDecayRuleId` | `static constexpr NeedDecayRuleId FromString(std::string_view s)noexcept`
- `9e5b98d3b4762313` | `QUERY` | `epidemic::gameplay::needs_life::LifeSimulationProfileId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `9ed2ff383bd29837` | `MUTATOR` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(NeedsLifeSnapshot snapshot);`
- `9f3b3caf58f83da5` | `QUERY` | `epidemic::gameplay::needs_life::LifeRoutineId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `a1e4055651d33e14` | `QUERY` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] std::vector<NeedState> FindCriticalNeeds(GameplayObjectRef subject,GameplayTimePoint now)const;`
- `a88412fd14a20b3d` | `MUTATOR` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] foundation::Result<void> AddNeedPressure(AddNeedPressureRequest request);`
- `acf4703a4787b176` | `QUERY` | `epidemic::gameplay::needs_life::NeedTypeId` | `[[nodiscard]] constexpr auto operator<=>(const NeedTypeId&)const noexcept=default;`
- `afb562de60c9d144` | `QUERY` | `epidemic::gameplay::needs_life::NeedTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `aff4531ea9bdcb55` | `QUERY` | `epidemic::gameplay::needs_life::LifeRoutineId` | `[[nodiscard]] constexpr auto operator<=>(const LifeRoutineId&)const noexcept=default;`
- `b082f37e06641212` | `MUTATOR` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] foundation::Result<void> RemoveRoutine(GameplayObjectRef subject,GameplayContext context={});`
- `b1491c87b40ecf5e` | `MUTATOR` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] foundation::Result<LifeRoutineId> SetRoutine(LifeRoutine routine,GameplayContext context={});`
- `b52b80e44dad825e` | `FACTORY` | `epidemic::gameplay::needs_life::LifePressureId` | `static constexpr LifePressureId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `bb98675ef65d8319` | `QUERY` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] const NeedState*GetCommittedNeedState(GameplayObjectRef subject,NeedTypeId need)const noexcept;`
- `bca4c8837ae1a372` | `QUERY` | `epidemic::gameplay::needs_life::LifeSimulationProfileId` | `[[nodiscard]] constexpr auto operator<=>(const LifeSimulationProfileId&)const noexcept=default;`
- `c0df9a0852b2c46a` | `QUERY` | `epidemic::gameplay::needs_life::NeedStateKeyHash` | `[[nodiscard]] std::size_t operator()(const NeedStateKey&key)const noexcept`
- `c0eea5a73c78f1b4` | `QUERY` | `epidemic::gameplay::needs_life::NeedTypeId` | `[[nodiscard]] constexpr bool operator==(const NeedTypeId&)const noexcept=default;`
- `c227ef9545e72e57` | `QUERY` | `epidemic::gameplay::needs_life::NeedStateKey` | `[[nodiscard]] constexpr bool operator==(const NeedStateKey&)const noexcept=default;`
- `c29435697c5d0264` | `QUERY` | `epidemic::gameplay::needs_life::LifePressureId` | `[[nodiscard]] constexpr bool operator==(const LifePressureId&)const noexcept=default;`
- `cdd2f53f5e5d6e77` | `QUERY` | `epidemic::gameplay::needs_life::NeedDecayRuleId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `d176558c07c54cfa` | `FACTORY` | `epidemic::gameplay::needs_life::NeedProfileId` | `static constexpr NeedProfileId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `de997c0bf3516b7d` | `QUERY` | `epidemic::gameplay::needs_life::NeedProfileId` | `[[nodiscard]] constexpr auto operator<=>(const NeedProfileId&)const noexcept=default;`
- `e13e11adc1e314be` | `LIFECYCLE` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] foundation::Result<void> FreezeDefinitions();`
- `eb7b4fd62ba717a3` | `FACTORY` | `epidemic::gameplay::needs_life::LifeRoutineId` | `static constexpr LifeRoutineId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `ee5a36d8c86716aa` | `QUERY` | `epidemic::gameplay::needs_life::NeedDecayRuleId` | `[[nodiscard]] constexpr auto operator<=>(const NeedDecayRuleId&)const noexcept=default;`
- `f002259f4c78f43c` | `QUERY` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] NeedsLifeDiagnostics GetDiagnostics()const noexcept;`
- `f10ef48bf908ed98` | `QUERY` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] std::vector<LifePressure> FindLifePressures(GameplayObjectRef subject)const;`
- `f502ab066537fe3f` | `MUTATOR` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] foundation::Result<void> SimulateLifeInterval(LifeSimulationRequest request);`
- `f5ee932fd153e844` | `FACTORY` | `epidemic::gameplay::needs_life::NeedProfileId` | `static constexpr NeedProfileId FromString(std::string_view s)noexcept`
- `fcc2aa9d526b1792` | `QUERY` | `epidemic::gameplay::needs_life::NeedsLifeService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `ff581aa57e1d7089` | `FACTORY` | `epidemic::gameplay::needs_life::LifePressureId` | `static constexpr LifePressureId FromString(std::string_view s)noexcept`
