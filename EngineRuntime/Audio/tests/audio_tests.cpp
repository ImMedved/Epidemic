#include "audio_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

using epidemic::runtime::RuntimeObjectId;
using epidemic::runtime::RuntimeFrameDuration;
using epidemic::runtime::Transform;
using epidemic::runtime::Vec3;
using epidemic::runtime::audio::AudioBackendOptions;
using epidemic::runtime::audio::AudioClipPayload;
using epidemic::runtime::audio::AudioClipFormat;
using epidemic::runtime::audio::AudioClipStorage;
using epidemic::runtime::audio::AudioDependencies;
using epidemic::runtime::audio::CreateMockAudioServices;
using epidemic::runtime::audio::CreateAudioServices;
using epidemic::runtime::audio::AudioVoiceDesc;
using epidemic::runtime::audio::AudioEventOverflowPolicy;
using epidemic::runtime::audio::IAudioClipResource;
using epidemic::runtime::audio::IAudioStreamSource;
using epidemic::runtime::audio::AudioListenerHandle;
using epidemic::runtime::audio::AudioSpatialState;
using epidemic::runtime::audio::AudioStreamReadResult;
using epidemic::runtime::audio::IAudioBackend;
using epidemic::runtime::audio::IAudioResourceSource;
using epidemic::runtime::audio::IAudioTransformSource;
using epidemic::runtime::audio::AudioEmitterDesc;
using epidemic::runtime::audio::AudioEmitterHandle;
using epidemic::runtime::audio::AudioEmitterId;
using epidemic::runtime::audio::AudioEvent;
using epidemic::runtime::audio::AudioListenerDesc;
using epidemic::runtime::audio::AudioOptions;
using epidemic::runtime::audio::AudioListenerId;
using epidemic::runtime::audio::AudioRuntime;
using epidemic::runtime::audio::AudioTransformId;
using epidemic::runtime::audio::BackendVoiceHandle;
using epidemic::runtime::audio::EmitterState;
using epidemic::runtime::audio::MixerFadeState;
using epidemic::runtime::audio::MixerGroupId;
using epidemic::runtime::audio::MixerGroupState;
using epidemic::runtime::audio::SoundDesc;
using epidemic::runtime::audio::SoundId;
using epidemic::runtime::audio::SoundState;

namespace
{
struct TestBackend final : IAudioBackend
{
    bool enabled = true;
    bool fail_create = false;
    bool fail_destroy = false;
    bool fail_play = false;
    bool fail_spatial = false;
    bool fail_update = false;
    bool fail_destroy_listener_once = false;
    bool fail_listener_update = false;
    std::uint64_t next_voice = 1;
    int created = 0;
    int played = 0;
    int stopped = 0;
    int destroyed = 0;
    int spatial_updates = 0;
    int gain_updates = 0;
    int backend_listeners_created = 0;
    int backend_listeners_destroyed = 0;
    int backend_listener_destroy_attempts = 0;
    int backend_listener_updates = 0;
    AudioVoiceDesc last_voice_desc{};
    BackendVoiceHandle last_created_voice{};
    std::vector<BackendVoiceHandle> finished_voices;
    float last_gain = 1.0f;
    AudioSpatialState last_spatial{};

    bool IsEnabled() const override { return enabled; }

    epidemic::foundation::Result<void> Initialize(const AudioBackendOptions& options) override
    {
        enabled = options.enabled;
        return epidemic::foundation::Result<void>::Success();
    }

    epidemic::foundation::Result<BackendVoiceHandle> CreateVoice(const AudioVoiceDesc& desc) override
    {
        if (fail_create)
        {
            return epidemic::foundation::Result<BackendVoiceHandle>::Failure(
                epidemic::foundation::Error::Create("audio.backend_failed", "test backend create failed"));
        }
        if (desc.clip.state != SoundState::Ready || desc.clip.resource == nullptr)
        {
            return epidemic::foundation::Result<BackendVoiceHandle>::Failure(
                epidemic::foundation::Error::Create("audio.sound_not_ready", "test payload not ready"));
        }
        ++created;
        last_voice_desc = desc;
        last_created_voice = BackendVoiceHandle{next_voice++};
        return epidemic::foundation::Result<BackendVoiceHandle>::Success(last_created_voice);
    }

    epidemic::foundation::Result<void> DestroyVoice(BackendVoiceHandle handle) override
    {
        if (fail_destroy)
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("audio.backend_failed", "test backend destroy failed"));
        }
        ++destroyed;
        finished_voices.erase(std::remove(finished_voices.begin(), finished_voices.end(), handle), finished_voices.end());
        return epidemic::foundation::Result<void>::Success();
    }

    epidemic::foundation::Result<void> Play(BackendVoiceHandle) override
    {
        if (fail_play)
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("audio.backend_failed", "test backend play failed"));
        }
        ++played;
        return epidemic::foundation::Result<void>::Success();
    }

    epidemic::foundation::Result<void> Pause(BackendVoiceHandle) override { return epidemic::foundation::Result<void>::Success(); }

    epidemic::foundation::Result<void> Stop(BackendVoiceHandle) override
    {
        ++stopped;
        return epidemic::foundation::Result<void>::Success();
    }

    bool IsVoiceFinished(BackendVoiceHandle handle) const override
    {
        return std::find(finished_voices.begin(), finished_voices.end(), handle) != finished_voices.end();
    }

    epidemic::foundation::Result<void> SetGain(BackendVoiceHandle, float gain) override
    {
        ++gain_updates;
        last_gain = gain;
        return epidemic::foundation::Result<void>::Success();
    }

    epidemic::foundation::Result<void> SetSpatialState(BackendVoiceHandle, const AudioSpatialState& state) override
    {
        if (fail_spatial)
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("audio.backend_failed", "test backend spatial failed"));
        }
        ++spatial_updates;
        last_spatial = state;
        return epidemic::foundation::Result<void>::Success();
    }

    epidemic::foundation::Result<void> CreateBackendListener(AudioListenerHandle, const Transform&) override
    {
        ++backend_listeners_created;
        return epidemic::foundation::Result<void>::Success();
    }

    epidemic::foundation::Result<void> DestroyBackendListener(AudioListenerHandle) override
    {
        ++backend_listener_destroy_attempts;
        if (fail_destroy_listener_once)
        {
            fail_destroy_listener_once = false;
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("audio.backend_failed", "test backend listener destroy failed"));
        }
        ++backend_listeners_destroyed;
        return epidemic::foundation::Result<void>::Success();
    }

    epidemic::foundation::Result<void> SetBackendListenerTransform(AudioListenerHandle, const Transform&) override
    {
        if (fail_listener_update)
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("audio.backend_failed", "test backend listener update failed"));
        }
        ++backend_listener_updates;
        return epidemic::foundation::Result<void>::Success();
    }

    epidemic::foundation::Result<void> Update(RuntimeFrameDuration) override
    {
        if (fail_update)
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("audio.backend_failed", "test backend update failed"));
        }
        return epidemic::foundation::Result<void>::Success();
    }
};

