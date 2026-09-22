# Goal 3 Block A evidence fragment: Time

Date: 2026-09-21. This file is module-local handoff evidence. Shared generated manifests and `local_ready_ledger.json` are intentionally unchanged. Final admission is conditional on MSVC Debug/Release passing after merge.

## Public signature anchor proposals

The stale generated coverage input contains 17 Time callables. The final source has 19 because `CaptureCheckpoint()` and `RestoreCheckpoint(...)` are intentionally added. Proposals below are signature-keyed so the serial integrator can bind final callable IDs after regeneration.

| Public signature | Proposed contract anchor | Proposed test/audit anchor |
| --- | --- | --- |
| `epidemic::runtime::CalendarDate::[[nodiscard]] constexpr bool operator==(const CalendarDate&)const noexcept=default;` | `docs/EngineRuntime/modules/time.md::Контракты` | `time_tests.cpp::TestInitialSnapshotIsStable; TestCalendarConversion; TestDayAndPhaseTransitions` |
| `epidemic::runtime::[[nodiscard]] constexpr bool IsZero(GameDuration duration)noexcept` | `docs/EngineRuntime/modules/time.md::Контракты` | `time_tests.cpp::TestInitialSnapshotIsStable; TestCalendarConversion; TestDayAndPhaseTransitions` |
| `epidemic::runtime::TimeEvent::[[nodiscard]] constexpr bool operator==(const TimeEvent&)const noexcept=default;` | `docs/EngineRuntime/modules/time.md::Контракты` | `time_tests.cpp::TestInitialSnapshotIsStable; TestCalendarConversion; TestDayAndPhaseTransitions` |
| `epidemic::runtime::[[nodiscard]] foundation::Result<TimeServices> CreateTimeServices(const TimeOptions&options={});` | `docs/freeze/engineruntime_time_audit.md::Configuration and boundary audit` | `time_tests.cpp::TestFactoryCreatesSplitServices; TestFactoryRejectsInvalidOptions; TestInvalidPhaseAndDirectInvalidConstructionAreRejected` |
| `epidemic::runtime::[[nodiscard]] foundation::Result<void> ValidateTimeOptions(const TimeOptions&options);` | `docs/freeze/engineruntime_time_audit.md::Configuration and boundary audit` | `time_tests.cpp::TestFactoryCreatesSplitServices; TestFactoryRejectsInvalidOptions; TestInvalidPhaseAndDirectInvalidConstructionAreRejected` |
| `epidemic::runtime::IGameClock::[[nodiscard]] virtual GameDuration LastDelta()const=0;` | `docs/freeze/engineruntime_time_audit.md::Reviewed state and ownership / Public contracts` | `time_tests.cpp::TestInitialSnapshotIsStable; TestGameTimeArithmeticWorks` |
| `epidemic::runtime::IGameClock::[[nodiscard]] virtual GameTimePoint Now()const=0;` | `docs/freeze/engineruntime_time_audit.md::Reviewed state and ownership / Public contracts` | `time_tests.cpp::TestInitialSnapshotIsStable; TestGameTimeArithmeticWorks` |
| `epidemic::runtime::IGameClock::[[nodiscard]] virtual TimeSnapshot GetSnapshot()const=0;` | `docs/freeze/engineruntime_time_audit.md::Reviewed state and ownership / Public contracts` | `time_tests.cpp::TestInitialSnapshotIsStable; TestGameTimeArithmeticWorks` |
| `epidemic::runtime::IGameClock::virtual ~IGameClock()=default;` | `docs/EngineRuntime/modules/time.md::Контракты` | `time_tests.cpp::TestInitialSnapshotIsStable; TestCalendarConversion; TestDayAndPhaseTransitions` |
| `epidemic::runtime::ITimeRuntime::[[nodiscard]] virtual TimeCheckpoint CaptureCheckpoint()const=0;` | `docs/freeze/engineruntime_time_audit.md::Public contracts / G3-TIME-001 regression` | `time_tests.cpp::TestCheckpointCapturesCanonicalPersistentState; TestCheckpointContinuationPreservesFractionalRemainder` |
| `epidemic::runtime::ITimeRuntime::[[nodiscard]] virtual foundation::Result<TimeAdvanceResult> Advance(std::chrono::microseconds real_delta)=0;` | `docs/freeze/engineruntime_time_audit.md::Configuration and boundary audit / State, revision and no-op semantics` | `time_tests.cpp::TestDeterministicAccumulationKeepsFractionalRemainder; TestNegativeAdvancePreservesObservableState; TestAdvanceOverflowIsRejected; TestRevisionExhaustionPreservesState` |
| `epidemic::runtime::ITimeRuntime::[[nodiscard]] virtual foundation::Result<TimeAdvanceResult> Skip(GameDuration duration)=0;` | `docs/freeze/engineruntime_time_audit.md::Configuration and boundary audit / Event semantics` | `time_tests.cpp::TestTimeSkipProducesJumpEvent; TestNegativeSkipIsRejected; TestSkipOverflowIsRejected; TestLargeSkipReportsCrossedPhaseBoundaryWhenFinalPhaseMatches` |
| `epidemic::runtime::ITimeRuntime::[[nodiscard]] virtual foundation::Result<void> Pause()=0;` | `docs/freeze/engineruntime_time_audit.md::State, revision and no-op semantics` | `time_tests.cpp::TestPauseResume; TestRevisionIncrementsOnlyOnChanges; TestIdempotentMutationsDoNotClearEvents` |
| `epidemic::runtime::ITimeRuntime::[[nodiscard]] virtual foundation::Result<void> RestoreCheckpoint(const TimeCheckpoint&checkpoint)=0;` | `docs/freeze/engineruntime_time_audit.md::Failure atomicity / G3-TIME-001 regression` | `time_tests.cpp::TestInvalidCheckpointPreservesLiveClock; TestIncompatibleCheckpointPreservesLiveClock; TestRestoreAllocationFailurePreservesLiveClock; TestRestoreClearsTransientStateAndRestoresPause` |
| `epidemic::runtime::ITimeRuntime::[[nodiscard]] virtual foundation::Result<void> Resume()=0;` | `docs/freeze/engineruntime_time_audit.md::State, revision and no-op semantics` | `time_tests.cpp::TestPauseResume; TestRevisionIncrementsOnlyOnChanges; TestIdempotentMutationsDoNotClearEvents` |
| `epidemic::runtime::ITimeRuntime::[[nodiscard]] virtual foundation::Result<void> SetTimeScale(TimeScale scale)=0;` | `docs/freeze/engineruntime_time_audit.md::Configuration and boundary audit / State, revision and no-op semantics` | `time_tests.cpp::TestInvalidTimeScaleIsRejected; TestTimeScaleChangesDeltaMultiplier; TestTimeScaleIsNormalized; TestTimeScaleChangeDropsFractionalRemainder` |
| `epidemic::runtime::ITimeRuntime::virtual ~ITimeRuntime()=default;` | `docs/EngineRuntime/modules/time.md::Контракты` | `time_tests.cpp::TestInitialSnapshotIsStable; TestCalendarConversion; TestDayAndPhaseTransitions` |
| `epidemic::runtime::TimeScale::[[nodiscard]] constexpr bool operator==(const TimeScale&)const noexcept=default;` | `docs/EngineRuntime/modules/time.md::Контракты` | `time_tests.cpp::TestInitialSnapshotIsStable; TestCalendarConversion; TestDayAndPhaseTransitions` |
| `epidemic::runtime::TimeSnapshot::[[nodiscard]] constexpr bool operator==(const TimeSnapshot&)const noexcept=default;` | `docs/EngineRuntime/modules/time.md::Контракты` | `time_tests.cpp::TestInitialSnapshotIsStable; TestCalendarConversion; TestDayAndPhaseTransitions` |

