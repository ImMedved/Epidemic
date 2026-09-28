# EngineRuntime/Resources local freeze audit

Audit date: 2026-09-21.

Status: `LOCAL_READY candidate` for Goal 3 after Block C source/test review. Final admission requires the repository MSVC Debug/Release qualification and the serial Runtime evidence pass.

## Scope and ownership

`EpidemicRuntimeResources` owns runtime resource slots, consumer acquisitions, load/wait queues, dependency leases, the dependency graph, resident-byte accounting, resource generations and the monotonic acquisition-ID allocator. `ResourceLoaderRegistry` owns only non-owning loader pointers keyed by `ResourceType`; registered loaders must outlive the registry/manager that uses them.

The module does not own Assets metadata, Scene/World state, persistence, renderer/audio objects, threads, or Framework policy. Its only external execution boundary is `IResourceLoader::Load()`.

## Public mutation and query contract

`RequestLease()` validates ID/type, reserves a monotonic acquisition ID, publishes queue/slot/acquisition state atomically and commits the ID only after local publication succeeds. Multiple consumers of one resource receive independent `ResourceAcquisitionId` values while sharing the same generation-bearing `ResourceHandle`.

`Release()` requires a valid current lease and removes exactly that acquisition. Double release reports `resource.acquisition_underflow`; stale-generation release reports `resource.stale_handle`. `Evict()` requires a valid unreferenced slot, performs retryable dependency cleanup, advances generation only after cleanup succeeds, then removes payload/dependency state. `EvictUnreferenced()` applies the same generation/cleanup rules to eligible slots and leaves in-flight work alone.

`ProcessPendingLoads()` processes a deterministic queue prefix constrained by item/time/byte budgets. Byte budget is soft for the current unit: a started load may complete, after which no further work begins once the consumed byte budget reaches the limit. Query methods reject or hide invalid/stale handles according to their return shape and never transfer resource ownership.

## Loader registry lifecycle

`RegisterLoader()` rejects invalid types, duplicates and registration after `Freeze()`. The registry is non-owning. `Freeze()` is monotonic and idempotent. `FindLoader()`/`HasLoader()` expose the registered pointer without hidden mutation.

There is no module-wide shutdown API. Loader-registry freeze and per-resource eviction are the applicable lifecycle boundaries.

## State, identities and derived ownership

Authoritative per-resource state is the `ResourceSlot`: resource/type identity, generation, state, reference count, active acquisition set, payload, resident bytes, dependency leases and optional pending load artifact. The load queue, waiting queue, dependency graph, loading-resource set and resident-byte aggregate are derived/coordination state that must stay consistent with slots.

`ResourceAcquisitionId` is monotonic and committed last. Exhaustion is rejected before mutation. Resource generation is advanced only on completed eviction. Generation zero and `UINT32_MAX` cannot be advanced, preventing stale-handle ABA. Reference count and resident/statistics counters are overflow-checked at their publication boundaries.

## External loader boundary and accepted-once semantics

`LoadSlot()` inserts loading ownership before calling the loader. Only the external `IResourceLoader::Load()` callback is inside the callback exception boundary. A successful `ResourceLoadArtifact` is moved into `slot.pending_artifact` before fallible Runtime-local dependency staging/finalization. Retrying a local finalization failure therefore resumes from the pending artifact and does not call the loader again.

Returned artifacts must match resource ID/type and carry a non-null payload. Loader Result failure and exceptions become controlled failures. Required/optional dependency semantics are validated before ready publication.

## G3-RES closure regressions

### G3-RES-001: atomic dependency replacement

`ResourceDependencyGraph::ReplaceDependencies()` stages a complete replacement before touching live graph state. Existing vectors are committed with allocation-free swap. `TestDependencyReplacementStrongCommit` injects replacement failure and proves the old set remains authoritative until a successful retry.

### G3-RES-002: waiting owner publication

A selected load-queue job remains owned by the load queue until `waiting_queue_.Publish()` succeeds. `TestWaitingOwnerPublicationFailureKeepsLoadOwner` injects waiting-owner publication failure and proves retry completes without reloading the already accepted loader artifact.

### G3-RES-003: loader/finalization staging

A successful loader artifact is stored in `pending_artifact` before Runtime-local finalization. `TestLocalFinalizationFailureDoesNotReloadArtifact` injects dependency-graph publication failure after loader success and proves the retry does not call the loader a second time.

### G3-RES-004: selected-job ownership

`ProcessPendingLoads()` does not pop a selected job before the next durable owner exists. `TestSelectedJobOwnershipSurvivesLoadingPublicationFailure` injects failure while publishing loading ownership and proves the queued job remains retryable and the loader has not executed.

## Ownership and failure matrix

The module tests cover independent consumer acquisitions, shared dependency ownership, double/stale release, acquisition/generation/reference exhaustion, stale handles after eviction, loader Result/exception failure, artifact ID/type/null-payload rejection, required/optional dependencies, deep iterative cycle search, dependency-publication rollback, retryable failed dependency release, item/time/byte budgets, resident accounting overflow, eviction prefix retry and cancellation of unreferenced queued/loading/waiting work without phantom `Ready` publication.

All fault injection in Resources is implementation-local and names semantic publication boundaries. No process-global allocator override is used.

## Persistence and threading

Resources exposes no snapshot/restore API. Slots, acquisitions, queues, generations and pending artifacts are transient runtime state and are not declared persistent continuity state in Goal 3. Persistence criteria are therefore not applicable to this module.

Public headers state that mutation occurs on the runtime thread and concurrent read/write is not supported unless explicitly documented. The implementation does not create worker threads.

## Generated-review decisions for serial integration

The six generated stale-identity candidates are not all equivalent. `Release(ResourceLease)`, `Evict(ResourceId)` and `EvictUnreferenced()` have real stale-generation/acquisition obligations and are covered by `TestResourceLeasePreventsCrossConsumerDoubleRelease`, `TestUnknownAndStaleHandles`, `TestGenerationExhaustionPreventsAba` and `TestEvictingRetryKeepsCompletedPrefix`. `RegisterLoader`, `Freeze` and `SetMemoryBudgetBytes` do not consume or recycle a reusable identity; their stale-identity records are scanner false positives. `Freeze()` is, however, a real lifecycle candidate and is covered by `TestLoaderRegistryFreezeAndEmptyQueueBoundaries`.

The 36 generated mutator obligation records are applicable review work and can be moved to `REVIEWED` after serial integration using the exact per-callable rationale in `block_c_resources_assets_evidence.md`. No shared generated JSON is edited by Block C.

## Verification

Block C runs the Resources regression suite together with Assets in an isolated module harness, with warnings treated as errors. The suite adds `TestTimeBudgetStopsFurtherJobs` so item/time/byte budget coverage is explicit. GCC 14 and Clang 17 Debug/Release pass, GCC 14 ASan+UBSan and a GCC libstdc++ debug-iterator/assertion build pass, and all 11 Resources public headers pass standalone syntax checks under both GCC and Clang. The repository top-level build remains Windows-only, so this Linux task does not claim the required MSVC qualification.
