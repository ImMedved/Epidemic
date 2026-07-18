# Simulation

## Purpose

`Simulation` owns generic job scheduling, attention scores, world-memory records and opaque effect buffering. It does not act as an NPC brain and does not mutate authoritative World state directly.

## Public Contracts

- `simulation_types.h`: generic job ids, generation handles, lanes, immutable step inputs, generic proposal payloads, proposal batches, source revision, memory lifetime/observation, abstract facts with revisions, scheduled tasks, memory records and effects.
- `simulation_runtime.h`: runtime/scheduler split, relevance policy, executable simulation job, commit target, attention, world memory, abstract fact store, proposal queue, scheduled task store, effect buffer contracts and `CreateSimulationServices()`.

## Rules

- Jobs require valid zones and non-zero work units.
- Job handles carry generations; stale handle cancellation is rejected.
- Jobs execute through `ISimulationJob::ExecuteStep(RuntimeBudget)`; the scheduler does not implement work by only mutating a counter.
- `Tick()` advances jobs in deterministic job-id order under job/work-unit budgets.
- Cancelled, completed and failed jobs do not consume scheduler work.
- Failed, cancelled and stale jobs do not publish proposal batches.
- World memory queries return deterministic memory-id order.
- World memory uses `SimulationTime`, an alias over the foundation game time point, and does not depend on the `Time` major.
- `ExpireOldEvents(now, max_events)` applies an item budget; `max_events == 0` means unlimited.
- Persistent and already expired memory records are not expired by the runtime.
- Memory lifetime and observation are independent; `MemoryLifetime::Persistent` records are not expired by TTL and observation can be `Unobserved`, `Observed` or `PlayerAffected`.
- Effects are opaque and remain in submission order until composition code resolves them.
- Proposal payloads are generic: domain, kind, target, schema id/version and opaque bytes. Simulation does not understand gameplay payloads.
- Proposal batches commit through `ISimulationCommitTarget`; commit failure such as `simulation.stale_revision` applies nothing and leaves the pending batch intact.
- Scheduled tasks support schedule, cancel, query due and budgeted execution. They are infrastructure timers, not persistent jobs for every far-away item.

## Testing Strategy

The `Simulation` tests cover real `ExecuteStep`, partial budgeted work, deterministic scheduler order, cancellation, stale handle rejection, proposal publication, stale revision commit rejection, proposal commit atomicity, failed/cancelled job isolation, memory TTL/lifetime/observation independence, abstract fact revision, scheduled task budget, service factory shape, effect order/clear and validation failures.
