# Milestone 2 full audit, 10-09-2026

## Verdict

**Milestone 2 is not complete on the current `dev` working tree.**

Audit baseline: branch `dev`, HEAD `40a38d72e3059bc844951ee1bd2f328bb77f597d` plus the currently applied, uncommitted deltas. The current source and tests are the only source of truth. The three reports in `Deltas` were used only to locate candidate paths; none of their 300 findings was carried forward without a fresh check.

The Debug build succeeds. EngineBase and EngineRuntime pass 29/29 tests. The complete test set is 81/88: seven processes reach their 240 second timeout. Six of them contain allocation-fault tests; the seventh is the load test. Therefore the statements `88/88` and `Milestone 2: complete` in `Milestones.md` and `Milestones_review.md` are stale for the current working tree.

Status meanings:

- `PASS`: current implementation and an executable regression test cover the required local contract.
- `FAIL`: a current defect or a failing/timeout mandatory local suite is confirmed.
- `UNPROVEN`: no current defect was proven, but the required failure contract is not exercised successfully.

## Test diagnostics

Each suite was launched by CTest as a separate process with the configured timeout.

| Range | Result | Duration |
|---|---:|---:|
| EngineBase + EngineRuntime, tests 1-29 | 29/29 PASS | 5.25 s total |
| Framework and truth, tests 30-88 | 52/59 PASS | seven 240 s timeouts |
| Complete set | 81/88 PASS | not 88/88 |

Timed out:

| Test | Current diagnosis |
|---|---|
| `EpidemicGameFrameworkFoundationTests` | `FAIL`: instrumented copy reaches `GameplayTagRegistry::Register("fault.deep.hierarchy.leaf")` with `FailAfter(0)` and does not return or throw; postcondition is never reached. The preceding `StableTypeRegistry` fault loop completes. |
| `EpidemicGameplayQueriesTests` | `UNPROVEN`: process timeout in a suite containing `RegisterProvider`/`Freeze` fault injection; exact fault index was not established. |
| `EpidemicGameplayFactsTests` | `UNPROVEN`: process timeout in a suite containing `Commit`, `Publish`, and `Dispatch` fault injection; exact fault index was not established. |
| `EpidemicGameplayTimeTests` | `FAIL`: timeout suite plus a separately confirmed quadratic scheduling path. |
| `EpidemicGameFrameworkAbilitiesTests` | `UNPROVEN`: process timeout in a suite containing `BindSchedule`, `BeginActivation`, and `CompleteExecution` fault injection; exact fault index was not established. |
| `EpidemicGameFrameworkAITests` | `FAIL`: timeout suite plus a separately confirmed quadratic registration path. |
| `EpidemicEngineLoadTruthTests` | `FAIL`: exceeds its explicit 240 s timeout. |

The temporary probe and instrumented Foundation copy are under `build/milestone2-audit`; no tracked test was changed. A minimal `FailAfter(0)` allocation probe throws `std::bad_alloc` and exits normally, so the allocator override alone is not sufficient to reproduce the hang. The Foundation hang is currently a test/implementation interaction blocker; it is not attributed solely to either side without a stack trace.

## Current findings

### M2-01 [P1] World journal append can terminate and consume a cursor without publishing a change

**API:** all `WorldService` mutations that call `Record`.

**Where:** `EngineFramework/GameplayWorldStateOwners/World/src/world.cpp:158-168`; storage is `std::deque<WorldChange>` at `world.h:462`.

**Violation:** `Record` is `noexcept`, updates `last_change_sequence_` and `next_change_sequence_`, then calls allocating `changes_.push_back`. Allocation failure invokes `std::terminate`; if the implementation ever catches internally, the cursor has already been consumed. This violates controlled failure semantics and state/journal atomicity.

**Reproduction:** inject failure into the allocation performed by `push_back` while calling any runtime mutation, for example `AddDynamicFeature`. The call cannot return a controlled `Result` and cannot preserve the pre-call cursor.

**Impact:** process termination or authoritative state with a missing incremental change; consumers can silently diverge.

**Fix:** reserve/stage the journal event before publishing state, or build a complete post-state journal and commit it with a no-throw swap. Advance sequence fields only after successful append. Remove `noexcept` unless the operation is genuinely non-throwing.

