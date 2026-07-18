# Animation

## Purpose

`Animation` owns animator lifecycle, per-instance playback state, pose readiness snapshots and generic animation events. Production skeleton and clip metadata may come from resource payloads through a public resource source. Animation does not update Renderer directly and does not encode gameplay behavior.

## Public Contracts

- `animation_types.h`: resource ids, generation handles, decomposed animator lifecycle/readiness/playback state, pose state, LOD hints, per-instance playback commands, crossfade state, immutable `PoseBuffer`, events and `PoseSnapshot`.
- `animation_runtime.h`: skeleton registry, clip registry, animator runtime, resource source, pose provider/sink, event buffer contracts, production `CreateAnimationServices()` and explicit reference/mock factories.

## Rules

- Skeletons require valid ids and at least one joint.
- Clips require valid ids, registered skeletons and positive duration.
- Animators require valid runtime owners and registered skeletons.
- `AnimationClipDesc` is immutable metadata; loop, playback rate and local time live in `AnimatorPlayback`.
- `Tick(max_animators)` advances playing/blending animators in deterministic animator-id order.
- Delta-based `Tick(GameDuration, max_animators)` is the primary playback step; item-only tick remains a compatibility wrapper.
- Pause, stop, loop and crossfade are explicit runtime commands.
- Crossfade stores source/target clips, elapsed/duration and source/target weights. Delta tick advances both playback and crossfade state, then switches to the target clip when complete.
- Pose buffers contain bone transforms and are immutable snapshots for consumers.
- Pose publication uses `IAnimationPoseSink::Publish(std::shared_ptr<const PoseBuffer>)`.
- `PoseSnapshot::revision` starts at `1` and increments on observable animator/pose changes.
- Production `CreateAnimationServices()` does not enable mock pose evaluation, even if options request it. Reference/mock pose evaluation is explicit through `CreateReferenceAnimationServices()` or `CreateMockAnimationServices()`.
- Events are queued into a buffer; Animation does not call Renderer, World or gameplay systems directly.

## Testing Strategy

The `Animation` tests cover resource validation, animator lifecycle, handle playback, per-animator loop isolation, immutable clip metadata, pause/stop/crossfade, crossfade completion, delta tick, immutable pose snapshots/buffers, pose sink publication, resource source failure, deterministic tick order, LOD-driven pose state, production/reference/mock factory separation and event buffer clearing.
