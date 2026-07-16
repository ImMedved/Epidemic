# Animation

## Purpose

`Animation` owns skeleton and clip metadata, animator lifecycle, pose readiness snapshots and generic animation events. It does not update Renderer directly and does not encode gameplay behavior.

## Public Contracts

- `animation_types.h`: resource ids, animator state, pose state, LOD hints, events and `PoseSnapshot`.
- `animation_runtime.h`: skeleton registry, clip registry, animator runtime, pose provider and event buffer contracts.

## Rules

- Skeletons require valid ids and at least one joint.
- Clips require valid ids, registered skeletons and positive duration.
- Animators require valid runtime owners and registered skeletons.
- `Tick(max_animators)` advances playing/blending animators in deterministic animator-id order.
- `PoseSnapshot::revision` starts at `1` and increments on observable animator/pose changes.
- Events are queued into a buffer; Animation does not call Renderer, World or gameplay systems directly.

## Testing Strategy

The `Animation` tests cover resource validation, animator lifecycle, playback state transitions, immutable pose snapshots, deterministic tick order, LOD-driven pose state and event buffer clearing.