struct TestClipResource final : IAudioClipResource
{
    AudioClipFormat format{2, 48000, 16, false};
    std::array<std::byte, 4> data{std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3}};

    AudioClipFormat GetFormat() const override { return format; }
    AudioClipStorage GetStorage() const override { return AudioClipStorage::InMemoryEncoded; }
    std::span<const std::byte> GetEncodedData() const override { return data; }
    std::shared_ptr<epidemic::runtime::audio::IAudioStreamSource> GetStreamSource() const override { return {}; }
};

struct TestStreamSource final : IAudioStreamSource
{
    std::uint64_t size = 4;
    bool seekable = true;
    std::size_t next_read = 0;
    bool next_eof = false;

    std::uint64_t GetSizeBytes() const override { return size; }
    bool IsSeekable() const override { return seekable; }

    epidemic::foundation::Result<AudioStreamReadResult> Read(std::uint64_t, std::span<std::byte> output) override
    {
        if (next_read > output.size())
        {
            return epidemic::foundation::Result<AudioStreamReadResult>::Failure(
                epidemic::foundation::Error::Create("audio.invalid_stream_read", "stream read exceeded output buffer"));
        }
        return epidemic::foundation::Result<AudioStreamReadResult>::Success(AudioStreamReadResult{next_read, next_eof});
    }
};

struct TestResourceSource final : IAudioResourceSource
{
    SoundState state = SoundState::Ready;

    SoundState GetSoundState(SoundId) const override { return state; }

    epidemic::foundation::Result<AudioClipPayload> LoadClip(SoundId id) const override
    {
        if (state == SoundState::Missing)
        {
            return epidemic::foundation::Result<AudioClipPayload>::Failure(
                epidemic::foundation::Error::Create("audio.sound_not_found", "test sound missing"));
        }
        return epidemic::foundation::Result<AudioClipPayload>::Success(
            AudioClipPayload{id, state, std::make_shared<TestClipResource>()});
    }
};

struct TestTransformSource final : IAudioTransformSource
{
    Transform transform{};

    epidemic::foundation::Result<Transform> ReadTransform(AudioTransformId id) const override
    {
        if (!id.IsValid())
        {
            return epidemic::foundation::Result<Transform>::Failure(
                epidemic::foundation::Error::Create("audio.invalid_transform", "test transform missing"));
        }
        return epidemic::foundation::Result<Transform>::Success(transform);
    }
};

bool Expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }

    return condition;
}

bool SeedSound(AudioRuntime& runtime, SoundState state = SoundState::Ready)
{
    return runtime.RegisterSound(SoundDesc{SoundId{1}, state}).HasValue();
}

AudioEmitterDesc MakeEmitterDesc()
{
    return AudioEmitterDesc{RuntimeObjectId{88}, SoundId{1}, AudioTransformId{9}, false};
}

bool TestCreatePlayStopDestroyEmitter()
{
    AudioRuntime runtime{{}};
    bool ok = Expect(SeedSound(runtime), "sound should seed");
    const auto emitter = runtime.CreateEmitterHandle(MakeEmitterDesc());
    ok &= Expect(emitter.HasValue(), "valid emitter should be created");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()).Value() == EmitterState::Stopped, "new emitter should be stopped");
    ok &= Expect(runtime.GetEmitterSnapshot(emitter.Value()).Value().revision == 1, "new emitter snapshot should start at revision one");
    ok &= Expect(runtime.Play(emitter.Value()).HasValue(), "ready sound should play");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()).Value() == EmitterState::Playing, "emitter should be playing");
    ok &= Expect(runtime.GetEmitterSnapshot(emitter.Value()).Value().revision == 2, "play should increment emitter revision");
    ok &= Expect(runtime.Stop(emitter.Value()).HasValue(), "playing emitter should stop");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()).Value() == EmitterState::Stopped, "emitter should return stopped");
    ok &= Expect(runtime.GetEmitterSnapshot(emitter.Value()).Value().revision == 3, "stop should increment emitter revision");
    ok &= Expect(runtime.Stop(emitter.Value()).HasValue(), "idempotent stop should succeed");
    ok &= Expect(runtime.GetEmitterSnapshot(emitter.Value()).Value().revision == 3, "idempotent stop should not increment revision");
    ok &= Expect(runtime.DestroyEmitter(emitter.Value()).HasValue(), "emitter should destroy");
    ok &= Expect(!runtime.GetEmitterState(emitter.Value()).HasValue(), "destroyed emitter state should be stale");
    ok &= Expect(!runtime.GetEmitterSnapshot(emitter.Value()).HasValue(), "destroyed emitter snapshot should be stale");
    return ok;
}

