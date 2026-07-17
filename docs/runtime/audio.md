# Audio

## Purpose

`Audio` owns sound resource state, emitter playback lifecycle, listener selection, mixer snapshots and queued one-shot events. It does not own Environment, Scene or gameplay rules.

## Public Contracts

- `audio_types.h`: sound, emitter generation handle, listener and mixer ids; sound/emitter states; fade duration/progress; mixer hierarchy; events; `AudioEmitterSnapshot`.
- `audio_runtime.h`: sound registry, audio runtime, backend/resource/transform source contracts, listener system, bounded event queue, mixer contracts and `CreateMockAudioServices()`.

## Rules

- Emitters require a valid owner, registered sound and valid transform node.
- Playback requires a ready sound and enabled backend contract.
- `AudioEmitterSnapshot::revision` starts at `1` and increments only on observable emitter state changes.
- Repeating an idempotent state command does not churn emitter revision.
- One-shot events are queued through a bounded event queue and cleared by frame orchestration.
- Listener lifecycle includes destruction; destroying the main listener clears selection.
- Mixer groups may reference parent groups and preserve fade progress metadata.
- Environment ambience and Scene transform data must arrive through adapters; Audio does not call those majors directly.

## Testing Strategy

The `Audio` tests cover emitter lifecycle, generation handles, versioned emitter snapshots, listener lifecycle, backend/resource/transform contracts, not-ready sound behavior, bounded one-shot queueing, mixer hierarchy/fade state, service factory shape and validation failures.