**Regression:** for every World mutation family, fail every allocation index and compare state, revision, generator, indexes, journal and cursor byte-for-byte/logically with the pre-call snapshot.

### M2-02 [P1] World runtime mutations publish state, indexes and journal non-atomically

**API:** `AddDynamicFeature`, `UpdateDynamicFeature`, `RemoveDynamicFeature`, `PlaceObject`, `RemoveObjectPlacement`, `WorldTransaction::Commit`, `CompactAlteration`.

**Where:** `world.cpp:285-319`, `world.cpp:357-410`, `world.cpp:423-429`.

**Violation:** the methods update live records/revision before fallible container or journal work. `CommitMutations` applies each alteration and its spatial index incrementally at lines 393-399, then emits journal entries at 401-407. A throw can leave an arbitrary committed prefix. `CompactAlteration` erases first and records last.

**Reproduction:** execute a transaction with at least two alterations and fail allocation in `alterations_.emplace`, `IndexAlteration`, or a later journal append. Direct methods have the same failure window at their final insertion/append.

**Impact:** primary records, spatial indexes, revision and journal can describe different worlds; retry is not deterministic.

**Fix:** construct complete replacement primary/index/journal state off-state, validate it, and publish with no-throw swaps. Do not mutate the live revision or generator until every throwing step has succeeded.

**Regression:** allocation sweep for each direct API and for two-element create/update/remove transactions; after each failure verify exact pre-state and both spatial queries.

### M2-03 [P1] World restore replaces live state before rebuilding its allocating index

**API:** `WorldService::RestoreSnapshot`.

**Where:** `world.cpp:438-456`; `RebuildAlterationIndex` is `world.cpp:146-150`.

**Violation:** line 455 assigns `features_`, `object_placements_`, `alterations_`, generator, revision and journal state, then calls `RebuildAlterationIndex`, which clears and allocates into live indexes. Failure leaves the old valid world destroyed and the replacement only partly indexed.

**Reproduction:** restore a valid snapshot containing alterations and fail an allocation during `RebuildAlterationIndex`.

**Impact:** a failed load is destructive; direct lookup and spatial lookup disagree.

**Fix:** build and validate replacement indexes beside the staged primary state, then swap every component in one no-throw commit.

**Regression:** allocation sweep over restore with small and large alterations; every failed restore must preserve the original snapshot, indexes, journal epoch and cursor.

### M2-04 [P1] World accepts out-of-domain alteration enums

**API:** transaction create/update and `RestoreSnapshot` through `ValidateAlteration`.

**Where:** enum declarations `world.h:185-197`; validator `world.cpp:324-329`; transition check `world.cpp:339-344`; restore loop `world.cpp:452-453`.

**Violation:** `ValidateAlteration` rejects only `Compacted`; it does not reject integer-cast values outside `WorldAlterationState` or `WorldAlterationPersistence`. Create and restore can therefore publish invalid authoritative enum values.

**Reproduction:** set `state` or `persistence` to `static_cast<...>(255)` in a transaction record or snapshot; the current validation has no exhaustive domain check.

**Impact:** later switches and persistence filtering operate on an undefined lifecycle value, and saves can permanently retain malformed state.

**Fix:** add exhaustive `EnumInRange`/named validators and reuse them in create, update and restore before any mutation.

**Regression:** test every valid enumerator and values `-1`, `max + 1`, and `255` for both mutation and restore paths.

### M2-05 [P1] Load truth is quadratic in Time scheduling and AI registration

**API:** `GameplayTimeService::Schedule`, `AIService::RegisterAgent`.

**Where:** Time copies `schedules_` and `schedule_index_` per insertion at `gameplay_time.cpp:443-444`; AI copies `agents_` and `due_agents_` per insertion at `ai.cpp:320-325`. The truth workload creates 20,000 schedules (`Tests/Truth/engine_load_tests.cpp:65`) and 10,000 agents (`:148`); timeout is 240 s (`Tests/Truth/CMakeLists.txt:63`).

**Violation:** repeated whole-container staging makes both build loops O(n^2). The mandatory load test exceeds its budget.

**Reproduction:** run `ctest --test-dir build/framework-check -C Debug -R EpidemicEngineLoadTruthTests --output-on-failure`; it times out at 240 s.

