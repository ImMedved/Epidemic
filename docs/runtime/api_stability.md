# Runtime API Stability

## Stable Public Contracts

Public headers under `EngineRuntime/*/include/Epidemic/Runtime/**` are source-level contracts for the runtime majors.

## Frozen In This Pass

- Typed time values and `TimeServices`.
- World object revision and deterministic queries.
- Streaming progress revision.
- Physics fixed-step result.
- Navigation path result revision and deterministic budgets.
- Animation pose snapshots.
- Audio emitter snapshots.
- Simulation deterministic scheduler and budgeted memory expiration.
- Support registration of `TimeServices`.

## Explicitly Unstable

Placement is still represented by a compact record, not the final tagged variant. Remaining Support registrations for World, Streaming, Physics, Navigation, Animation, Audio and Simulation require service-bundle factories before they should be considered frozen.

## Compatibility Promise

The project currently promises source compatibility only. ABI stability is not declared.

## Change Rules After Freeze

Breaking public API changes must include migration notes, updated docs, and focused tests for old/new state transition behavior.
