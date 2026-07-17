# Streaming

## Purpose

`Streaming` coordinates residency demand through progressive, budgeted state transitions. It does not own World, Resources or Persistence data; it talks to those systems through public source/controller contracts and explicit backend ports.

## Public Contracts

- `streaming_types.h`: request ids, demand ids, generation handles, target variants, priority classes, states, budgets and versioned progress.
- `streaming_types.h`: demand count, progressive load plan cursor and statistics.
- `streaming_runtime.h`: runtime/query split, demand request/release, compatibility request/cancel, tick/progress/statistics contracts and `CreateStreamingServices()`.
- `streaming_priority_resolver.h`: priority policy contract.
- `streaming_sources.h`: world, persistence and resource source contracts.
- `streaming_sources.h`: backend-oriented data source, commit target and priority provider contracts.
- `residency_controller.h`: activation/deactivation/unload sink contract.

## Rules

- Invalid chunk ids are rejected with `streaming.invalid_chunk`.
- The public target set is Region, Chunk, Object, ResourceGroup and Asset. The reference runtime executes chunk targets; unsupported targets fail with `streaming.unsupported_target`.
- `Request(target, priority)` returns a `StreamingDemandHandle`. Each handle owns one consumer demand on a target.
- Duplicate live requests for a chunk reuse the existing request record, add a new demand handle and may raise max priority.
- `ReleaseDemand(handle)` releases only that consumer. Loading is not cancelled while another active demand remains.
- Releasing the last demand cancels pre-resident work or starts unload for resident/active content.
- `Tick()` processes requests in deterministic priority/request-id order.
- `StreamingBudget::max_requests` limits how many requests advance per tick.
- `StreamingBudget::max_bytes` is the reference implementation's progressive-step budget.
- `StreamingProgress::revision` starts at `1` and increments only when state/progress changes.
- Terminal or already-deactivating cancellation does not create extra revision churn.
- Source/controller failures move only the affected request to `Failed`.
- `StreamingRequestHandle` and `StreamingDemandHandle` carry generations; stale handles are rejected.
- `IStreamingDataSource::BuildLoadPlan()` creates the progressive plan. The runtime executes budgeted plan steps and commits only after the plan completes.
- Failure or cancellation before commit rolls back the temporary plan through `IStreamingCommitTarget::Rollback()`.
- Cancellation states are explicit: requested/queued work becomes `Cancelled`; loading or loaded-before-commit work rolls back and becomes `Cancelled`; resident/active work goes through deactivation/unload before `Unloaded`.
- A never-activated request is not routed through deactivation.
- Terminal records are retained only as bounded history and may be cleaned after demand release.
- Statistics count requested, cancelled, committed, rolled-back and failed transitions.

## Forbidden Dependencies

`Streaming` must not own authoritative World, Resources or Persistence state and must not call private implementation headers from those majors.

## Testing Strategy

The `Streaming` tests cover two-consumer demand ownership, partial release, last-release cancellation/unload, cancel before and during load, rollback on failed progressive step, request-count and step budgets, unsupported target variants, stale demand generations, cleanup, injected dependencies, statistics and service factory shape.
