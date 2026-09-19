# EngineRuntime/Streaming local freeze audit

Scope: Goal 3.6. Module: `EngineRuntime/Streaming`, target `EpidemicRuntimeStreamingTests`.

Status: module-local contracts, regressions, failure boundaries and cleanup ownership are complete. Publication into the shared `LOCAL_READY` ledger and regeneration of global API/surface/dossier manifests are intentionally left to the integrated Goal 3 tree so parallel module deltas do not overwrite one another.

## Reviewed state and ownership

Streaming owns request records, demand ownership, request/demand generations, completion sequence, request revisions, progressive-plan cursor/accounting, chunk-to-current-request mapping, observable chunk state, terminal history and diagnostic statistics. External world, persistence, resources, residency and commit state stay behind neutral dependency ports.

A request can own one private accepted `pending_step_result`. It contains the plan cursor, budget identity used for the accepted callback and the returned `StreamingStepResult`. This is retry/reconciliation state, not public progress and not persistent gameplay state. It is cleared only after local accounting commit or terminal request commit.

## G3-STR-001

The old `Tick()` wrapped both `BuildWorkList()` and result-capacity preparation in `catch (...)`, incremented `statistics.failed` and returned an empty successful-looking tick result. The same broad catch around `AdvanceRequest()` could convert Runtime-local `std::bad_alloc` into `streaming.runtime_exception` and drive request rollback/failure semantics.

The fix removes semantic conversion for Runtime-owned preparation and explicitly rethrows `std::bad_alloc` from request processing. Work-list construction and `StreamingTickResult::failures` capacity are completed before any request mutation. Regression fault injection at the first and second Tick allocations proves exact progress/statistics pre-state preservation. External callback exceptions remain contained inside their specific port boundaries.

## G3-STR-002

A successful `IStreamingDataSource::ExecuteStep()` previously remained only in a stack local until byte checks, revision reservation and cursor/accounting commit completed. A local failure in that suffix left the cursor unchanged, so retry called `ExecuteStep()` again for an already accepted logical unit.

The fix publishes the successful result immediately into no-throw fixed-size pending state. Retry consumes that pending result without a second callback. The regression uses revision exhaustion as a deterministic local commit failure after callback success, then restores revision capacity and proves the retry reaches `Loaded` with the same processed bytes and `execute_count == 1`. The same pending path protects the neutral world/persistence/resource step path.

The public data-source contract now states that failure/exception means the step unit was not accepted, while success is accepted exactly once by Runtime. `completed=false` still permits another callback for the same cursor after the previous partial result has been accounted.

## G3-STR-003 discovered during 3.6 audit

Problem: `ValidatePlan()` accepted a `Commit` step in the middle of a progressive plan, and `ExecutePlanStep()` invoked `IStreamingCommitTarget::Commit()` as soon as that step completed. External commit could therefore happen before later plan steps.

Cause: plan validation checked only enum domain, initial cursor and pre-processed bytes, while commit ordering was implicit in the default plan.

Fix: if a plan contains `Commit`, it must be unique and final. Plans without a Commit step remain valid. A regression covers both early and duplicate Commit and verifies rejection before request publication.

## G3-STR-004 discovered during 3.6 audit

Problem: when terminal history cleanup had removed the old chunk mapping but retained `chunk_states_`, publication of a new request first overwrote the existing chunk state with `Requested`. If the subsequent `chunk_to_request_` insertion failed allocation, rollback removed the new request but did not restore the previous chunk state.

Cause: publication rollback tracked only whether a state/mapping key was newly inserted, not whether an existing value had been overwritten before a later fallible operation.

Fix: request publication stages the previous chunk state/mapping values and restores them on any publication exception. A bounded global-allocation sweep recreates the terminal-history scenario and verifies record count, chunk state and identity allocators remain exactly at pre-call values for every injected failure position.

## Demand, identity and ordering audit

Multiple demands for one target share the same active request while keeping distinct demand handles. Stale demand/request generations are rejected. Request ID, demand ID, request generation, demand generation, revision and completion-sequence exhaustion paths are covered, including last-value publication and subsequent exhaustion. Publication failures do not consume request/demand identities.

Equal-priority processing is deterministic by request ID. Priority provider and priority resolver enum/exception boundaries are validated. During irreversible `Unloading`, at most one waiting successor exists; all new demands share it, last waiting-demand release cancels it, and history cleanup preserves a newer successor mapping.

## Loading, budget and commit audit

Requested/Queued/Loading/Loaded/Activating/Resident/Active and deactivation/unload transitions are covered. Incomplete steps retain cursor; zero-byte incomplete steps are not auto-completed. Partial steps receive only the remaining byte budget. Actual byte overruns and request/tick counter overflow are rejected before commit/accounting. Diagnostic counters use the documented saturating behavior.

External commit is attempted only for a completed final Commit step and is guarded by `commit_completed`. Rollback is preflighted for revision/completion-sequence capacity before the external call. Failed rollback enters explicit retry ownership; a successful rollback is not repeated.

## Dependency and cleanup boundaries

Throwing fakes cover data-source plan build/step execution, commit/rollback, priority provider/resolver, world resolution, persistence preparation, resource preparation/release and residency activate/deactivate/unload. Provider failures remain controlled semantic failures.

Unload records durable cleanup prefix flags. If resource release succeeds and residency unload fails/throws, retry does not repeat resource release. Shutdown becomes terminal for admission as soon as it starts, drains demands in deterministic order, preserves failed cleanup ownership and completes only after live request/temp/resident ownership is gone.

## Verification

The Streaming suite is compiled and executed directly as C++20 with warnings-as-errors because the repository-wide CMake configuration intentionally rejects non-Windows `EngineBase/Platform` before Runtime tests in this task container. Portable verification covers GCC Debug/Release and Clang Debug/Release. Clang AddressSanitizer plus UndefinedBehaviorSanitizer and standalone public-header consumers are also run for this delta.

The official Windows MSVC Debug/Release, exact CTest manifests, shared `LOCAL_READY` ledger and global generated freeze manifests remain the integration-step qualification after the parallel Goal 3 module deltas are merged.
