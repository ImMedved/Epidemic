# Simulation

## Purpose

`Simulation` owns generic job scheduling, attention scores, world-memory records and opaque effect buffering. It does not act as an NPC brain and does not mutate authoritative World state directly.

## Public Contracts

- `simulation_types.h`: generic job ids, generation handles, lanes, immutable step inputs, proposal batches, source revision, memory lifetime/observation, abstract facts, scheduled tasks, memory records and effects.
- `simulation_runtime.h`: scheduler, relevance policy, simulation job, commit target, attention, world memory, abstract fact store, effect buffer contracts and `CreateSimulationServices()`.

## Rules

- Jobs require valid zones and non-zero work units.
- Job handles carry generations; stale handle cancellation is rejected.
- `Tick()` advances jobs in deterministic job-id order under job/work-unit budgets.
- Cancelled, completed and failed jobs do not consume scheduler work.
- World memory queries return deterministic memory-id order.
- World memory uses `SimulationTime`, an alias over the foundation game time point, and does not depend on the `Time` major.
- `ExpireOldEvents(now, max_events)` applies an item budget; `max_events == 0` means unlimited.
- Persistent and already expired memory records are not expired by the runtime.
- `MemoryLifetime::Persistent` records are not expired even if their event state is temporary.
- Effects are opaque and remain in submission order until composition code resolves them.
- Proposal batches and commit targets are contracts only; Simulation does not apply authoritative World mutations.

## Testing Strategy

The `Simulation` tests cover job completion, generation handles, partial budgeted work, deterministic scheduler order, cancellation, memory TTL/lifetime/expiration budget, proposal/task/fact value contracts, service factory shape, effect order/clear and validation failures.
