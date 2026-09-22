# Goal 3.9 Simulation local freeze audit

Baseline: `Epidemic 19-09-2026-2`.
Scope: `EngineRuntime/Simulation` only. Cross-major orchestration and whole-engine persistence/determinism remain later Goal 3/Goals 5-9 work.

## Confirmed fixes

### G3-SIM-001: durable ownership of successful ExecuteStep results

A successful `ISimulationJob::ExecuteStep()` is now accepted exactly once into private `JobRecord::pending_step_result`. The move into this owner is statically required to be non-throwing. Runtime-local validation, proposal copies/reserves and publication happen from this retained result. If local staging fails, the next `Tick()` resumes publication and does not call `ExecuteStep()` again for the same logical step.

The grant associated with the accepted callback is retained in `pending_step_granted_work_units`, so a retry validates the result against the budget under which the callback actually ran. Successful cancellation explicitly releases pending worker/main-thread results.

Permanent regression: a named internal tick-result staging seam fails immediately after Runtime has accepted a successful callback result. First `Tick()` returns `simulation.allocation_failed` with one callback invocation and unchanged authoritative job/proposal state. Retry completes the same retained step with the callback count still equal to one.

### G3-SIM-002: no allocation-dependent post-commit pruning/result bookkeeping

`PruneTerminalJobs()` and `PruneExpiredMemoryEvents()` no longer build temporary vectors. Both perform deterministic no-allocation scans and erase only after selection of the next lowest identity.

`Tick()`, `ProcessMainThreadCommits()` and scheduled execution reserve result buffers before mutation. Existing callback failure payloads are moved into reserved result storage instead of copied. Scheduled failure storage is reserved before task mutation. Proposal publication reserves both authoritative and returned batch storage before commit.

Permanent regressions cover callback failure bookkeeping, successful no-proposal post-commit housekeeping, named tick work-list preparation failure, and named memory-expiration work-list preparation failure. A fault either happens before mutation and preserves pre-state, or the post-commit path contains no Runtime-owned allocation at the audited boundary.

## Additional defects found by the 3.9 audit

### G3-SIM-AUDIT-001: shutdown admitted new work after cleanup had started

After a failed first `Shutdown()`, the runtime remained `ShuttingDown`, but `Tick()`, `ProcessMainThreadCommits()` and `Schedule()` could still accept work. They now reject new work once shutdown has entered its retryable cleanup state. `ExecuteDueWithinBudget()` performs no activation in that state. Proposal `Publish()` is also rejected, while explicit cleanup operations such as cancellation, `CommitNext()` and `DiscardAll()` remain available.

Regression covers a scheduled job whose cancel callback throws during shutdown, verifies that no due work activates, verifies scheduling/Tick/main-thread work rejection, then retries shutdown successfully.

### G3-SIM-AUDIT-002: SetAttention allocation escaped its Result contract

First insertion into the object-attention map could throw `std::bad_alloc` directly. Publication is now contained and returns `simulation.allocation_failed`; failed insertion leaves the previous attention state unchanged.

### G3-SIM-AUDIT-003: proposal Publish allocation escaped its Result contract

`ISimulationProposalQueue::Publish()` validated the complete batch but performed the vector copy without containment. Allocation failure is now returned as `simulation.allocation_failed`, with no partial queue entry.

## Block D allocator-harness closure

The Simulation test executable no longer replaces process-global `operator new/new[]`. The old allocator-index sweep has been replaced by named internal seams at the semantic boundaries required by Goal 3: tick work-list preparation, accepted-step/result staging, object-attention publication, proposal-batch publication and memory-expiration work-list preparation. Each seam is one-shot and the regression checks both exact pre-state preservation and retry behavior where retry is part of the contract.

Portable Block D verification compiles and executes the Simulation suite as C++20 with warnings-as-errors in GCC Debug/Release and Clang Debug/Release. Clang AddressSanitizer plus UndefinedBehaviorSanitizer and standalone public-header consumers pass. Repository-wide CMake still intentionally rejects this Linux environment at `EngineBase/Platform`, so official MSVC Debug/Release qualification remains an integration requirement rather than being claimed by this audit.

## Checklist evidence

Jobs/scheduler: valid/null/invalid submission, monotonic ID and generation exhaustion, stale handles, Pending/Scheduled/PartiallyComplete/WaitingForMainThread/Completed/Failed/Cancelled paths, Result failure and exception containment, budget deferral, deterministic job order, and accepted-result retry ownership are covered.

Cancellation/scheduling: cancel Result failure/exception retains ownership; successful cancellation removes the associated schedule and pending step payloads; task ID exhaustion is non-wrapping and atomic; equal-due tasks are ordered by task identity; invalid due records are cleaned without phantom active schedules; budgeted activation defers the remainder.

World memory/attention/facts: memory ID exhaustion, negative/zero TTL semantics, expiration overflow, persistent retention, capacity limits, deterministic expiration order, finite inclusive attention range `[0,1]`, relevance exception/invalid-enum containment, fact validation, revision exhaustion, and deterministic publication revision order are covered.

Proposal queue/commit: legal source states and complete schema validation precede publication; allocation failure leaves the queue unchanged; commit Result failure/exception retains the front batch; success removes exactly one batch; terminal jobs remain retained while their proposals are pending; explicit discard releases that ownership. Local `Shutdown()` does not silently discard pending proposals because `ShutdownProposalPolicy` belongs to Runtime Support composition.

Shutdown: once cleanup starts, new work is blocked; cancellation remains best-effort and retryable; successful completion is idempotent; pending proposal ownership is preserved for the composition-level policy.

Local persistence applicability: Simulation exposes no local snapshot/restore API in this baseline. Whole-engine persistence and ordered restore are intentionally deferred to the later system goals rather than inventing a module-local persistence surface in Goal 3.9.
