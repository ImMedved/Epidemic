# Audio

## Purpose

`Audio` owns sound resource state, emitter playback lifecycle, listener selection, mixer snapshots and queued one-shot events. It does not own Environment, Scene or gameplay rules.

## Public Contracts

- `audio_types.h`: sound, emitter, listener and mixer ids; sound/emitter states; events; `AudioEmitterSnapshot`.
- `audio_runtime.h`: sound registry, audio runtime, listener system, event queue and mixer contracts.

## Rules

- Emitters require a valid owner, registered sound and valid transform node.
- Playback requires a ready sound and enabled backend contract.
- `AudioEmitterSnapshot::revision` starts at `1` and increments only on observable emitter state changes.
- Repeating an idempotent state command does not churn emitter revision.
- One-shot events are queued through the event queue and cleared by frame orchestration.
- Environment ambience and Scene transform data must arrive through adapters; Audio does not call those majors directly.

## Testing Strategy

The `Audio` tests cover emitter lifecycle, versioned emitter snapshots, listener selection, not-ready sound behavior, one-shot queueing, mixer state and validation failures.
