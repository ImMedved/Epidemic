# EngineBase/Core local freeze audit

Scope: Goal 2.4. Module: `EngineBase/Core`, targets `EpidemicCore` and `EpidemicCoreUnitTests`.

Status: `LOCAL_READY` after strict MSVC Debug/Release execution on Windows 11.

## Reviewed state and ownership

Core owns the application lifecycle, module dependency plan, service composition container, event subscriptions and queued events, task scheduling state, main-thread dispatch queue, frame phases, frame index and stop request. Service and module identity maps are the authoritative records; derived execution plans and handler snapshots never become second owners. Core depends only on Foundation and Diagnostics and exposes external work through service, module, task and event interfaces.

## Public contracts

Every public callable is represented by the generated signature inventory. Mutators reject empty callbacks/tasks, duplicate services/modules, missing or cyclic dependencies, invalid/foreign task handles, invalid frame phases and mutation after sealing/lifecycle start. Successful service, event, task and lifecycle calls publish their state exactly once. Overloads remain separate inventory rows and are covered by the same explicit contract families.

## Lifecycle and no-op semantics

The application state machine is `Constructed -> Bootstrapped -> Initialized -> Running -> ShutDown`. Calls from every forbidden state are rejected; successful shutdown is idempotent. ServiceContainer sealing is irreversible. Event unsubscribe of an unknown token and repeated task cancellation are controlled no-ops. A stop request before `TickModules` prevents an extra gameplay tick.

## Failure atomicity

Module bootstrap/initialization failure unwinds already-started modules in reverse dependency order. Scheduler, dispatcher and event callback failures preserve queue/container invariants and propagate the first failure at the documented boundary. Service bundle registration is all-or-nothing. EventBus token allocation enters an exhausted zero sentinel after the final `uint64_t` token, and application frame counting saturates instead of reusing frame zero.

## Persistence

Core owns no durable snapshot/restore format. All application, task, event and module-registry state is process-local and transient, so the five persistence admission criteria are not applicable.

## Defect regressions

- Reentrant shutdown during `Running` is rejected.
- Module ordering is canonical and independent of registration order; missing/cyclic dependencies and partial initialization are covered.
- Task cancellation, foreign handles, worker self-wait/self-shutdown and grouped failure consumption are covered.
- Dispatcher reentrancy moves newly posted work to the next drain wave without loss.
- Stop requests prevent a final unwanted module tick.
- Event subscription and frame counters no longer wrap into live identities.

## Windows verification

MSVC `19.50.35729`, C++20, `/W4 /WX`: Debug and Release build passed. `EpidemicCoreUnitTests` passed in the complete Base `10/10` CTest profiles. The architecture, dossier, callable coverage, public surface and LOCAL_READY validators are part of the same qualification run.

## Corrective patch 2026-09-18

A second defect review found additional failure-boundary gaps after the original LOCAL_READY run. The patch closes them without changing Core ownership.

`CORE-009`: `ModuleRegistry::Register` could publish one id/name index before a later allocation failed. Registration now stages both indexes and reserves both live vectors before the no-fail commit. `TestModuleRegistrationAllocationFailureAtomicity` sweeps the observed allocation boundaries and verifies that retry sees no ghost identity.

`CORE-010`: `EventBus::SubscribeImpl` used `operator[]`, so allocation failure after bucket insertion could leave an empty observable bucket. Missing buckets are now built off-state and inserted only after the handler record exists.

`CORE-011`: `DrainQueued` removed the authoritative queued event before copying potentially throwing `std::function` subscribers. Subscriber copies are now completed while the event remains at queue front; `TestQueuedEventCopyFailureIsRetryable` proves that a failed copy leaves the event available for one later delivery.

`CORE-012`: grouped task scheduling bound a `TaskGroup` to a scheduler before the queue insertion succeeded. Queue publication now precedes group binding/count increment. `TestTaskGroupScheduleFailureDoesNotBindGroup` verifies a group remains reusable after injected allocation failure.

`CORE-013`: scheduler lifetime identity used a raw `SharedState*`, permitting allocator address reuse to make a stale handle/group look like it belonged to a new scheduler. Handle and group identity now use a weak lifetime token. `TestExpiredSchedulerIdentityIsRejected` covers both stale handle and stale group behavior.

`CORE-014`: a constructor failure after one or more `jthread` workers started had no explicit partial-construction cleanup. Constructor failure now requests stop, wakes and joins started workers, and publishes the global worker count only after construction completes. `TestSchedulerConstructorFailureCleansStartedWorkers` sweeps constructor allocation failures.

`CORE-015`: `ModuleRegistry::ShutdownAll` marked the registry terminal even when a module cleanup failed. Successful per-module cleanup is now remembered, failed modules remain retryable, and terminal `ShutDown` is published only when all required cleanup succeeds. The module-registry regression verifies successful modules are skipped and the failed module is retried.

`CORE-016`: Support required one atomic commit boundary for a service bundle plus its frame-handler batch. Core now provides strong-guarantee batch handler registration and an atomic service pre-commit hook. Duplicate types in one service bundle are rejected before candidate publication. These contracts are covered by `TestAtomicFrameHandlerRegistration` and `TestServiceContainerContracts`.

The exact public callable inventory increased from 4306 to 4308 because the two composition primitives are public Core contracts. Both rows have reviewed precondition, success, failure and no-op coverage entries plus explicit contract/test anchors.