**Impact:** Milestone 2's load regression is red and ordinary bulk world initialization scales pathologically.

**Fix:** stage only the new record/index node with rollback guards or provide a checked bulk insertion transaction. Preserve strong exception safety without copying the complete owner on every mutation.

**Regression:** retain the current truth workload and add per-section timing output; add growth checks comparing N and 2N to detect quadratic regressions without relying only on a wall-clock ceiling.

### M2-06 [P2] Generator restore helper is `noexcept` while constructing allocating errors

**API:** `RestoreMonotonicIdGeneratorSnapshot`.

**Where:** `EngineFramework/BaseInfrastructure/Foundation/include/Epidemic/GameFramework/Foundation/id_generator.h:166-185`; `Error::Create` allocates three `std::string`s at `EngineBase/Foundation/include/Epidemic/Foundation/error.h:27-30`.

**Violation:** invalid input enters a `noexcept` function that constructs owning error strings. Allocation failure terminates instead of returning a defined failure.

**Reproduction:** pass an invalid snapshot and inject failure into error construction.

**Impact:** corrupted save validation can terminate the process precisely on the recovery path.

**Fix:** remove `noexcept`, or return a non-allocating status from the helper and construct the owned `Error` at a throwing boundary.

**Regression:** inject allocation failure for every invalid snapshot status and assert controlled propagation/no state change.

### M2-07 [P1 gate] Required allocation-failure contracts are not proved for Parts 2 and 3

**Where:** only `foundation_tests.cpp`, `queries_tests.cpp`, `facts_tests.cpp`, `time_tests.cpp`, `abilities_tests.cpp`, and `ai_tests.cpp` include `allocation_fault_injection.h`. No Part 2 or Part 3 local suite uses it. Part 2's own acceptance text requires allocation injection for APIs touching multiple allocating containers or a journal.

**Violation:** passing logical tests cannot prove the stated Milestone 2 exception-path postcondition. This is a coverage blocker, not evidence that every untested API is defective.

**Fix:** build a public-mutation contract matrix per major and add allocation-index sweeps around every multi-container/journal mutation, comparing complete pre/post state.

**Regression:** these tests are the regression requirement; suites must complete under a bounded timeout and report API plus fault index on failure.

## Requirements cross-check

| Source requirement | Status | Evidence |
|---|---|---|
| `Framework_Local_Correctness_Audit_Part1.md`: 13 majors, full mutation/failure matrix | `FAIL` | Six local suites time out; Foundation is localized at tag registration; Time and AI also fail load behavior. |
| `Framework_Local_Correctness_Audit_Part2.md`: 13 majors, allocation injection for multi-container/journal APIs | `UNPROVEN` | Functional suites pass, but none includes the allocator harness required by the document. |
| `Framework_Local_Correctness_Audit_Part3.md`: 12 majors including World | `FAIL` | The old report contains no World section and neither `3-1.zip` nor `3-2.zip` changes World. Fresh findings M2-01 through M2-04 remain. Allocation coverage for the other 11 majors is absent. |

## Module status

EngineBase and EngineRuntime were re-run on the current tree; prior `29/29` was not used as a substitute.

