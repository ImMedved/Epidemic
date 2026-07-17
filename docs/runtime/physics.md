# Physics

## Purpose

`Physics` owns physics body proxies, collision shapes, deterministic queries, fixed-step progression and contact events. It does not mutate World directly.

## Public Contracts

- `physics_types.h`: generation body handles, opaque backend handles, shape ids, body type/lifecycle/activity/dirty flags, queries, contacts, body snapshots and `PhysicsStepResult`.
- `physics_scene.h`: shape registry, body scene, backend, transform source/sink, fixed-step contracts and `PhysicsServices`.
- `physics_query.h`: raycast and overlap query contract.
- `physics_event_buffer.h`: contact event publication boundary.

## Rules

- Bodies require valid owner ids, transform node ids and registered shapes.
- Bodies are addressed through `PhysicsBodyHandle`; unknown and stale handles return errors instead of synthetic destroyed state.
- `GetBodySnapshot(PhysicsBodyHandle)` is the stable state read API.
- Static and disabled bodies reject impulses with `physics.impulse_not_allowed`.
- `StepFixed(GameDuration)` accepts only positive fixed deltas and returns a versioned step result. `Tick(GameDuration)` owns accumulator, max-substep and dropped-time accounting.
- Contact events use explicit `Begin`, `Persist` and `End` states. `ApplyImpulse()` does not create fake contact events.
- Contact events are published through the event buffer; Physics does not call World or Scene mutation APIs.
- Query results must remain deterministic for equivalent body/query inputs.
- `IPhysicsBackend` is a backend boundary with SDK-neutral shape/body handles, fixed-step simulation, impulse, snapshot and raycast ports; the reference backend remains deterministic and simplified.
- Transform source/sink contracts keep Physics decoupled from Scene/World ownership.

## Forbidden Dependencies

`Physics` must not own World state, destroy World objects or include private World implementation headers.

## Testing Strategy

The `Physics` tests cover generation/stale handles, unknown handle errors, independent lifecycle/activity/dirty state, fixed-step accumulator and max substeps, backend failure propagation, transform source/sink synchronization, explicit contact states, raycast direction/max-distance behavior, dirty flags and service factory shape.
