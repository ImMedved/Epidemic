# EngineRuntime/Time local freeze audit

Scope: Goal 3.2. Module: `EngineRuntime/Time`, target `EpidemicRuntimeTimeTests`.

Status: module-local contracts, regressions and persistence continuation are complete. Publication into the shared `LOCAL_READY` ledger and regeneration of global API/surface/dossier manifests are intentionally left to the integrated Goal 3 tree so parallel module deltas do not overwrite one another.

## Reviewed state and ownership

Time is the sole owner of authoritative Runtime game time, current normalized time scale, pause state, fractional tick accumulator remainder, revision and transient operation/event state. `TimeSnapshot` is frame-facing observation. The fractional remainder is authoritative because it changes future deterministic tick production and is therefore part of the persistent checkpoint state.

## Public contracts

`IGameClock` exposes `Now`, `LastDelta` and `TimeSnapshot`. `ITimeRuntime` owns `Advance`, `Pause`, `Resume`, `SetTimeScale`, `Skip`, `CaptureCheckpoint` and `RestoreCheckpoint`.

`TimeCheckpoint` contains current game time, normalized current `TimeScale`, pause state, fractional remainder and revision. It also carries immutable compatibility identity: `game_ticks_per_real_second`, calendar definition and canonical phase boundaries. `last_delta`, current event buffer and operation-only `TimeRuntimeState` are transient and are reconstructed/cleared on restore.

## Configuration and boundary audit

Creation rejects non-positive tick rate, invalid time scale, zero calendar units, calendar multiplication overflow, invalid phase enum, phase minutes outside the configured day and duplicate phase minutes. Empty phase boundaries resolve to the standard defaults before validation, so a custom short day cannot bypass boundary validation and then fail later in the constructor.

Advance rejects negative real delta and checked-multiplies all rational scaling terms. Skip rejects negative durations. Both reject `GameTimePoint` overflow before commit. Scale denominator overflow is rejected before state mutation. Revision exhaustion is checked before every authoritative change, including fractional-remainder-only advancement.

## State, revision and no-op semantics

Fractional accumulation is integer/rational and deterministic across frame splits. Changing scale and a non-zero Skip reset the fractional remainder. A remainder-only change is authoritative and increments revision even when no whole game tick was produced. A change only to transient `last_delta`, such as `Advance(0)`, does not increment revision. Duplicate Pause/Resume remain no-ops and preserve the existing event buffer.

## Event semantics

A successful advancing operation emits its operation event first, followed by `DayChanged` and then `DayPhaseChanged` when those semantic boundaries were crossed. Boundary detection considers the complete interval rather than only comparing initial and final phases. A one-day jump that starts and ends in Night therefore still reports the phase transitions crossed inside the interval. Event staging is completed off-state before live state commit.

## Failure atomicity

All mutable operations build candidate state before commit. Event-buffer capacity is prepared before publication. The concrete Time test hook injects `std::bad_alloc` at the event-staging boundary and proves that snapshot, checkpoint and event buffer stay equal to the pre-call state. The final commit uses no-throw moves for `TimeOptions` and the event vector, guarded by compile-time assertions.

Restore validates checkpoint contents and immutable configuration against the destination runtime before building a candidate. Invalid or incompatible checkpoints leave the exact live state unchanged. Candidate allocation failure also leaves the exact pre-state. A successful restore commits once and clears transient delta/events.

## G3-TIME-001 regression

A checkpoint captured with a non-zero fractional remainder can be restored into a fresh runtime with matching immutable configuration. Continuing both the uninterrupted clock and the restored clock with the same input produces identical snapshot, checkpoint and events. This closes the original defect where two clocks with the same public `TimeSnapshot` could have different future deterministic behavior.

## Additional audit regressions

`TestFactoryRejectsInvalidOptions` covers duplicate phase boundaries, overflowing calendar configuration and invalid default boundaries for a short custom day. `TestLargeSkipReportsCrossedPhaseBoundaryWhenFinalPhaseMatches` covers semantic phase crossings whose final phase equals the initial phase. `TestFractionalRemainderMutationIncrementsRevision` covers the newly authoritative remainder. `TestRevisionExhaustionPreservesState` verifies both remainder-only advance and Skip at `UINT64_MAX`. Checkpoint tests cover invalid normalized scale, negative time, negative/out-of-range remainder, tick-rate/calendar/phase incompatibility, allocation failure, pause restoration and transient-state clearing.

## Verification

The Time suite passes C++20 with warnings-as-errors in four portable configurations: GCC Debug, GCC Release, Clang Debug and Clang Release. A Clang AddressSanitizer plus UndefinedBehaviorSanitizer run passes, and a standalone public-header consumer for `time_runtime.h` compiles and runs.

The repository-wide CMake profile cannot be executed in the Linux task container because the existing `EngineBase/Platform` CMake contract intentionally rejects non-Windows platform builds before reaching Runtime tests. No project code was changed to bypass that platform gate. The official Windows MSVC Debug/Release and complete CTest qualification therefore remains an integration-step check after parallel Goal 3 deltas are merged.