| Area / major | Status | Reason |
|---|---|---|
| EngineBase Foundation, Memory, Diagnostics, Core, RHI, Application, Platform/Input, RHI_D3D11, regression umbrella | `PASS` | Current tests 1-9 pass; fresh overflow/lifecycle/failure-path scan found no current blocker. |
| EngineRuntime RuntimeFoundation, Assets, Resources, Serialization, Persistence, Time, Environment, Scene, World, Streaming, Renderer, Physics, Navigation, Animation, Audio, Simulation, Support | `PASS` | Current local, architecture, integration and regression tests 10-29 pass; fresh unchecked-revision scan confirmed guards around live mutation paths. |
| Framework Foundation | `FAIL` | M2-06 and localized fault-injection hang. |
| Queries | `UNPROVEN` | Mandatory fault suite times out; exact failing API/index remains unproved. |
| Facts | `UNPROVEN` | Mandatory fault suite times out; exact failing API/index remains unproved. |
| SupportRandom | `PASS` | Local suite passes; current boundary/constructor fixes are exercised. |
| Time | `FAIL` | Local timeout and M2-05. |
| Abilities | `UNPROVEN` | Mandatory fault suite times out. |
| AI | `FAIL` | Local timeout and M2-05. |
| Combat | `PASS` | Local suite passes including current lifecycle/revision regressions. |
| Conditions | `PASS` | Local suite passes including current mutation regressions. |
| Construction | `PASS` | Local suite passes including socket journal regression. |
| Crime | `PASS` | Local suite passes; corrected three-record expectation is current. |
| Dialogue | `PASS` | Local suite passes current callback/lifecycle regressions. |
| Economy | `PASS` | Local suite passes current revision/journal regressions. |
| Effects | `UNPROVEN` | Functional suite passes; allocation failure matrix absent. |
| Encounters | `UNPROVEN` | Functional suite passes; allocation failure matrix absent. |
| Entities | `UNPROVEN` | Functional and load portions pass; allocation failure matrix absent. |
| Environment | `UNPROVEN` | Functional suite passes; allocation failure matrix absent. |
| Equipment | `UNPROVEN` | Functional suite passes; allocation failure matrix absent. |
| Interaction | `UNPROVEN` | Functional suite passes; allocation failure matrix absent. |
| ItemsInventory | `UNPROVEN` | Functional suite passes; allocation failure matrix absent. |
| Knowledge | `UNPROVEN` | Functional suite passes; allocation failure matrix absent. |
| Loot | `UNPROVEN` | Functional suite passes; allocation/external-commit matrix absent. |
| Materials | `UNPROVEN` | Functional suite passes; allocation failure matrix absent. |
| Narrative | `UNPROVEN` | Both functional suites pass; allocation/external-commit matrix absent. |
| NavigationSemantics | `UNPROVEN` | Functional suite passes; allocation failure matrix absent. |
| NeedsLife | `UNPROVEN` | Functional suite passes; allocation failure matrix absent. |
| Ownership | `UNPROVEN` | Functional suite passes; allocation failure matrix absent. |
| Perception | `UNPROVEN` | Functional suite passes; allocation/callback matrix absent. |
| Population | `UNPROVEN` | Functional suite passes; allocation failure matrix absent. |
| Processes | `UNPROVEN` | Functional suite passes; allocation/external-resource matrix absent. |
| Progression | `UNPROVEN` | Functional suite passes; allocation failure matrix absent. |
| ResourcesProduction | `UNPROVEN` | Both local/separation suites pass; allocation/external-resource matrix absent. |
| RolesJobs | `UNPROVEN` | Functional suite passes; allocation failure matrix absent. |
| SaveGame | `UNPROVEN` | Functional suite passes; allocation/callback matrix absent. |
| Simulation | `UNPROVEN` | Functional suite passes; allocation/callback matrix absent. |
| Society | `UNPROVEN` | Functional suite passes; allocation failure matrix absent. |
| Traversal | `UNPROVEN` | Functional suite passes; allocation failure matrix absent. |
| World | `FAIL` | M2-01 through M2-04; omitted by old Part 3 fixes. |

## Disposition of old findings

The old reports describe `Engine_9-9-2026-1(6).zip`, not this working tree. Their counts (Part 1: 100, Part 2: 99, Part 3: 101) are not current defect counts. Most named paths now contain checked revisions, enum validation, staged generators or staged state and their new regression tests pass. Those entries are obsolete unless represented by a fresh finding above.

The old Part 3 scope says 12 majors but its findings cover only 11; `World` was never audited there. The old baseline statements `13/13`, `14/14`, `13/13`, and the milestone statements `88/88` describe earlier artifacts and must not be used as acceptance evidence for the current code.

## Closure conditions

Milestone 2 can be marked complete only after:

1. M2-01 through M2-06 are fixed without weakening public failure contracts.
2. All six allocation-fault suites terminate and report deterministic results; every hang is localized to an API/fault index and assigned to either harness or implementation.
3. Part 2 and Part 3 receive the missing mutation/failure contract tests, including a fresh World suite.
4. `EpidemicEngineLoadTruthTests` completes below its 240 second budget with the current workload.
5. A clean Debug run returns 88/88 (or the intentionally revised, documented test count), with no timeout.
