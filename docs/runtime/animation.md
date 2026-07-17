# Animation

## Purpose

`Animation` owns skeleton and clip metadata, animator lifecycle, pose readiness snapshots and generic animation events. It does not update Renderer directly and does not encode gameplay behavior.

## Public Contracts

- `animation_types.h`: resource ids, generation handles, animator state, pose state, LOD hints, playback commands, immutable `PoseBuffer`, events and `PoseSnapshot`.
- `animation_runtime.h`: skeleton registry, clip registry, animator runtime, resource source, pose provider/sink, event buffer contracts and `CreateAnimationServices()`.

## Rules

- Skeletons require valid ids and at least one joint.
- Clips require valid ids, registered skeletons and positive duration.
- Animators require valid runtime owners and registered skeletons.
- `Tick(max_animators)` advances playing/blending animators in deterministic animator-id order.
- Delta-based `Tick(GameDuration, max_animators)` is the primary playback step; item-only tick remains a compatibility wrapper.
- Pause, stop, loop and crossfade are explicit runtime commands.
- Pose buffers contain bone transforms and are immutable snapshots for consumers.
- `PoseSnapshot::revision` starts at `1` and increments on observable animator/pose changes.
- Events are queued into a buffer; Animation does not call Renderer, World or gameplay systems directly.

## Testing Strategy

The `Animation` tests cover resource validation, animator lifecycle, handle playback, pause/stop/crossfade, delta tick, immutable pose snapshots/buffers, deterministic tick order, LOD-driven pose state, service factory shape and event buffer clearing.