bool TestListenerCreationAndMainListener()
{
    AudioRuntime runtime{{}};
    const auto listener = runtime.CreateListenerHandle(AudioListenerDesc{AudioTransformId{12}});
    bool ok = Expect(listener.HasValue(), "valid listener should be created");
    ok &= Expect(runtime.SetMainListener(listener.Value()).HasValue(), "main listener should be set");
    ok &= Expect(runtime.GetMainListener().has_value(), "main listener should be readable");
    ok &= Expect(runtime.GetMainListener().has_value() && runtime.GetMainListener()->id == listener.Value().id, "main listener id should match");
    ok &= Expect(!runtime.CreateListenerHandle(AudioListenerDesc{}).HasValue(), "invalid listener transform should fail");
    return ok;
}

bool TestInvalidSoundBehavior()
{
    AudioRuntime runtime{{}};
    bool ok = Expect(SeedSound(runtime, SoundState::Loading), "loading sound should seed");
    const auto emitter = runtime.CreateEmitterHandle(MakeEmitterDesc());
    ok &= Expect(emitter.HasValue(), "emitter can reference registered loading sound");
    ok &= Expect(!runtime.Play(emitter.Value()).HasValue(), "loading sound should not play");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()).Value() == EmitterState::Stopped, "failed play should leave stopped state");
    ok &= Expect(!runtime.SubmitOneShot(AudioEvent{SoundId{99}, Vec3{}, 1.0f}).HasValue(), "missing one-shot sound should fail");
    return ok;
}

bool TestFadeVirtualizedMixerAndEvents()
{
    AudioRuntime runtime{{}};
    bool ok = Expect(SeedSound(runtime), "sound should seed");
    const auto emitter = runtime.CreateEmitterHandle(MakeEmitterDesc());
    ok &= Expect(emitter.HasValue(), "valid emitter should be created");
    ok &= Expect(runtime.Play(emitter.Value()).HasValue(), "emitter should play before fade-out");
    ok &= Expect(runtime.FadeOut(emitter.Value(), RuntimeFrameDuration{}).HasValue(), "emitter should fade out");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()).Value() == EmitterState::Stopped, "zero-duration fade-out should stop synchronously");
    ok &= Expect(runtime.Virtualize(emitter.Value()).HasValue(), "emitter should virtualize");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()).Value() == EmitterState::Virtualized, "emitter should report virtualized state");
    ok &= Expect(runtime.SubmitOneShot(AudioEvent{SoundId{1}, Vec3{1.0f, 2.0f, 3.0f}, 0.75f}).HasValue(), "one-shot should queue");
    ok &= Expect(runtime.Events().size() == 1, "event queue should contain one event");
    runtime.Clear();
    ok &= Expect(runtime.Events().empty(), "event queue should clear");
    ok &= Expect(runtime.SetMixerGroup(MixerGroupState{MixerGroupId{3}, 0.5f, MixerFadeState::FadingOut, {},
                                                      RuntimeFrameDuration{std::chrono::microseconds{2}}})
                     .HasValue(),
                 "mixer group should update");
    const auto group = runtime.GetMixerGroup(MixerGroupId{3});
    ok &= Expect(group.has_value() && group->fade_state == MixerFadeState::FadingOut, "mixer fade state should be stored");
    return ok;
}

bool TestValidationFailures()
{
    AudioRuntime runtime{{}};
    bool ok = Expect(!runtime.RegisterSound(SoundDesc{}).HasValue(), "invalid sound id should fail");
    ok &= Expect(SeedSound(runtime), "valid sound should seed before duplicate check");
    ok &= Expect(!runtime.RegisterSound(SoundDesc{SoundId{1}, SoundState::Ready}).HasValue(), "duplicate sound id should fail");
    ok &= Expect(!runtime.CreateEmitterHandle(AudioEmitterDesc{}).HasValue(), "empty emitter desc should fail");
    ok &= Expect(!runtime.SetMainListener(AudioListenerHandle{epidemic::runtime::audio::AudioListenerId{99}, 1}).HasValue(), "unknown listener should fail main selection");
    ok &= Expect(!runtime.SetMixerGroup(MixerGroupState{MixerGroupId{}, 1.0f, MixerFadeState::Stable}).HasValue(), "invalid mixer group should fail");
    return ok;
}

bool TestBackendContractsHandlesBoundsAndHierarchy()
{
    AudioRuntime runtime{AudioOptions{true, 1}};
    bool ok = Expect(runtime.IsEnabled(), "mock backend should be enabled");
    ok &= Expect(SeedSound(runtime), "sound should seed");
    const auto handle = runtime.CreateEmitterHandle(MakeEmitterDesc());
    ok &= Expect(handle.HasValue(), "emitter handle should be created");
    ok &= Expect(runtime.Play(handle.Value()).HasValue(), "handle should play before fade-out");
    ok &= Expect(runtime.FadeOut(handle.Value(), RuntimeFrameDuration{std::chrono::microseconds{5}}).HasValue(), "handle fade should succeed");
    const auto snapshot = runtime.GetEmitterSnapshot(handle.Value());
    ok &= Expect(snapshot.HasValue() && snapshot.Value().handle == handle.Value(), "snapshot should preserve emitter handle");
    ok &= Expect(snapshot.HasValue() && snapshot.Value().fade_duration.value.count() == 5, "snapshot should expose fade duration");

    ok &= Expect(runtime.SubmitOneShot(AudioEvent{SoundId{1}, Vec3{}, 1.0f}).HasValue(), "first event should queue");
    ok &= Expect(!runtime.SubmitOneShot(AudioEvent{SoundId{1}, Vec3{}, 1.0f}).HasValue(), "bounded queue should reject overflow");

    const auto listener = runtime.CreateListenerHandle(AudioListenerDesc{AudioTransformId{44}});
    ok &= Expect(listener.HasValue(), "listener should be created");
    ok &= Expect(runtime.SetMainListener(listener.Value()).HasValue(), "listener should become main");
    ok &= Expect(runtime.DestroyListener(listener.Value()).HasValue(), "listener should destroy");
    ok &= Expect(!runtime.GetMainListener().has_value(), "destroyed main listener should clear selection");

    ok &= Expect(runtime.SetMixerGroup(MixerGroupState{MixerGroupId{3}, 1.0f, MixerFadeState::Stable}).HasValue(),
                 "parent mixer group should exist");
    ok &= Expect(runtime.SetMixerGroup(MixerGroupState{MixerGroupId{4}, 1.0f, MixerFadeState::FadingIn, MixerGroupId{3},
                                                      RuntimeFrameDuration{std::chrono::microseconds{2}}, 0.5f})
                     .HasValue(),
                 "hierarchical mixer group should update");
    const auto mixer = runtime.GetMixerGroup(MixerGroupId{4});
    ok &= Expect(mixer.has_value() && mixer->parent == MixerGroupId{3} && mixer->fade_progress == 0.0f,
                 "mixer hierarchy should initialize runtime fade progress");

    const auto services = CreateMockAudioServices();
    ok &= Expect(services.HasValue(), "mock audio services should be created");
    ok &= Expect(services.HasValue() && services.Value().sounds != nullptr && services.Value().runtime != nullptr &&
                     services.Value().listeners != nullptr && services.Value().events != nullptr &&
                     services.Value().mixer != nullptr && services.Value().backend != nullptr,
                 "audio services should be populated");
    return ok;
}

