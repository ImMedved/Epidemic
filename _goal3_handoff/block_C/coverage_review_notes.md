# Block C coverage review notes

These notes explain the machine-readable `coverage_reviews.json` decisions. Shared canonical files are not edited by this delta.

## EngineRuntime/Streaming

- Public callables: `52`.
- Mutator obligations: `60`.
- Lifecycle candidates: `3`.
- Stale-identity candidates: `13`.
- External-boundary candidates: `0`.
- Detailed per-ID review: `EngineRuntime/Streaming/docs/Goal3_Block_C_Evidence_2026-09-21.md`.

## EngineRuntime/Simulation

- Public callables: `55`.
- Mutator obligations: `84`.
- Lifecycle candidates: `1`.
- Stale-identity candidates: `16`.
- External-boundary candidates: `0`.
- Detailed per-ID review: `EngineRuntime/Simulation/docs/Goal3_Block_C_Evidence_2026-09-21.md`.

## EngineRuntime/Physics

- Public callables: `54`.
- Mutator obligations: `72`.
- Lifecycle candidates: `2`.
- Stale-identity candidates: `15`.
- External-boundary candidates: `15`.
- Detailed per-ID review: `EngineRuntime/Physics/docs/Goal3_Block_C_Evidence_2026-09-21.md`.

## EngineRuntime/Navigation

- Public callables: `33`.
- Mutator obligations: `28`.
- Lifecycle candidates: `0`.
- Stale-identity candidates: `5`.
- External-boundary candidates: `1`.
- Detailed per-ID review: `EngineRuntime/Navigation/docs/Goal3_Block_C_Evidence_2026-09-21.md`.

## Defect inventory

- `G3-STR-001` (EngineRuntime/Streaming): Runtime-local Tick preparation/allocation failure must propagate before mutation rather than becoming a semantic request failure. Regression: `TestTickAllocationPreflightFailuresPropagateWithoutMutation` / `EpidemicRuntimeStreamingTests`.
- `G3-STR-002` (EngineRuntime/Streaming): A successful ExecuteStep result is accepted exactly once and retained across later local commit failure so retry does not execute the backend step twice. Regression: `TestAcceptedStepResultIsNotExecutedAgainAfterLocalCommitFailure` / `EpidemicRuntimeStreamingTests`.
- `G3-STR-003` (EngineRuntime/Streaming): A Commit step in a progressive plan is unique and terminal, preventing external commit before later work. Regression: `TestCommitStepMustBeTerminalAndUniqueWhenPresent` / `EpidemicRuntimeStreamingTests`.
- `G3-STR-004` (EngineRuntime/Streaming): Request publication rollback restores prior chunk state/mapping and identity allocators when later publication fails. Regression: `TestRequestPublicationFailureRestoresPublishedState` / `EpidemicRuntimeStreamingTests`.
- `G3-SIM-001` (EngineRuntime/Simulation): Successful ExecuteStep output is durably owned before fallible local publication and retry does not call the job twice. Regression: `TestGoal3SimulationFaultAtomicity` / `EpidemicRuntimeSimulationTests`.
- `G3-SIM-002` (EngineRuntime/Simulation): Post-commit pruning/result bookkeeping is allocation-safe so late local allocation cannot make an accepted prefix ambiguous. Regression: `TestGoal3SimulationFaultAtomicity` / `EpidemicRuntimeSimulationTests`.
- `G3-SIM-AUDIT-001` (EngineRuntime/Simulation): Once retryable shutdown starts, new Tick/Schedule/main-thread/proposal work is rejected while cleanup ownership remains retryable. Regression: `TestGoal3SimulationFaultAtomicity` / `EpidemicRuntimeSimulationTests`.
- `G3-SIM-AUDIT-002` (EngineRuntime/Simulation): SetAttention contains first-insertion allocation failure in Result and preserves prior attention state. Regression: `TestGoal3SimulationFaultAtomicity` / `EpidemicRuntimeSimulationTests`.
- `G3-SIM-AUDIT-003` (EngineRuntime/Simulation): Proposal Publish contains allocation failure and leaves the proposal queue unchanged. Regression: `TestGoal3SimulationFaultAtomicity` / `EpidemicRuntimeSimulationTests`.
- `G3-PHYS-001` (EngineRuntime/Physics): Tick uses prefix-progress acceptance: accepted delta/prefix and backend simulation are not replayed after a later fixed-step synchronization/projection failure. Regression: `TestTickSyncFailureRetriesAcceptedBackendStepWithoutResimulation` / `EpidemicRuntimePhysicsTests`.
- `M3C-PHYS-001` (EngineRuntime/Physics): Finite large ray directions are normalized with sufficient range so validation cannot turn a valid non-zero direction into zero through float overflow. Regression: `TestRaycastLargeFiniteDirectionNormalizesSafely` / `EpidemicRuntimePhysicsTests`.
- `G3-NAV-001` (EngineRuntime/Navigation): Reference path byte-budget admission is based on the actual prepared path shape, including cost-provider midpoint expansion. Regression: `TestReferenceBudgetUsesActualPreparedShape` / `EpidemicRuntimeNavigationTests`.
- `G3-NAV-002` (EngineRuntime/Navigation): A successful backend/reference result remains owned across local budget/allocation failure and retry does not call the backend again. Regression: `TestBackendResultOwnershipSurvivesBudgetAndAllocationFault` / `EpidemicRuntimeNavigationTests`.
- `G3-NAV-003` (EngineRuntime/Navigation): Released/expired query purge occurs only after all fallible purge/query work-list preparation succeeds. Regression: `TestPurgeOccursOnlyAfterAllFallibleWorkListPreparation` / `EpidemicRuntimeNavigationTests`.
