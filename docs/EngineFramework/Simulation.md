# Simulation Goal 4 B05 local audit

Status: block-local `LOCAL_READY` projection for Goal 4. This is not a whole-Framework `FROZEN` claim.

## Responsibility and ownership

Owns Framework simulation regions, layer definitions/executor bindings, resumable interval/task state, completed summaries, task/summary generators, revisions and the bounded simulation journal.

The service remains the single authoritative owner of that state. Derived indexes, journals and diagnostic/read models are not independent semantic owners.

## Authoritative state and indexes

Admission/generated B05 inventory: 45 public callables, 40 mutation obligations, 2 lifecycle candidates, 9 stale-identity candidates and 0 external-boundary candidates.

Primary records, secondary indexes, ID generators, revision/change sequence and bounded journal state are reviewed together. Multi-container mutations must complete fallible staging before authoritative publication. Deterministic query/order semantics must agree with primary state.

## Public contracts and failure atomicity

Every admission callable below is classified and reviewed. Mutators have success, no-op, invalid/precondition and failure decisions in the B05 coverage projection. Revision, identity-generator and journal publication is part of the mutation contract. Allocation/publication failure may not expose partially advanced primary state, index state, generator, revision or journal.

B05 allocation evidence uses source-private module seams rather than the shared process-global allocator override. The seams are implementation/test details and do not expand the public API.

## Lifecycle and identity

All emitted lifecycle and stale-identity candidates were reviewed. Invalid, removed, duplicate and stale identities are rejected or handled as the documented no-op without false publication. Retry does not silently resurrect terminal state.

## Persistence

Snapshot/restore is reviewed as candidate-state publication: validate first, build off-state, preserve the current service on failure, then atomically publish the candidate. Successful restore preserves generator/revision/cursor continuity required by the module snapshot contract.

## External boundaries

`ISimulationLayerExecutor::Prepare/Commit` is the critical accepted-work boundary. Stable task IDs and idempotent commit remain required. Before an external `Commit`, the service now proves capacity for the task terminal record. If the task is the last unfinished layer, it additionally stages the final interval revision, summary node and `SummaryGenerated` journal record. After accepted work only no-throw local state/journal publication remains.

## Threading contract

No additional internal synchronization guarantee is introduced by B05. These gameplay owners are treated as externally serialized/owner-thread services for Goal 4 local correctness. Engine-wide concurrency qualification remains Goal 6.

## Regression evidence

Registered module target: `EpidemicGameFrameworkSimulationTests` from `EngineFramework/DevelopmentInfrastructure/Tests/simulation_tests.cpp`.

`G4-SIM-001` is covered by restored pending-interval regressions for revision `MAX`, revision `MAX-1` on the last unfinished layer, final journal-sequence exhaustion and terminal `SummaryPublication` allocation failure. Each proves external `Commit` is not called unless both task and interval terminal publication can complete, and the allocation case also proves retry succeeds. `G4-SIM-002` is covered by module-local `IntervalPublication`, `SummaryPublication` and `JournalPublication` fault points with complete snapshot equality on failure and retry after summary-publication failure.

## Public-header surface review

Goal 4 changes only private storage types inside `SimulationService`: completed summaries and the change journal use `std::list` so staged nodes can be published with no-throw `splice` after accepted external work. No public callable was added, removed or reclassified; the B05 callable inventory remains 45 for Simulation and 316 for the block. The header hash/private layout change is explicitly recorded for serial public-surface regeneration. Stable C++ binary ABI is not part of the freeze promise.

## Goal 4 functional checklist

- [x] Framework simulation definitions/state.
- [x] Job/task registration and lifecycle.
- [x] Budgeted update.
- [x] Proposal/application boundary to state owners.
- [x] No duplicate authoritative state with Runtime Simulation.
- [x] Large delta/catch-up.
- [x] Failure atomicity.
- [x] Snapshot/restore.

## Exact public API anchors

Each row is the exact reviewed contract for one B05 callable. The matching assertion is recorded in `_goal4_handoff/B05/public_api_anchors.json`.