bool TestProductionFactoryBackendRequiredAndFailurePropagates()
{
    const auto missing = CreateAudioServices(AudioOptions{.enable_mock_backend = false}, {});
    bool ok = Expect(!missing.HasValue() && missing.GetError().HasCode("audio.backend_missing"),
                     "production audio factory should require backend");

    auto backend = std::make_shared<TestBackend>();
    auto resources = std::make_shared<TestResourceSource>();
    auto transforms = std::make_shared<TestTransformSource>();
    const auto services = CreateAudioServices(
        AudioOptions{.enable_mock_backend = false},
        AudioDependencies{backend, resources, transforms});
    ok &= Expect(services.HasValue(), "production audio factory should accept backend dependencies");

    const auto emitter = services.Value().runtime->CreateEmitterHandle(MakeEmitterDesc());
    ok &= Expect(emitter.HasValue(), "production emitter should create from resource source");
    backend->fail_create = true;
    const auto played = services.Value().runtime->Play(emitter.Value());
    ok &= Expect(!played.HasValue() && played.GetError().HasCode("audio.backend_failed"),
                 "backend create failure should propagate");

    const auto mock = CreateMockAudioServices();
    ok &= Expect(mock.HasValue() && mock.Value().backend != nullptr, "mock factory should stay explicit");
    return ok;
}

bool TestListenerGenerationAndUnknownErrors()
{
    AudioRuntime runtime{{}};
    const auto handle = runtime.CreateListenerHandle(AudioListenerDesc{AudioTransformId{44}});
    bool ok = Expect(handle.HasValue(), "listener handle should be created");
    ok &= Expect(runtime.SetMainListener(handle.Value()).HasValue(), "listener handle should select main listener");

    AudioListenerHandle stale = handle.Value();
    ++stale.generation;
    ok &= Expect(!runtime.SetMainListener(stale).HasValue(), "stale listener should fail main selection");
    ok &= Expect(!runtime.DestroyListener(stale).HasValue(), "stale listener should fail destruction");
    ok &= Expect(!runtime.DestroyEmitter(epidemic::runtime::audio::AudioEmitterHandle{AudioEmitterId{99}, 1}).HasValue(), "unknown emitter should fail destruction");
    ok &= Expect(!runtime.DestroyListener(AudioListenerHandle{AudioListenerId{99}, 1}).HasValue(), "unknown listener should fail destruction");
    return ok;
}

bool TestFadeProgressTickAndAttachedTransformUpdate()
{
    auto backend = std::make_shared<TestBackend>();
    auto resources = std::make_shared<TestResourceSource>();
    auto transforms = std::make_shared<TestTransformSource>();
    transforms->transform.position = Vec3{4.0f, 5.0f, 6.0f};
    AudioRuntime runtime{AudioOptions{.enable_mock_backend = false}, AudioDependencies{backend, resources, transforms}};

    const auto emitter = runtime.CreateEmitterHandle(MakeEmitterDesc());
    bool ok = Expect(emitter.HasValue(), "attached emitter should create");
    ok &= Expect(runtime.Play(emitter.Value()).HasValue(), "attached emitter should play through backend");
    ok &= Expect(backend->spatial_updates > 0 && backend->last_spatial.transform.position == transforms->transform.position,
                 "play should publish attached transform to backend");
    const auto handle = runtime.CreateEmitterHandle(MakeEmitterDesc());
    ok &= Expect(handle.HasValue(), "second emitter handle should create");
    ok &= Expect(runtime.Play(handle.Value()).HasValue(), "second emitter should play");
    ok &= Expect(runtime.FadeOut(handle.Value(), RuntimeFrameDuration{std::chrono::microseconds{4}}).HasValue(), "fade-out should start");
    ok &= Expect(runtime.Tick(RuntimeFrameDuration{std::chrono::microseconds{2}}).HasValue(), "audio tick should advance fade");
    const auto snapshot = runtime.GetEmitterSnapshot(handle.Value());
    ok &= Expect(snapshot.HasValue() && snapshot.Value().fade_progress > 0.49f && snapshot.Value().fade_progress < 0.51f, "fade should advance halfway");
    ok &= Expect(backend->gain_updates > 0 && backend->last_gain < 1.0f, "fade should update backend gain");
    return ok;
}

