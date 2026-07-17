# Streaming

## Purpose

`Streaming` coordinates chunk residency requests through progressive, budgeted state transitions. It does not own World, Resources or Persistence data; it talks to those systems through public source/controller contracts.

## Public Contracts

- `streaming_types.h`: request ids, priority classes, states, budgets and versioned progress.
- `streaming_types.h`: generation handles, non-chunk target variants, demand count, cancellation token, progressive load plan and statistics.
- `streaming_runtime.h`: runtime/query split, request/cancel/tick/progress/statistics contracts and `CreateStreamingServices()`.
- `streaming_priority_resolver.h`: priority policy contract.
- `streaming_sources.h`: world, persistence and resource source contracts.
- `streaming_sources.h`: backend-oriented data source, commit target and priority provider contracts.
- `residency_controller.h`: activation/deactivation/unload sink contract.

## Rules

- Invalid chunk ids are rejected with `streaming.invalid_chunk`.
- Duplicate live requests for a chunk reuse the existing request id and may only raise priority.
- `Tick()` processes requests in deterministic priority/request-id order.
- `StreamingBudget::max_requests` limits how many requests advance per tick.
- `StreamingProgress::revision` starts at `1` and increments only when state/progress changes.
- Terminal or already-deactivating cancellation does not create extra revision churn.
- Source/controller failures move only the affected request to `Failed`.
- `StreamingRequestHandle` carries a generation; stale handle cancellation is rejected.
- Reference runtime executes chunk targets; region/asset targets are contract-visible and intentionally rejected by the fake implementation until real backends exist.
- Statistics count requested, cancelled, committed, rolled-back and failed transitions.

## Forbidden Dependencies

`Streaming` must not own authoritative World, Resources or Persistence state and must not call private implementation headers from those majors.

## Testing Strategy

The `Streaming` tests cover request/cancel flow, target handles, stale generation rejection, priority ordering, request-count budget limits, failure reporting, mock activation, statistics, service factory shape and progress revision stability.
