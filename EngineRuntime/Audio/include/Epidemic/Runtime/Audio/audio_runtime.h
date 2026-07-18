#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Audio/audio_types.h"

#include <memory>
#include <optional>
#include <span>

namespace epidemic::runtime::audio
{
// Public contracts for Audio runtime services. The interfaces expose registry, emitter, listener,
// mixer and event-queue behavior while keeping backend ownership behind the implementation boundary.

class ISoundRegistry
{
public:
    virtual ~ISoundRegistry() = default;

    [[nodiscard]] virtual foundation::Result<void> RegisterSound(SoundDesc desc) = 0;

    [[nodiscard]] virtual SoundState GetSoundState(SoundId id) const = 0;
};

class IAudioBackend
{
public:
    virtual ~IAudioBackend() = default;

    [[nodiscard]] virtual bool IsEnabled() const = 0;
    [[nodiscard]] virtual foundation::Result<void> Initialize(const AudioBackendOptions& options) = 0;
    [[nodiscard]] virtual foundation::Result<BackendVoiceHandle> CreateVoice(const AudioClipPayload& payload) = 0;
    [[nodiscard]] virtual foundation::Result<void> DestroyVoice(BackendVoiceHandle handle) = 0;
    [[nodiscard]] virtual foundation::Result<void> Play(BackendVoiceHandle handle) = 0;
    [[nodiscard]] virtual foundation::Result<void> Pause(BackendVoiceHandle handle) = 0;
    [[nodiscard]] virtual foundation::Result<void> Stop(BackendVoiceHandle handle) = 0;
    [[nodiscard]] virtual foundation::Result<void> SetGain(BackendVoiceHandle handle, float gain) = 0;
    [[nodiscard]] virtual foundation::Result<void> SetSpatialState(BackendVoiceHandle handle, const AudioSpatialState& state) = 0;
    [[nodiscard]] virtual foundation::Result<void> Update(GameDuration delta) = 0;
};

class IAudioResourceSource
{
public:
    virtual ~IAudioResourceSource() = default;

    [[nodiscard]] virtual SoundState GetSoundState(SoundId id) const = 0;
    [[nodiscard]] virtual foundation::Result<AudioClipPayload> LoadClip(SoundId id) const = 0;
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

    [[nodiscard]] virtual foundation::Result<AudioEmitterId> CreateEmitter(const AudioEmitterDesc& desc) = 0;
    [[nodiscard]] virtual foundation::Result<AudioEmitterHandle> CreateEmitterHandle(const AudioEmitterDesc& desc) = 0;

    [[nodiscard]] virtual foundation::Result<void> DestroyEmitter(AudioEmitterId id) = 0;

    [[nodiscard]] virtual foundation::Result<void> Play(AudioEmitterId id) = 0;

    [[nodiscard]] virtual foundation::Result<void> Stop(AudioEmitterId id) = 0;

    [[nodiscard]] virtual foundation::Result<void> FadeOut(AudioEmitterId id) = 0;
    [[nodiscard]] virtual foundation::Result<void> FadeOut(AudioEmitterHandle handle, GameDuration duration) = 0;

    [[nodiscard]] virtual foundation::Result<void> Virtualize(AudioEmitterId id) = 0;

    [[nodiscard]] virtual EmitterState GetEmitterState(AudioEmitterId id) const = 0;

    [[nodiscard]] virtual AudioEmitterSnapshot GetEmitterSnapshot(AudioEmitterId id) const = 0;
    [[nodiscard]] virtual AudioEmitterSnapshot GetEmitterSnapshot(AudioEmitterHandle handle) const = 0;
    [[nodiscard]] virtual foundation::Result<void> Tick(GameDuration delta) = 0;
};

class IListenerSystem
{
public:
    virtual ~IListenerSystem() = default;

    [[nodiscard]] virtual foundation::Result<AudioListenerId> CreateListener(const AudioListenerDesc& desc) = 0;
    [[nodiscard]] virtual foundation::Result<AudioListenerHandle> CreateListenerHandle(const AudioListenerDesc& desc) = 0;
    [[nodiscard]] virtual foundation::Result<void> DestroyListener(AudioListenerId id) = 0;
    [[nodiscard]] virtual foundation::Result<void> DestroyListener(AudioListenerHandle handle) = 0;

    [[nodiscard]] virtual foundation::Result<void> SetMainListener(AudioListenerId id) = 0;
    [[nodiscard]] virtual foundation::Result<void> SetMainListener(AudioListenerHandle handle) = 0;

    [[nodiscard]] virtual std::optional<AudioListenerId> GetMainListener() const = 0;
};

class IAudioEventQueue
{
public:
    virtual ~IAudioEventQueue() = default;

    [[nodiscard]] virtual foundation::Result<void> SubmitOneShot(const AudioEvent& event) = 0;

    [[nodiscard]] virtual std::span<const AudioEvent> Events() const = 0;

    virtual void Clear() = 0;
};

class IMixerSystem
{
public:
    virtual ~IMixerSystem() = default;

    [[nodiscard]] virtual foundation::Result<void> SetMixerGroup(MixerGroupState state) = 0;

    [[nodiscard]] virtual std::optional<MixerGroupState> GetMixerGroup(MixerGroupId id) const = 0;
};

struct AudioDependencies
{
    std::shared_ptr<IAudioBackend> backend;
    std::shared_ptr<IAudioResourceSource> resources;
    std::shared_ptr<IAudioTransformSource> transforms;
};

[[nodiscard]] std::unique_ptr<class AudioRuntime> CreateAudioRuntime(
    AudioOptions options = {},
    AudioDependencies dependencies = {});

struct AudioServices
{
    std::shared_ptr<ISoundRegistry> sounds;
    std::shared_ptr<IAudioRuntime> runtime;
    std::shared_ptr<IListenerSystem> listeners;
    std::shared_ptr<IAudioEventQueue> events;
    std::shared_ptr<IMixerSystem> mixer;
    std::shared_ptr<IAudioBackend> backend;
};

[[nodiscard]] foundation::Result<AudioServices> CreateAudioServices(
    AudioOptions options = {},
    AudioDependencies dependencies = {});
[[nodiscard]] foundation::Result<AudioServices> CreateMockAudioServices(AudioOptions options = {});
} // namespace epidemic::runtime::audio