bool TestMixerCycleAndQueueOverflowPolicies()
{
    AudioRuntime runtime{AudioOptions{.enable_mock_backend = true, .max_queued_events = 1, .event_overflow_policy = AudioEventOverflowPolicy::DropOldest}};
    bool ok = Expect(SeedSound(runtime), "sound should seed");
    ok &= Expect(runtime.SubmitOneShot(AudioEvent{SoundId{1}, Vec3{1.0f, 0.0f, 0.0f}, 0.5f}).HasValue(), "first event should queue");
    ok &= Expect(runtime.SubmitOneShot(AudioEvent{SoundId{1}, Vec3{2.0f, 0.0f, 0.0f}, 0.5f}).HasValue(), "drop-oldest overflow should accept second event");
    ok &= Expect(runtime.Events().size() == 1 && runtime.Events().front().position.x == 2.0f, "drop-oldest should retain newest event");
    ok &= Expect(!runtime.SubmitOneShot(AudioEvent{SoundId{1}, Vec3{1.0f, 0.0f, 0.0f}, 0.5f, epidemic::runtime::audio::AudioEventSpace::NonSpatial}).HasValue(),
                 "non-spatial event with position should fail");

    ok &= Expect(runtime.SetMixerGroup(MixerGroupState{MixerGroupId{1}, 1.0f, MixerFadeState::Stable}).HasValue(), "root mixer should register");
    ok &= Expect(runtime.SetMixerGroup(MixerGroupState{MixerGroupId{2}, 1.0f, MixerFadeState::Stable, MixerGroupId{1}}).HasValue(), "child mixer should register");
    ok &= Expect(!runtime.SetMixerGroup(MixerGroupState{MixerGroupId{1}, 1.0f, MixerFadeState::Stable, MixerGroupId{2}}).HasValue(),
                 "mixer cycle should be rejected");
    ok &= Expect(!runtime.SetMixerGroup(MixerGroupState{MixerGroupId{3}, 2.0f, MixerFadeState::Stable}).HasValue(),
                 "mixer boost should be rejected by default");
    return ok;
}

bool TestBackendVoiceDescOneShotListenerAndMixerGain()
{
    auto backend = std::make_shared<TestBackend>();
    auto resources = std::make_shared<TestResourceSource>();
    auto transforms = std::make_shared<TestTransformSource>();
    AudioRuntime runtime{AudioOptions{.enable_mock_backend = false}, AudioDependencies{backend, resources, transforms}};

    bool ok = Expect(runtime.SetMixerGroup(MixerGroupState{MixerGroupId{7}, 0.5f, MixerFadeState::Stable}).HasValue(),
                     "mixer group should register");
    AudioEmitterDesc desc = MakeEmitterDesc();
    desc.loop = true;
    desc.mixer_group = MixerGroupId{7};
    desc.gain = 0.8f;
    const auto emitter = runtime.CreateEmitterHandle(desc);
    ok &= Expect(emitter.HasValue(), "emitter should create for voice-desc test");
    ok &= Expect(runtime.Play(emitter.Value()).HasValue(), "emitter should play for voice-desc test");
    ok &= Expect(backend->last_voice_desc.loop, "loop flag should reach backend voice desc");
    ok &= Expect(backend->last_voice_desc.mixer_group == MixerGroupId{7}, "mixer group should reach backend voice desc");
    ok &= Expect(backend->last_voice_desc.clip.resource != nullptr, "clip resource should reach backend voice desc");
    ok &= Expect(backend->last_voice_desc.clip.resource != nullptr &&
                     !backend->last_voice_desc.clip.resource->GetEncodedData().empty(),
                 "clip resource should expose backend-readable data");
    ok &= Expect(backend->last_gain > 0.39f && backend->last_gain < 0.41f, "mixer gain should affect voice gain");

    const auto listener = runtime.CreateListenerHandle(AudioListenerDesc{AudioTransformId{44}});
    ok &= Expect(listener.HasValue(), "listener should create through backend");
    ok &= Expect(backend->backend_listeners_created == 1, "backend listener should be created");
    ok &= Expect(runtime.SetMainListener(listener.Value()).HasValue(), "main listener should be selected");
    ok &= Expect(backend->backend_listener_updates == 1, "main listener transform should reach backend");
    ok &= Expect(runtime.DestroyListener(listener.Value()).HasValue(), "listener should destroy through backend");
    ok &= Expect(backend->backend_listeners_destroyed == 1, "backend listener should be destroyed");

    ok &= Expect(runtime.SubmitOneShot(AudioEvent{SoundId{1}, Vec3{1.0f, 2.0f, 3.0f}, 0.25f}).HasValue(),
                 "one-shot should queue");
    const int created_before_tick = backend->created;
    const int played_before_tick = backend->played;
    ok &= Expect(runtime.Tick(RuntimeFrameDuration{std::chrono::microseconds{1}}).HasValue(), "tick should consume one-shot");
    ok &= Expect(runtime.Events().empty(), "one-shot queue should be cleared after tick");
    ok &= Expect(backend->created == created_before_tick + 1 && backend->played == played_before_tick + 1,
                 "one-shot should create and play a backend voice");
    ok &= Expect(!backend->last_voice_desc.loop && backend->last_voice_desc.initial_gain == 0.25f,
                 "one-shot voice desc should carry non-looping volume");
    backend->finished_voices.push_back(backend->last_created_voice);
    const int destroyed_before_cleanup = backend->destroyed;
    ok &= Expect(runtime.Tick(RuntimeFrameDuration{std::chrono::microseconds{1}}).HasValue(), "tick should cleanup completed one-shot");
    ok &= Expect(backend->destroyed == destroyed_before_cleanup + 1, "completed one-shot should destroy backend voice during tick");

    ok &= Expect(runtime.DestroyEmitter(emitter.Value()).HasValue(), "emitter voice should destroy before one-shot shutdown check");
    const int destroyed_before_shutdown = backend->destroyed;
    ok &= Expect(runtime.Shutdown().HasValue(), "shutdown should release one-shot voices");
    ok &= Expect(backend->destroyed == destroyed_before_shutdown, "shutdown should not redestroy completed one-shot voice");
    return ok;
}