## Dossier field review decisions

| Field | Decision | Rationale |
| --- | --- | --- |
| `Responsibility` | `REVIEWED` | Sole Runtime owner of authoritative game clock progression, scale, pause state, deterministic fractional remainder and time events. |
| `Authoritative state` | `REVIEWED` | now, normalized time scale, pause state, fractional remainder and revision. |
| `Derived/cache/index state` | `REVIEWED` | TimeSnapshot and calendar/day-phase observations are rebuilt from candidate authoritative state; no secondary lookup index. |
| `Public mutation API` | `REVIEWED` | Advance, Pause, Resume, SetTimeScale, Skip and RestoreCheckpoint; all have candidate-before-commit or explicit no-op behavior. |
| `Read/query API for invariants` | `REVIEWED` | Now, LastDelta, GetSnapshot and CaptureCheckpoint expose deterministic state observations. |
| `Public headers and types` | `REVIEWED` | TimeCheckpoint is persistence-complete; TimeSnapshot remains frame-facing observation. |
| `Dependency list` | `REVIEWED` | RuntimeFoundation and Foundation only; no Runtime peer dependency. |
| `External ports/callbacks/providers/backends` | `REVIEWED` | None. |
| `ID spaces, generations, revisions and cursors` | `REVIEWED` | No handle ID space; revision is checked and exhaustion rejects before mutation. |
| `State machines` | `REVIEWED` | Running/paused semantics are explicit; pause/resume duplicate operations are no-ops. |
| `Local invariants` | `REVIEWED` | Non-negative time, normalized positive scale, canonical phase boundaries, rational remainder bounds and deterministic event order. |
| `Persistent and transient state` | `REVIEWED` | Checkpoint includes remainder/scale/pause/revision/config identity; last_delta/events/operation-only state are transient. |
| `Snapshot/restore contract` | `REVIEWED` | Validate complete checkpoint and compatibility, build candidate off-state, one no-fail commit, clear transient state. |
| `Hard limits, budgets and complexity bounds` | `REVIEWED` | All calendar/scaling multiplications and revision/time bounds checked before commit; event staging reserves before publication. |
| `Threading contract` | `REVIEWED` | No internal worker/thread ownership; synchronous caller-serialized mutable runtime contract. |

