# Physics

## Purpose

`Physics` owns physics body proxies, collision shapes, deterministic queries, fixed-step progression and contact events. It does not mutate World directly.

## Public Contracts

- `physics_types.h`: body ids, shape ids, body states, queries, contacts and `PhysicsStepResult`.
- `physics_scene.h`: shape registry, body scene and fixed-step contracts.
- `physics_query.h`: raycast and overlap query contract.
- `physics_event_buffer.h`: contact event publication boundary.

## Rules

- Bodies require valid owner ids, transform node ids and registered shapes.
- Static and disabled bodies reject impulses with `physics.impulse_not_allowed`.
- `StepFixed(GameDuration)` accepts only positive fixed deltas and returns a versioned step result.
- Contact events are published through the event buffer; Physics does not call World mutation APIs.
- Query results must remain deterministic for equivalent body/query inputs.

## Forbidden Dependencies

`Physics` must not own World state, destroy World objects or include private World implementation headers.

## Testing Strategy

The `Physics` tests cover body lifecycle, invalid body errors, impulse state transitions, fixed-step revision, deterministic queries and event buffer clearing.