bool TestPlayRollbackDestroyFailureVirtualizeFadeAndShutdown()
{
    auto backend = std::make_shared<TestBackend>();
    auto resources = std::make_shared<TestResourceSource>();
    auto transforms = std::make_shared<TestTransformSource>();
    AudioRuntime runtime{AudioOptions{.enable_mock_backend = false}, AudioDependencies{backend, resources, transforms}};

    const auto emitter = runtime.CreateEmitterHandle(MakeEmitterDesc());
    bool ok = Expect(emitter.HasValue(), "rollback emitter should create");
    backend->fail_spatial = true;
    const auto failed_spatial = runtime.Play(emitter.Value());
    ok &= Expect(!failed_spatial.HasValue() && failed_spatial.GetError().HasCode("audio.backend_failed"),
                 "spatial failure should propagate");
    ok &= Expect(backend->created == 1 && backend->destroyed == 1, "partial play should destroy newly-created voice");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()).Value() == EmitterState::Stopped, "partial play should leave emitter stopped");

    backend->fail_spatial = false;
    ok &= Expect(runtime.Play(emitter.Value()).HasValue(), "play should retry after rollback");
    backend->fail_destroy = true;
    ok &= Expect(!runtime.DestroyEmitter(emitter.Value()).HasValue(), "destroy backend failure should propagate");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()).Value() == EmitterState::Playing, "failed destroy should keep emitter record");
    backend->fail_destroy = false;

    ok &= Expect(runtime.Virtualize(emitter.Value()).HasValue(), "virtualize should release backend voice");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()).Value() == EmitterState::Virtualized, "emitter should become virtualized");
    const int destroyed_after_virtualize = backend->destroyed;
    ok &= Expect(runtime.Play(emitter.Value()).HasValue(), "virtualized emitter should restore playback");
    ok &= Expect(backend->created >= 3, "restored virtualized emitter should create a new voice");

    ok &= Expect(runtime.FadeOut(emitter.Value(), RuntimeFrameDuration{std::chrono::microseconds{2}}).HasValue(),
                 "fade should start");
    ok &= Expect(runtime.Tick(RuntimeFrameDuration{std::chrono::microseconds{2}}).HasValue(), "fade should complete");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()).Value() == EmitterState::Stopped, "completed fade should stop emitter state");
    ok &= Expect(backend->stopped > 0, "completed fade should stop backend voice");
    ok &= Expect(runtime.Play(emitter.Value()).HasValue(), "play after fade-out should restart audible playback");
    ok &= Expect(backend->last_gain > 0.99f, "fade-out should not destroy emitter base gain");
    ok &= Expect(runtime.Pause(emitter.Value()).HasValue(), "playing emitter should pause");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()).Value() == EmitterState::Paused, "paused state should be reachable");
    ok &= Expect(runtime.Resume(emitter.Value()).HasValue(), "paused emitter should resume");
    ok &= Expect(runtime.FadeIn(emitter.Value(), RuntimeFrameDuration{std::chrono::microseconds{2}}).HasValue(), "fade-in should be reachable");
    ok &= Expect(runtime.Tick(RuntimeFrameDuration{std::chrono::microseconds{2}}).HasValue(), "fade-in should complete");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()).Value() == EmitterState::Playing, "fade-in should return to playing");

    const auto shutdown_emitter = runtime.CreateEmitterHandle(MakeEmitterDesc());
    ok &= Expect(shutdown_emitter.HasValue(), "shutdown emitter should create");
    ok &= Expect(runtime.Play(shutdown_emitter.Value()).HasValue(), "shutdown emitter should play");
    const int destroyed_before_shutdown = backend->destroyed;
    ok &= Expect(runtime.Shutdown().HasValue(), "shutdown should release active backend voices");
    ok &= Expect(backend->destroyed > destroyed_before_shutdown && backend->destroyed >= destroyed_after_virtualize,
                 "shutdown should destroy tracked backend voices");
    ok &= Expect(!runtime.GetEmitterState(shutdown_emitter.Value()).HasValue(),
                 "shutdown should clear emitter records");
    return ok;
}

bool TestFadeEdgesPauseResumeAndRollbackCleanup()
{
    auto backend = std::make_shared<TestBackend>();
    auto resources = std::make_shared<TestResourceSource>();
    auto transforms = std::make_shared<TestTransformSource>();
    AudioRuntime runtime{AudioOptions{.enable_mock_backend = false}, AudioDependencies{backend, resources, transforms}};

    const auto emitter = runtime.CreateEmitterHandle(MakeEmitterDesc());
    bool ok = Expect(emitter.HasValue(), "fade edge emitter should create");
    ok &= Expect(runtime.Play(emitter.Value()).HasValue(), "fade edge emitter should play");
    ok &= Expect(runtime.FadeIn(emitter.Value(), RuntimeFrameDuration{std::chrono::microseconds{4}}).HasValue(),
                 "fade-in from active playback should start");
    ok &= Expect(backend->last_gain == 0.0f, "fade-in should synchronize zero gain before tick");
    ok &= Expect(runtime.Pause(emitter.Value()).HasValue(), "fading-in emitter should pause");
    ok &= Expect(runtime.Resume(emitter.Value()).HasValue(), "fading-in emitter should resume");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()).Value() == EmitterState::FadingIn,
                 "resume should restore fading-in state");
    ok &= Expect(runtime.Tick(RuntimeFrameDuration{std::chrono::microseconds{4}}).HasValue(), "fade-in should finish after resume");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()).Value() == EmitterState::Playing,
                 "fade-in should complete to playing");

    ok &= Expect(runtime.FadeOut(emitter.Value(), RuntimeFrameDuration{std::chrono::microseconds{4}}).HasValue(),
                 "fade-out should start");
    ok &= Expect(runtime.Tick(RuntimeFrameDuration{std::chrono::microseconds{2}}).HasValue(), "fade-out should progress");
    ok &= Expect(runtime.Pause(emitter.Value()).HasValue(), "fading-out emitter should pause");
    ok &= Expect(runtime.Resume(emitter.Value()).HasValue(), "fading-out emitter should resume");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()).Value() == EmitterState::FadingOut,
                 "resume should restore fading-out state");
    ok &= Expect(runtime.Play(emitter.Value()).HasValue(), "explicit play should cancel previous fade");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()).Value() == EmitterState::Playing && backend->last_gain > 0.99f,
                 "play during fade-out should restore full effective gain");

    ok &= Expect(runtime.FadeOut(emitter.Value(), RuntimeFrameDuration{}).HasValue(), "zero fade-out should complete synchronously");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()).Value() == EmitterState::Stopped,
                 "zero fade-out should stop immediately");
    ok &= Expect(runtime.FadeIn(emitter.Value(), RuntimeFrameDuration{}).HasValue(), "zero fade-in should complete synchronously");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()).Value() == EmitterState::Playing && backend->last_gain > 0.99f,
                 "zero fade-in should play immediately at full gain");

    const auto rollback = runtime.CreateEmitterHandle(MakeEmitterDesc());
    ok &= Expect(rollback.HasValue(), "rollback cleanup emitter should create");
    backend->fail_spatial = true;
    backend->fail_destroy = true;
    const auto failed_play = runtime.Play(rollback.Value());
    ok &= Expect(!failed_play.HasValue() && failed_play.GetError().HasCode("audio.backend_failed"),
                 "setup failure should report backend error");
    const int destroyed_before_retry = backend->destroyed;
    backend->fail_spatial = false;
    backend->fail_destroy = false;
    ok &= Expect(runtime.Tick(RuntimeFrameDuration{std::chrono::microseconds{1}}).HasValue(),
                 "pending voice cleanup should retry on tick");
    ok &= Expect(backend->destroyed == destroyed_before_retry + 1,
                 "failed rollback voice should be destroyed by retryable cleanup");
    return ok;
}