## Generated mutator obligation review

After final regeneration Time has six public mutators with four obligations each: `Advance`, `Skip`, `Pause`, `Resume`, `SetTimeScale`, and `RestoreCheckpoint`. For every one, `preconditions`, `success`, `failure`, and `noop` are reviewed against the Time audit plus the named regressions in the public-anchor table. `RestoreCheckpoint` has no semantic no-op shortcut: an equal valid checkpoint is still a valid candidate/commit path, and the obligation is reviewed as defined behavior rather than inferred from scanner output.

## Stale-identity and lifecycle candidate review

| Candidate | Decision | Evidence |
| --- | --- | --- |
| `[[nodiscard]] virtual foundation::Result<void> Pause()=0;` | `REVIEWED / scanner false positive for stale identity` | Time has no handle/generation identity; behavior is state/revision based and covered by `TestRevisionExhaustionPreservesState`, lifecycle tests, and checkpoint atomicity tests. |
| `[[nodiscard]] virtual foundation::Result<void> RestoreCheckpoint(const TimeCheckpoint& checkpoint)=0;` | `REVIEWED / scanner false positive for stale identity` | Time has no handle/generation identity; behavior is state/revision based and covered by `TestRevisionExhaustionPreservesState`, lifecycle tests, and checkpoint atomicity tests. |
| `[[nodiscard]] virtual foundation::Result<void> Resume()=0;` | `REVIEWED / scanner false positive for stale identity` | Time has no handle/generation identity; behavior is state/revision based and covered by `TestRevisionExhaustionPreservesState`, lifecycle tests, and checkpoint atomicity tests. |
| `[[nodiscard]] virtual foundation::Result<void> SetTimeScale(TimeScale scale)=0;` | `REVIEWED / scanner false positive for stale identity` | Time has no handle/generation identity; behavior is state/revision based and covered by `TestRevisionExhaustionPreservesState`, lifecycle tests, and checkpoint atomicity tests. |
| `[[nodiscard]] virtual foundation::Result<void> Pause()=0;` | `REVIEWED / real lifecycle transition` | `TestPauseResume`; `TestRevisionIncrementsOnlyOnChanges`; `TestIdempotentMutationsDoNotClearEvents`. |
| `[[nodiscard]] virtual foundation::Result<void> Resume()=0;` | `REVIEWED / real lifecycle transition` | `TestPauseResume`; `TestRevisionIncrementsOnlyOnChanges`; `TestIdempotentMutationsDoNotClearEvents`. |

Time has no external-boundary candidates.

## LOCAL_READY criteria handoff