- `072013adb1839b82` | `QUERY` | `epidemic::gameplay::simulation::SimulationService` | `[[nodiscard]] bool DefinitionsFrozen()const noexcept`
- `0a4127d5ca1b56ca` | `QUERY` | `epidemic::gameplay::simulation::SimulationPolicyId` | `[[nodiscard]] constexpr auto operator<=>(const SimulationPolicyId&)const noexcept=default;`
- `0b1015ff577ad4aa` | `QUERY` | `epidemic::gameplay::simulation::SimulationRegionId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `0c25d7f79cf3afda` | `MUTATOR` | `epidemic::gameplay::simulation::SimulationService` | `void SetBudget(SimulationBudget budget)noexcept`
- `12547365a2ceb834` | `MUTATOR` | `epidemic::gameplay::simulation::SimulationService` | `void SetRetentionPolicy(SimulationRetentionPolicy policy)noexcept;`
- `1c1d25409628059c` | `MUTATOR` | `epidemic::gameplay::simulation::ISimulationLayerExecutor` | `[[nodiscard]] virtual foundation::Result<SimulationLayerSummary> Prepare(const SimulationTask&task)=0;`
- `2518f2abebaaf5c6` | `QUERY` | `epidemic::gameplay::simulation::SimulationPolicyId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `2b4314510ad596ff` | `LIFECYCLE` | `epidemic::gameplay::simulation::SimulationService` | `void Freeze()noexcept`
- `2efa3095b5da1a11` | `MUTATOR` | `epidemic::gameplay::simulation::SimulationService` | `[[nodiscard]] foundation::Result<SimulationLayerId> RegisterLayer(SimulationLayerDefinition layer,ISimulationLayerExecutor*executor);`
- `344232bacf5a4334` | `QUERY` | `epidemic::gameplay::simulation::SimulationService` | `[[nodiscard]] const SimulationIntervalExecution*FindActiveInterval(SimulationRegionId region)const noexcept;`
- `3c3b6bec43adba36` | `QUERY` | `epidemic::gameplay::simulation::SimulationService` | `[[nodiscard]] SimulationDiagnostics GetDiagnostics()const noexcept;`
- `3eb08f59034bdb46` | `QUERY` | `epidemic::gameplay::simulation::SimulationSummaryId` | `[[nodiscard]] constexpr auto operator<=>(const SimulationSummaryId&)const noexcept=default;`
- `418237a8ed6a9841` | `QUERY` | `epidemic::gameplay::simulation::SimulationService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`
- `4bce61c9915f5089` | `FACTORY` | `epidemic::gameplay::simulation::SimulationSummaryId` | `static constexpr SimulationSummaryId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `4cda1fda7a39066f` | `QUERY` | `epidemic::gameplay::simulation::SimulationService` | `[[nodiscard]] SimulationSnapshot CaptureSnapshot()const;`
- `4db6705d8d1e64d9` | `MUTATOR` | `epidemic::gameplay::simulation::SimulationService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(SimulationSnapshot snapshot);`
- `4f8bcfa474b80fe1` | `QUERY` | `epidemic::gameplay::simulation::SimulationSummaryId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `5d75bcf08facc2cd` | `QUERY` | `epidemic::gameplay::simulation::SimulationSummaryId` | `[[nodiscard]] constexpr bool operator==(const SimulationSummaryId&)const noexcept=default;`
- `68dc8156413962bd` | `QUERY` | `epidemic::gameplay::simulation::SimulationService` | `[[nodiscard]] SimulationChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `6981e5f697c9ad41` | `QUERY` | `epidemic::gameplay::simulation::SimulationService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `69c1ee08d01d8037` | `QUERY` | `epidemic::gameplay::simulation::SimulationPolicyId` | `[[nodiscard]] constexpr bool operator==(const SimulationPolicyId&)const noexcept=default;`
- `6dda7dded217f3c2` | `MUTATOR` | `epidemic::gameplay::simulation::SimulationService` | `[[nodiscard]] foundation::Result<SimulationRegionId> RegisterRegion(SimulationRegion region);`
- `6eccd989bd3fc328` | `QUERY` | `epidemic::gameplay::simulation::SimulationPreparedOperation` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `72fa8e5829edb3fe` | `QUERY` | `epidemic::gameplay::simulation::ISimulationLayerExecutor` | `[[nodiscard]] virtual SimulationLayerId Layer()const noexcept=0;`
- `7e226e7fac25dc01` | `FACTORY` | `epidemic::gameplay::simulation::SimulationTaskId` | `static constexpr SimulationTaskId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `8e08d57964f8cdce` | `QUERY` | `epidemic::gameplay::simulation::SimulationLayerId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `9c1683ba644e2efe` | `FACTORY` | `epidemic::gameplay::simulation::SimulationPolicyId` | `static constexpr SimulationPolicyId FromString(std::string_view s)noexcept`
- `9eb60b091c2cf21d` | `QUERY` | `epidemic::gameplay::simulation::SimulationService` | `[[nodiscard]] std::vector<SimulationSummary> FindSummaries(SimulationRegionId region)const;`
- `9fea7049e21f72ed` | `QUERY` | `epidemic::gameplay::simulation::SimulationService` | `[[nodiscard]] const SimulationRegion*FindRegion(SimulationRegionId id)const noexcept;`
- `a0d87004d560cd15` | `QUERY` | `epidemic::gameplay::simulation::SimulationTaskId` | `[[nodiscard]] constexpr bool operator==(const SimulationTaskId&)const noexcept=default;`
- `a0e89f0f3d998d7a` | `QUERY` | `epidemic::gameplay::simulation::SimulationLayerId` | `[[nodiscard]] constexpr bool operator==(const SimulationLayerId&)const noexcept=default;`
- `ae93dfee55f9b9c9` | `MUTATOR` | `epidemic::gameplay::simulation::ISimulationLayerExecutor` | `[[nodiscard]] virtual foundation::Result<void> Commit(const SimulationTask&task,const SimulationLayerSummary&summary)=0;`
- `b1afb46588a419c0` | `MUTATOR` | `epidemic::gameplay::simulation::SimulationService` | `[[nodiscard]] foundation::Result<SimulationSummaryId> SimulateInterval(SimulationRegionId region,GameplayTimePoint from,GameplayTimePoint to,GameplayContext context={});`
- `b1cf1df51af90cd9` | `DESTRUCTOR` | `epidemic::gameplay::simulation::ISimulationLayerExecutor` | `virtual ~ISimulationLayerExecutor()=default;`
- `b8508f22677227d6` | `QUERY` | `epidemic::gameplay::simulation::SimulationService` | `[[nodiscard]] const SimulationLayerDefinition*FindLayer(SimulationLayerId id)const noexcept;`
- `bca8a384109bc5b5` | `CONSTRUCTOR` | `epidemic::gameplay::simulation::SimulationService` | `SimulationService();`
- `d6ca86785fb42f06` | `QUERY` | `epidemic::gameplay::simulation::SimulationRegionId` | `[[nodiscard]] constexpr bool operator==(const SimulationRegionId&)const noexcept=default;`
- `d6d4c530c0ce83f4` | `QUERY` | `epidemic::gameplay::simulation::SimulationRegionId` | `[[nodiscard]] constexpr auto operator<=>(const SimulationRegionId&)const noexcept=default;`
- `db3f3b6c42e4abb8` | `QUERY` | `epidemic::gameplay::simulation::SimulationLayerId` | `[[nodiscard]] constexpr auto operator<=>(const SimulationLayerId&)const noexcept=default;`
- `db93826504bc4a74` | `QUERY` | `epidemic::gameplay::simulation::SimulationTaskId` | `[[nodiscard]] constexpr auto operator<=>(const SimulationTaskId&)const noexcept=default;`
- `dc665e6895efe424` | `FACTORY` | `epidemic::gameplay::simulation::SimulationLayerId` | `static constexpr SimulationLayerId FromString(std::string_view s)noexcept`
- `e1c26f42d3c78b3c` | `QUERY` | `epidemic::gameplay::simulation::SimulationTaskId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `e8627dd8741619d5` | `LIFECYCLE` | `epidemic::gameplay::simulation::SimulationService` | `void FreezeDefinitions()noexcept`
- `f7baec16396a4838` | `QUERY` | `epidemic::gameplay::simulation::SimulationService` | `[[nodiscard]] Revision CurrentRevision()const noexcept`
- `fd3f292d87d7ab10` | `FACTORY` | `epidemic::gameplay::simulation::SimulationRegionId` | `static constexpr SimulationRegionId FromString(std::string_view s)noexcept`

## Local-ready projection

All 37 Goal 4 local criteria have a typed block-local `PASS` decision with exact contract/state/test evidence in `_goal4_handoff/B05/local_ready.json`. All 15 dossier fields are `REVIEWED`; canonical promotion remains the serial integrator step.