bool TestRetryableListenerShutdownAndStreamReadContract()
{
    auto backend = std::make_shared<TestBackend>();
    auto resources = std::make_shared<TestResourceSource>();
    auto transforms = std::make_shared<TestTransformSource>();
    AudioRuntime runtime{AudioOptions{.enable_mock_backend = false}, AudioDependencies{backend, resources, transforms}};

    const auto first = runtime.CreateListenerHandle(AudioListenerDesc{AudioTransformId{44}});
    const auto second = runtime.CreateListenerHandle(AudioListenerDesc{AudioTransformId{45}});
    bool ok = Expect(first.HasValue() && second.HasValue(), "shutdown listeners should create");
    ok &= Expect(runtime.SetMainListener(first.Value()).HasValue(), "first listener should become main before shutdown");
    backend->fail_destroy_listener_once = true;
    const auto failed_shutdown = runtime.Shutdown();
    ok &= Expect(!failed_shutdown.HasValue() && failed_shutdown.GetError().HasCode("audio.backend_failed"),
                 "listener destroy failure should make shutdown retryable");
    ok &= Expect(!runtime.CreateEmitterHandle(MakeEmitterDesc()).HasValue(),
                 "failed shutdown should immediately reject new emitters");
    ok &= Expect(!runtime.SubmitOneShot(AudioEvent{SoundId{1}, Vec3{}, 1.0f}).HasValue(),
                 "failed shutdown should immediately reject one-shots");
    ok &= Expect(backend->backend_listeners_destroyed == 1,
                 "shutdown should erase successfully destroyed listeners before returning failure");
    const int attempts_after_failure = backend->backend_listener_destroy_attempts;
    const auto retried_shutdown = runtime.Shutdown();
    ok &= Expect(retried_shutdown.HasValue(), "second shutdown should retry remaining listener");
    ok &= Expect(backend->backend_listener_destroy_attempts == attempts_after_failure + 1,
                 "second shutdown should not redestroy already-erased listener");
    ok &= Expect(!runtime.GetMainListener().has_value(), "main listener should clear after retryable shutdown completes");

    TestStreamSource stream{};
    std::array<std::byte, 4> buffer{};
    stream.next_read = 2;
    stream.next_eof = false;
    const auto short_read = stream.Read(0, buffer);
    ok &= Expect(short_read.HasValue() && short_read.Value().bytes_read == 2 && !short_read.Value().end_of_stream,
                 "stream source should report short reads explicitly");
    stream.next_read = 0;
    stream.next_eof = true;
    const auto eof = stream.Read(4, buffer);
    ok &= Expect(eof.HasValue() && eof.Value().end_of_stream, "stream source should report EOF explicitly");
    stream.next_read = buffer.size() + 1;
    const auto invalid = stream.Read(0, buffer);
    ok &= Expect(!invalid.HasValue() && invalid.GetError().HasCode("audio.invalid_stream_read"),
                 "stream source should reject reads larger than output buffer");
    return ok;
}

bool TestPostShutdownOperationsRejected()
{
    AudioRuntime runtime{{}};
    bool ok = Expect(SeedSound(runtime), "post-shutdown sound should seed");
    const auto listener = runtime.CreateListenerHandle(AudioListenerDesc{AudioTransformId{44}});
    ok &= Expect(listener.HasValue(), "post-shutdown listener should create before shutdown");
    ok &= Expect(runtime.SubmitOneShot(AudioEvent{SoundId{1}, Vec3{}, 1.0f}).HasValue(),
                 "post-shutdown one-shot should queue before shutdown");
    ok &= Expect(runtime.Shutdown().HasValue(), "shutdown should succeed");
    ok &= Expect(runtime.Shutdown().HasValue(), "second shutdown should be idempotent");
    ok &= Expect(!runtime.RegisterSound(SoundDesc{SoundId{2}, SoundState::Ready}).HasValue(),
                 "register sound should reject after shutdown");
    ok &= Expect(!runtime.CreateEmitterHandle(MakeEmitterDesc()).HasValue(),
                 "create emitter should reject after shutdown");
    ok &= Expect(!runtime.CreateListenerHandle(AudioListenerDesc{AudioTransformId{45}}).HasValue(),
                 "create listener should reject after shutdown");
    ok &= Expect(!runtime.SubmitOneShot(AudioEvent{SoundId{1}, Vec3{}, 1.0f}).HasValue(),
                 "submit one-shot should reject after shutdown");
    ok &= Expect(!runtime.Tick(RuntimeFrameDuration{std::chrono::microseconds{1}}).HasValue(),
                 "tick should reject after shutdown");
    return ok;
}

