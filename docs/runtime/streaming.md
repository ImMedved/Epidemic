# Streaming

## Purpose

`Streaming` coordinates chunk residency requests through progressive, budgeted state transitions. It does not own World, Resources or Persistence data; it talks to those systems through public source/controller contracts.

## Public Contracts

- `streaming_types.h`: request ids, priority classes, states, budgets and versioned progress.
- `streaming_runtime.h`: request/cancel/tick/progress runtime contract.
- `streaming_priority_resolver.h`: priority policy contract.
- `streaming_sources.h`: world, persistence and resource source contracts.
- `residency_controller.h`: activation/deactivation/unload sink contract.

## Rules

- Invalid chunk ids are rejected with `streaming.invalid_chunk`.
- Duplicate live requests for a chunk reuse the existing request id and may only raise priority.
- `Tick()` processes requests in deterministic priority/request-id order.
- `StreamingBudget::max_requests` limits how many requests advance per tick.
- `StreamingProgress::revision` starts at `1` and increments only when state/progress changes.
- Terminal or already-deactivating cancellation does not create extra revision churn.
- Source/controller failures move only the affected request to `Failed`.

## Forbidden Dependencies

`Streaming` must not own authoritative World, Resources or Persistence state and must not call private implementation headers from those majors.

## Testing Strategy

The `Streaming` tests cover request/cancel flow, priority ordering, request-count budget limits, failure reporting, mock activation, and progress revision stability.
