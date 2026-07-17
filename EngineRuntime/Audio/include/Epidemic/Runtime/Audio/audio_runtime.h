#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Audio/audio_types.h"

#include <memory>
#include <optional>
#include <span>

namespace epidemic::runtime::audio
{
// File note:
// Public contracts for Audio runtime services. The interfaces expose registry, emitter, listener,
// mixer and event-queue behavior while keeping backend ownership behind the implementation boundary.

class ISoundRegistry
{
public:
    virtual ~ISoundRegistry() = default;

    // Function note: Registers or updates sound resource state.
    // Inputs: sound descriptor; outputs: success/failure Result.
    // Relations: emitter playback validates readiness through this registry.
    [[nodiscard]] virtual foundation::Result<void> RegisterSound(SoundDesc desc) = 0;

    // Function note: Reads current sound state.
    // Inputs: sound id; outputs: Missing for unknown ids.
    // Relations: used by Play and one-shot event validation.
    [[nodiscard]] virtual SoundState GetSoundState(SoundId id) const = 0;
};

class IAudioBackend
{
public:
    virtual ~IAudioBackend() = default;

    [[nodiscard]] virtual bool IsEnabled() const = 0;
};

class IAudioResourceSource
{
public:
    virtual ~IAudioResourceSource() = default;

    [[nodiscard]] virtual SoundState GetSoundState(SoundId id) const = 0;
};

class IAudioTransformSource
{
public:
    virtual ~IAudioTransformSource() = default;

    [[nodiscard]] virtual foundation::Result<Transform> ReadTransform(AudioTransformId id) const = 0;
};

class IAudioRuntime
{
public:
    virtual ~IAudioRuntime() = default;

    // Function note: Creates an emitter bound to a sound and scene transform.
    // Inputs: emitter descriptor; outputs: emitter id or validation error.
    // Relations: Play/Stop/DestroyEmitter operate on returned ids.
    [[nodiscard]] virtual foundation::Result<AudioEmitterId> CreateEmitter(const AudioEmitterDesc& desc) = 0;
    [[nodiscard]] virtual foundation::Result<AudioEmitterHandle> CreateEmitterHandle(const AudioEmitterDesc& desc) = 0;

    // Function note: Destroys an emitter and marks its lifecycle terminal.
    // Inputs: emitter id; outputs: success/failure Result.
    // Relations: removes emitter state from active playback registries.
    [[nodiscard]] virtual foundation::Result<void> DestroyEmitter(AudioEmitterId id) = 0;

    // Function note: Starts emitter playback if its sound is ready.
    // Inputs: emitter id; outputs: success/failure Result.
    // Relations: transitions emitter state without owning external scene state.
    [[nodiscard]] virtual foundation::Result<void> Play(AudioEmitterId id) = 0;

    // Function note: Stops emitter playback.
    // Inputs: emitter id; outputs: success/failure Result.
    // Relations: returns emitter to Stopped state for reuse.
    [[nodiscard]] virtual foundation::Result<void> Stop(AudioEmitterId id) = 0;

    // Function note: Applies fade-out placeholder state.
    // Inputs: emitter id; outputs: success/failure Result.
    // Relations: models mixer/backend transition without time-based backend work.
    [[nodiscard]] virtual foundation::Result<void> FadeOut(AudioEmitterId id) = 0;
    [[nodiscard]] virtual foundation::Result<void> FadeOut(AudioEmitterHandle handle, GameDuration duration) = 0;

    // Function note: Marks an emitter as virtualized.
    // Inputs: emitter id; outputs: success/failure Result.
    // Relations: placeholder for distance/budget virtualization policy.
    [[nodiscard]] virtual foundation::Result<void> Virtualize(AudioEmitterId id) = 0;

    // Function note: Reads emitter lifecycle state.
    // Inputs: emitter id; outputs: Destroyed for unknown ids.
    // Relations: status query for frame orchestration and tests.
    [[nodiscard]] virtual EmitterState GetEmitterState(AudioEmitterId id) const = 0;

    // Function note: Reads immutable emitter playback state with revision.
    // Inputs: emitter id; outputs: emitter snapshot, or Destroyed revision 0 for unknown ids.
    // Relations: adapters can observe Audio without receiving mutable emitter storage.
    [[nodiscard]] virtual AudioEmitterSnapshot GetEmitterSnapshot(AudioEmitterId id) const = 0;
    [[nodiscard]] virtual AudioEmitterSnapshot GetEmitterSnapshot(AudioEmitterHandle handle) const = 0;
};

class IListenerSystem
{
public:
    virtual ~IListenerSystem() = default;

    // Function note: Creates a listener bound to a scene transform.
    // Inputs: listener descriptor; outputs: listener id or validation error.
    // Relations: main-listener selection uses ids returned here.
    [[nodiscard]] virtual foundation::Result<AudioListenerId> CreateListener(const AudioListenerDesc& desc) = 0;
    [[nodiscard]] virtual foundation::Result<void> DestroyListener(AudioListenerId id) = 0;

    // Function note: Selects the main listener.
    // Inputs: listener id; outputs: success/failure Result.
    // Relations: spatial adapters can read this choice without mutating scene data.
    [[nodiscard]] virtual foundation::Result<void> SetMainListener(AudioListenerId id) = 0;

    // Function note: Reads the main listener id if one exists.
    // Inputs: none; outputs: optional listener id.
    // Relations: pairs with SetMainListener for composition code.
    [[nodiscard]] virtual std::optional<AudioListenerId> GetMainListener() const = 0;
};

class IAudioEventQueue
{
public:
    virtual ~IAudioEventQueue() = default;

    // Function note: Queues a one-shot audio event.
    // Inputs: sound/position/volume event; outputs: success/failure Result.
    // Relations: consumers can drain Events after frame orchestration processes queued one-shots.
    [[nodiscard]] virtual foundation::Result<void> SubmitOneShot(const AudioEvent& event) = 0;

    // Function note: Exposes queued one-shot events.
    // Inputs: none; outputs: immutable span over current queue.
    // Relations: event consumers read this without taking ownership.
    [[nodiscard]] virtual std::span<const AudioEvent> Events() const = 0;

    // Function note: Clears the queued one-shot events.
    // Inputs: none; outputs: none.
    // Relations: called after event processing completes.
    virtual void Clear() = 0;
};

class IMixerSystem
{
public:
    virtual ~IMixerSystem() = default;

    // Function note: Sets or updates a mixer group snapshot.
    // Inputs: mixer group state; outputs: success/failure Result.
    // Relations: placeholder contract for future backend mixer integration.
    [[nodiscard]] virtual foundation::Result<void> SetMixerGroup(MixerGroupState state) = 0;

    // Function note: Reads mixer group state.
    // Inputs: mixer group id; outputs: group state if registered.
    // Relations: validates fade/volume placeholder behavior.
    [[nodiscard]] virtual std::optional<MixerGroupState> GetMixerGroup(MixerGroupId id) const = 0;
};

[[nodiscard]] std::unique_ptr<class AudioRuntime> CreateAudioRuntime(AudioOptions options = {});

struct AudioServices
{
    std::shared_ptr<ISoundRegistry> sounds;
    std::shared_ptr<IAudioRuntime> runtime;
    std::shared_ptr<IListenerSystem> listeners;
    std::shared_ptr<IAudioEventQueue> events;
    std::shared_ptr<IMixerSystem> mixer;
    std::shared_ptr<IAudioBackend> backend;
};

[[nodiscard]] AudioServices CreateMockAudioServices(AudioOptions options = {});
} // namespace epidemic::runtime::audio
