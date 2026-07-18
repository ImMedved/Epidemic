# Audio

## Purpose

`Audio` owns sound resource state, emitter playback lifecycle, listener selection, mixer snapshots and queued one-shot events. It does not own Environment, Scene or gameplay rules.

## Public Contracts

- `audio_types.h`: sound, emitter/listener generation handles, backend voice handles, listener and mixer ids; sound/emitter states; fade duration/progress; mixer hierarchy; spatial event source, overflow policy, events and `AudioEmitterSnapshot`.
- `audio_runtime.h`: sound registry, audio runtime, full backend/resource/transform source contracts, listener system, bounded event queue, mixer contracts, production `CreateAudioServices()` and explicit `CreateMockAudioServices()`.

## Rules

- Emitters require a valid owner, registered sound and valid transform node.
- Playback requires a ready sound and a backend that can initialize, create a voice, play/stop, set gain and receive spatial state.
- Production `CreateAudioServices()` requires an injected backend. Mock behavior is available only through `CreateMockAudioServices()`.
- `AudioEmitterSnapshot::revision` starts at `1` and increments only on observable emitter state changes.
- Repeating an idempotent state command does not churn emitter revision.
- `AudioRuntime::Tick(delta)` advances fade elapsed/progress, updates backend gain and calls backend update.
- One-shot events are queued through a bounded event queue with explicit `DropNewest`, `DropOldest` or `FailSubmit` overflow policy.
- Listener lifecycle uses generation handles. Unknown or stale listener handles return errors; destroying the main listener clears selection.
- Mixer groups validate parent existence, self-parenting, cycles, finite volume and baseline `[0,1]` volume unless boost is enabled.
- Attached emitters use runtime object/transform source data. One-shot events are either world-positioned or non-spatial and must not carry contradictory source data.
- Environment ambience and Scene transform data must arrive through adapters; Audio does not call those majors directly.

## Testing Strategy

The `Audio` tests cover production backend requirement, explicit mock factory, backend failure propagation, emitter lifecycle, generation handles, versioned emitter snapshots, listener generation/stale errors, fade progression, attached transform updates, backend/resource/transform contracts, not-ready sound behavior, bounded one-shot overflow policies, mixer hierarchy/cycle validation, service factory shape and validation failures.
