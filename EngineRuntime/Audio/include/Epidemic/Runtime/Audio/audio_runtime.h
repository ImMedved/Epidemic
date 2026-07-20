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
    [[nodiscard]] virtual foundation::Result<BackendVoiceHandle> CreateVoice(const AudioVoiceDesc& desc) = 0;
    [[nodiscard]] virtual foundation::Result<void> DestroyVoice(BackendVoiceHandle handle) = 0;
    [[nodiscard]] virtual foundation::Result<void> Play(BackendVoiceHandle handle) = 0;
    [[nodiscard]] virtual foundation::Result<void> Pause(BackendVoiceHandle handle) = 0;
    [[nodiscard]] virtual foundation::Result<void> Stop(BackendVoiceHandle handle) = 0;
    [[nodiscard]] virtual bool IsVoiceFinished(BackendVoiceHandle handle) const = 0;
    [[nodiscard]] virtual foundation::Result<void> SetGain(BackendVoiceHandle handle, float gain) = 0;
    [[nodiscard]] virtual foundation::Result<void> SetSpatialState(BackendVoiceHandle handle, const AudioSpatialState& state) = 0;
    [[nodiscard]] virtual foundation::Result<void> CreateBackendListener(AudioListenerHandle handle, const Transform& transform) = 0;
    [[nodiscard]] virtual foundation::Result<void> DestroyBackendListener(AudioListenerHandle handle) = 0;
    [[nodiscard]] virtual foundation::Result<void> SetBackendListenerTransform(AudioListenerHandle handle, const Transform& transform) = 0;
    [[nodiscard]] virtual foundation::Result<void> Update(RuntimeFrameDuration delta) = 0;
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

    [[nodiscard]] virtual foundation::Result<AudioEmitterHandle> CreateEmitterHandle(const AudioEmitterDesc& desc) = 0;

    [[nodiscard]] virtual foundation::Result<void> DestroyEmitter(AudioEmitterHandle handle) = 0;

    [[nodiscard]] virtual foundation::Result<void> Play(AudioEmitterHandle handle) = 0;

    [[nodiscard]] virtual foundation::Result<void> Pause(AudioEmitterHandle handle) = 0;

    [[nodiscard]] virtual foundation::Result<void> Resume(AudioEmitterHandle handle) = 0;

    [[nodiscard]] virtual foundation::Result<void> Stop(AudioEmitterHandle handle) = 0;

    [[nodiscard]] virtual foundation::Result<void> FadeOut(AudioEmitterHandle handle, RuntimeFrameDuration duration) = 0;

    [[nodiscard]] virtual foundation::Result<void> FadeIn(AudioEmitterHandle handle, RuntimeFrameDuration duration) = 0;

    // Virtualization releases the backend voice; calling Play(handle) later restarts playback from the clip beginning.
    [[nodiscard]] virtual foundation::Result<void> Virtualize(AudioEmitterHandle handle) = 0;

    [[nodiscard]] virtual foundation::Result<EmitterState> GetEmitterState(AudioEmitterHandle handle) const = 0;

    [[nodiscard]] virtual foundation::Result<AudioEmitterSnapshot> GetEmitterSnapshot(AudioEmitterHandle handle) const = 0;
    [[nodiscard]] virtual foundation::Result<void> Tick(RuntimeFrameDuration delta) = 0;
    [[nodiscard]] virtual foundation::Result<void> Shutdown() = 0;
};

class IListenerSystem
{
public:
    virtual ~IListenerSystem() = default;

    [[nodiscard]] virtual foundation::Result<AudioListenerHandle> CreateListenerHandle(const AudioListenerDesc& desc) = 0;
    [[nodiscard]] virtual foundation::Result<void> DestroyListener(AudioListenerHandle handle) = 0;

    [[nodiscard]] virtual foundation::Result<void> SetMainListener(AudioListenerHandle handle) = 0;

    [[nodiscard]] virtual std::optional<AudioListenerHandle> GetMainListener() const = 0;
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