bool TestAllocatorOverflowDoesNotMutateState()
{
    AudioRuntime runtime{AudioOptions{.enable_mock_backend = true}};
    bool ok = Expect(SeedSound(runtime), "allocator overflow test sound should register");

    runtime.SetAllocatorStateForTesting(std::numeric_limits<std::uint64_t>::max(), 1, 1, 1, 1);
    const auto emitter_value_overflow = runtime.CreateEmitterHandle(MakeEmitterDesc());
    ok &= Expect(!emitter_value_overflow.HasValue() &&
                     emitter_value_overflow.GetError().HasCode("audio.emitter_id_overflow"),
                 "emitter value overflow should fail");
    ok &= Expect(!runtime.GetEmitterState(AudioEmitterHandle{AudioEmitterId{std::numeric_limits<std::uint64_t>::max()}, 1}).HasValue(),
                 "emitter value overflow should not insert a record");

    runtime.SetAllocatorStateForTesting(1, std::numeric_limits<std::uint32_t>::max(), 1, 1, 1);
    const auto emitter_generation_overflow = runtime.CreateEmitterHandle(MakeEmitterDesc());
    ok &= Expect(!emitter_generation_overflow.HasValue() &&
                     emitter_generation_overflow.GetError().HasCode("audio.emitter_id_overflow"),
                 "emitter generation overflow should fail");
    ok &= Expect(!runtime.GetEmitterState(AudioEmitterHandle{AudioEmitterId{1}, std::numeric_limits<std::uint32_t>::max()}).HasValue(),
                 "emitter generation overflow should not insert a record");

    runtime.SetAllocatorStateForTesting(1, 1, std::numeric_limits<std::uint64_t>::max(), 1, 1);
    const auto listener_value_overflow = runtime.CreateListenerHandle(AudioListenerDesc{AudioTransformId{4}});
    ok &= Expect(!listener_value_overflow.HasValue() &&
                     listener_value_overflow.GetError().HasCode("audio.listener_id_overflow"),
                 "listener value overflow should fail");

    runtime.SetAllocatorStateForTesting(1, 1, 1, std::numeric_limits<std::uint32_t>::max(), 1);
    const auto listener_generation_overflow = runtime.CreateListenerHandle(AudioListenerDesc{AudioTransformId{4}});
    ok &= Expect(!listener_generation_overflow.HasValue() &&
                     listener_generation_overflow.GetError().HasCode("audio.listener_id_overflow"),
                 "listener generation overflow should fail");

    runtime.SetAllocatorStateForTesting(1, 1, 1, 1, std::numeric_limits<std::uint64_t>::max());
    AudioVoiceDesc voice_desc{};
    voice_desc.clip = AudioClipPayload{SoundId{1}, SoundState::Ready, std::make_shared<TestClipResource>()};
    voice_desc.initial_gain = 0.5f;
    const auto voice_overflow = runtime.CreateVoice(voice_desc);
    ok &= Expect(!voice_overflow.HasValue() &&
                     voice_overflow.GetError().HasCode("audio.voice_id_overflow"),
                 "voice overflow should fail before inserting mock voice");
    ok &= Expect(!runtime.Play(BackendVoiceHandle{std::numeric_limits<std::uint64_t>::max()}).HasValue(),
                 "voice overflow should not insert a backend voice");
    return ok;
}

bool TestMainListenerSelectionIsAtomic()
{
    auto backend = std::make_shared<TestBackend>();
    auto resources = std::make_shared<TestResourceSource>();
    auto transforms = std::make_shared<TestTransformSource>();
    AudioRuntime runtime{AudioOptions{.enable_mock_backend = false}, AudioDependencies{backend, resources, transforms}};
    const auto first = runtime.CreateListenerHandle(AudioListenerDesc{AudioTransformId{44}});
    const auto second = runtime.CreateListenerHandle(AudioListenerDesc{AudioTransformId{45}});
    bool ok = Expect(first.HasValue() && second.HasValue(), "listeners should create");
    ok &= Expect(runtime.SetMainListener(first.Value()).HasValue(), "first listener should become main");
    backend->fail_listener_update = true;
    const auto failed = runtime.SetMainListener(second.Value());
    ok &= Expect(!failed.HasValue() && failed.GetError().HasCode("audio.backend_failed"), "backend listener failure should propagate");
    ok &= Expect(runtime.GetMainListener().has_value() && *runtime.GetMainListener() == first.Value(),
                 "failed SetMainListener should keep previous main listener");
    return ok;
}
} // namespace

int main()
{
    static_assert(std::is_abstract_v<IAudioBackend>);
    static_assert(std::is_abstract_v<IAudioResourceSource>);
    static_assert(std::is_abstract_v<IAudioTransformSource>);

    bool ok = true;
    ok &= TestCreatePlayStopDestroyEmitter();
    ok &= TestListenerCreationAndMainListener();
    ok &= TestInvalidSoundBehavior();
    ok &= TestFadeVirtualizedMixerAndEvents();
    ok &= TestValidationFailures();
    ok &= TestBackendContractsHandlesBoundsAndHierarchy();
    ok &= TestProductionFactoryBackendRequiredAndFailurePropagates();
    ok &= TestListenerGenerationAndUnknownErrors();
    ok &= TestFadeProgressTickAndAttachedTransformUpdate();
    ok &= TestMixerCycleAndQueueOverflowPolicies();
    ok &= TestBackendVoiceDescOneShotListenerAndMixerGain();
    ok &= TestPlayRollbackDestroyFailureVirtualizeFadeAndShutdown();
    ok &= TestFadeEdgesPauseResumeAndRollbackCleanup();
    ok &= TestRetryableListenerShutdownAndStreamReadContract();
    ok &= TestPostShutdownOperationsRejected();
    ok &= TestAllocatorOverflowDoesNotMutateState();
    ok &= TestMainListenerSelectionIsAtomic();
    return ok ? 0 : 1;
}