| Criterion | Proposed decision after MSVC qualification | Module-specific rationale/evidence |
| --- | --- | --- |
| `ARCH-RESPONSIBILITY` | `PASS` | Generated architecture ownership assigns clock responsibility to Time. |
| `ARCH-SINGLE-OWNER` | `PASS` | Authoritative game time has one Runtime owner. |
| `ARCH-DIRECT-DEPS` | `PASS` | Time links only Foundation and RuntimeFoundation. |
| `ARCH-PORTS` | `PASS` | No peer-major direct dependency or external backend port. |
| `API-CLASSIFIED` | `PASS` | All 19 final public callables have explicit query/mutator/factory/value roles; final IDs are assigned after merge. |
| `API-PRECONDITIONS` | `PASS` | Negative deltas/skips, invalid scale/calendar/phase/checkpoint inputs are explicit regressions. |
| `API-SUCCESS` | `PASS` | Advance, Skip, scale and lifecycle success effects and event order are covered. |
| `API-FAILURE` | `PASS` | Overflow, revision exhaustion, invalid checkpoint and allocation failure preserve pre-state. |
| `API-OVERLOADS` | `N/A` | No public overload family has competing semantic contracts. |
| `API-INVALID` | `PASS` | Invalid options, dates, phase enums, checkpoint scale/remainder/configuration are rejected. |
| `STATE-PRIMARY` | `PASS` | now/scale/pause/remainder/revision are explicitly identified as authoritative. |
| `STATE-INDEXES` | `N/A` | No secondary lookup index/cache with independent mutation exists. |
| `STATE-COUNTERS` | `PASS` | Revision and game-time arithmetic have overflow/exhaustion regressions. |
| `STATE-NO-FALSE-PUBLISH` | `PASS` | Candidate state and event staging complete before live commit. |
| `STATE-NOOP` | `PASS` | Advance(0), duplicate Pause/Resume and repeated semantic no-ops have defined revision/event behavior. |
| `LIFE-ALLOWED` | `PASS` | Pause/Resume and advancing while paused follow defined state semantics. |
| `LIFE-FORBIDDEN` | `PASS` | Invalid state/input mutations are rejected without publication; no hidden lifecycle path exists. |
| `LIFE-SHUTDOWN` | `N/A` | Time owns no shutdown-managed external resource. |
| `LIFE-RETRY-CLEANUP` | `N/A` | No fallible cleanup ownership exists. |
| `ATOMIC-SINGLE` | `PASS` | Every public mutation is preflighted/candidate-built before commit. |
| `ATOMIC-MULTI` | `PASS` | Compound clock/snapshot/remainder/event state publishes as one candidate commit. |
| `ATOMIC-EXTERNAL` | `N/A` | No external callback/backend operation. |
| `ATOMIC-RECONCILE` | `N/A` | No irreversible external success requiring reconciliation. |
| `PERSIST-SNAPSHOT` | `PASS` | TimeCheckpoint captures all future-determinism state plus immutable compatibility identity. |
| `PERSIST-VALIDATE` | `PASS` | Restore validates compatibility, normalized scale, time and remainder bounds before candidate build. |
| `PERSIST-CANDIDATE` | `PASS` | Restore builds the complete candidate before one no-fail commit. |
| `PERSIST-FAILURE` | `PASS` | Validation and injected allocation failure preserve exact live snapshot/checkpoint/events. |
| `PERSIST-CONTINUITY` | `PASS` | Restored continuation matches uninterrupted control with fractional remainder. |
| `TEST-HAPPY` | `PASS` | Normal advance/skip/calendar/scale/pause/restore paths covered. |
| `TEST-INVALID` | `PASS` | Invalid numeric/configuration/enum/checkpoint paths covered. |
| `TEST-DUPLICATE` | `PASS` | Duplicate Pause/Resume and duplicate phase boundary validation covered. |
| `TEST-STALE` | `N/A` | Time exposes no reusable handle/generation identity; scanner stale candidates are false positives. |
| `TEST-EMPTY` | `PASS` | Default options, empty phase-boundary input and empty transient events have defined behavior. |
| `TEST-BOUNDARY` | `PASS` | INT64/UINT64, calendar, scale denominator, remainder and crossed-boundary cases covered. |
| `TEST-WRONG-LIFECYCLE` | `PASS` | Paused Advance and duplicate pause/resume semantics are explicit regressions. |
| `TEST-CALLBACK-FAILURE` | `N/A` | Time invokes no external callback/provider/backend. |
| `TEST-REGRESSION` | `PASS` | G3-TIME-001 checkpoint continuity plus Block A invalid checkpoint hardening remain covered. |

## Defect evidence handoff

| ID | Verified defect | Fix | Regression | CTest target |
| --- | --- | --- | --- | --- |
| `G3-TIME-001` | `TimeSnapshot` alone omitted fractional remainder and could not guarantee deterministic continuation after persistence restore. | Persistence-complete `TimeCheckpoint`, validate-first restore candidate, one commit, immutable configuration compatibility identity. | `time_tests.cpp::TestCheckpointContinuationPreservesFractionalRemainder`; invalid/incompatible/allocation/ transient-state restore tests | `EpidemicRuntimeTimeTests` |

Block A hardening additionally rejects an out-of-domain `DayPhase` in checkpoint compatibility identity and a non-zero remainder whose scaled denominator is not representable. Portable GCC/Clang Debug/Release qualification passes; official MSVC qualification remains pending.
