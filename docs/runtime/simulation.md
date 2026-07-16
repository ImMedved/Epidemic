# Simulation

## Purpose

`Simulation` owns generic job scheduling, attention scores, world-memory records and opaque effect buffering. It does not act as an NPC brain and does not mutate authoritative World state directly.

## Public Contracts

- `simulation_types.h`: generic job ids, memory ids, zone/job states, budgets, memory records and effects.
- `simulation_runtime.h`: scheduler, attention, world memory and effect buffer contracts.

## Rules

- Jobs require valid zones and non-zero work units.
- `Tick()` advances jobs in deterministic job-id order under job/work-unit budgets.
- Cancelled, completed and failed jobs do not consume scheduler work.
- World memory queries return deterministic memory-id order.
- World memory uses `SimulationTime`, an alias over the foundation game time point, and does not depend on the `Time` major.
- `ExpireOldEvents(now, max_events)` applies an item budget; `max_events == 0` means unlimited.
- Persistent and already expired memory records are not expired by the runtime.
- Effects are opaque and remain in submission order until composition code resolves them.

## Testing Strategy

The `Simulation` tests cover job completion, partial budgeted work, deterministic scheduler order, cancellation, memory TTL/expiration budget, effect order/clear and validation failures.
