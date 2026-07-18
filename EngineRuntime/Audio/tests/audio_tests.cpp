#include "audio_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <iostream>
#include <memory>
#include <string_view>
#include <type_traits>

using epidemic::runtime::GameDuration;
using epidemic::runtime::RuntimeObjectId;
using epidemic::runtime::Transform;
using epidemic::runtime::Vec3;
using epidemic::runtime::audio::AudioBackendOptions;
using epidemic::runtime::audio::AudioClipPayload;
using epidemic::runtime::audio::AudioDependencies;
using epidemic::runtime::audio::CreateMockAudioServices;
using epidemic::runtime::audio::CreateAudioServices;
using epidemic::runtime::audio::AudioEventOverflowPolicy;
using epidemic::runtime::audio::AudioListenerHandle;
using epidemic::runtime::audio::AudioSpatialState;
using epidemic::runtime::audio::IAudioBackend;
using epidemic::runtime::audio::IAudioResourceSource;
using epidemic::runtime::audio::IAudioTransformSource;
using epidemic::runtime::audio::AudioEmitterDesc;
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
    bool fail_update = false;
    std::uint64_t next_voice = 1;
    int created = 0;
    int played = 0;
    int stopped = 0;
    int destroyed = 0;
    int spatial_updates = 0;
    int gain_updates = 0;
    float last_gain = 1.0f;
    AudioSpatialState last_spatial{};

    bool IsEnabled() const override { return enabled; }

    epidemic::foundation::Result<void> Initialize(const AudioBackendOptions& options) override
    {
        enabled = options.enabled;
        return epidemic::foundation::Result<void>::Success();
    }

    epidemic::foundation::Result<BackendVoiceHandle> CreateVoice(const AudioClipPayload& payload) override
    {
        if (fail_create)
        {
            return epidemic::foundation::Result<BackendVoiceHandle>::Failure(
                epidemic::foundation::Error::Create("audio.backend_failed", "test backend create failed"));
        }
        if (payload.state != SoundState::Ready)
        {
            return epidemic::foundation::Result<BackendVoiceHandle>::Failure(
                epidemic::foundation::Error::Create("audio.sound_not_ready", "test payload not ready"));
        }
        ++created;
        return epidemic::foundation::Result<BackendVoiceHandle>::Success(BackendVoiceHandle{next_voice++});
    }

    epidemic::foundation::Result<void> DestroyVoice(BackendVoiceHandle) override
    {
        ++destroyed;
        return epidemic::foundation::Result<void>::Success();
    }

    epidemic::foundation::Result<void> Play(BackendVoiceHandle) override
    {
        ++played;
        return epidemic::foundation::Result<void>::Success();
    }

    epidemic::foundation::Result<void> Pause(BackendVoiceHandle) override { return epidemic::foundation::Result<void>::Success(); }

    epidemic::foundation::Result<void> Stop(BackendVoiceHandle) override
    {
        ++stopped;
        return epidemic::foundation::Result<void>::Success();
    }

    epidemic::foundation::Result<void> SetGain(BackendVoiceHandle, float gain) override
    {
        ++gain_updates;
        last_gain = gain;
        return epidemic::foundation::Result<void>::Success();
    }

    epidemic::foundation::Result<void> SetSpatialState(BackendVoiceHandle, const AudioSpatialState& state) override
    {
        ++spatial_updates;
        last_spatial = state;
        return epidemic::foundation::Result<void>::Success();
    }

    epidemic::foundation::Result<void> Update(GameDuration) override
    {
        if (fail_update)
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("audio.backend_failed", "test backend update failed"));
        }
        return epidemic::foundation::Result<void>::Success();
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
        return epidemic::foundation::Result<AudioClipPayload>::Success(AudioClipPayload{id, state});
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
    const auto emitter = runtime.CreateEmitter(MakeEmitterDesc());
    ok &= Expect(emitter.HasValue(), "valid emitter should be created");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()) == EmitterState::Stopped, "new emitter should be stopped");
    ok &= Expect(runtime.GetEmitterSnapshot(emitter.Value()).revision == 1, "new emitter snapshot should start at revision one");
    ok &= Expect(runtime.Play(emitter.Value()).HasValue(), "ready sound should play");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()) == EmitterState::Playing, "emitter should be playing");
    ok &= Expect(runtime.GetEmitterSnapshot(emitter.Value()).revision == 2, "play should increment emitter revision");
    ok &= Expect(runtime.Stop(emitter.Value()).HasValue(), "playing emitter should stop");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()) == EmitterState::Stopped, "emitter should return stopped");
    ok &= Expect(runtime.GetEmitterSnapshot(emitter.Value()).revision == 3, "stop should increment emitter revision");
    ok &= Expect(runtime.Stop(emitter.Value()).HasValue(), "idempotent stop should succeed");
    ok &= Expect(runtime.GetEmitterSnapshot(emitter.Value()).revision == 3, "idempotent stop should not increment revision");
    ok &= Expect(runtime.DestroyEmitter(emitter.Value()).HasValue(), "emitter should destroy");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()) == EmitterState::Destroyed, "destroyed emitter should read destroyed");
    ok &= Expect(runtime.GetEmitterSnapshot(emitter.Value()).revision == 0, "destroyed emitter snapshot should be revision zero");
    return ok;
}

bool TestListenerCreationAndMainListener()
{
    AudioRuntime runtime{{}};
    const auto listener = runtime.CreateListener(AudioListenerDesc{AudioTransformId{12}});
    bool ok = Expect(listener.HasValue(), "valid listener should be created");
    ok &= Expect(runtime.SetMainListener(listener.Value()).HasValue(), "main listener should be set");
    ok &= Expect(runtime.GetMainListener().has_value(), "main listener should be readable");
    ok &= Expect(runtime.GetMainListener().has_value() && runtime.GetMainListener()->value == listener.Value().value, "main listener id should match");
    ok &= Expect(!runtime.CreateListener(AudioListenerDesc{}).HasValue(), "invalid listener transform should fail");
    return ok;
}

bool TestInvalidSoundBehavior()
{
    AudioRuntime runtime{{}};
    bool ok = Expect(SeedSound(runtime, SoundState::Loading), "loading sound should seed");
    const auto emitter = runtime.CreateEmitter(MakeEmitterDesc());
    ok &= Expect(emitter.HasValue(), "emitter can reference registered loading sound");
    ok &= Expect(!runtime.Play(emitter.Value()).HasValue(), "loading sound should not play");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()) == EmitterState::Stopped, "failed play should leave stopped state");
    ok &= Expect(!runtime.SubmitOneShot(AudioEvent{SoundId{99}, Vec3{}, 1.0f}).HasValue(), "missing one-shot sound should fail");
    return ok;
}

bool TestFadeVirtualizedMixerAndEvents()
{
    AudioRuntime runtime{{}};
    bool ok = Expect(SeedSound(runtime), "sound should seed");
    const auto emitter = runtime.CreateEmitter(MakeEmitterDesc());
    ok &= Expect(emitter.HasValue(), "valid emitter should be created");
    ok &= Expect(runtime.FadeOut(emitter.Value()).HasValue(), "emitter should fade out");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()) == EmitterState::FadingOut, "emitter should report fade-out state");
    ok &= Expect(runtime.Virtualize(emitter.Value()).HasValue(), "emitter should virtualize");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()) == EmitterState::Virtualized, "emitter should report virtualized state");
    ok &= Expect(runtime.SubmitOneShot(AudioEvent{SoundId{1}, Vec3{1.0f, 2.0f, 3.0f}, 0.75f}).HasValue(), "one-shot should queue");
    ok &= Expect(runtime.Events().size() == 1, "event queue should contain one event");
    runtime.Clear();
    ok &= Expect(runtime.Events().empty(), "event queue should clear");
    ok &= Expect(runtime.SetMixerGroup(MixerGroupState{MixerGroupId{3}, 0.5f, MixerFadeState::FadingOut}).HasValue(), "mixer group should update");
    const auto group = runtime.GetMixerGroup(MixerGroupId{3});
    ok &= Expect(group.has_value() && group->fade_state == MixerFadeState::FadingOut, "mixer fade state should be stored");
    return ok;
}

bool TestValidationFailures()
{
    AudioRuntime runtime{{}};
    bool ok = Expect(!runtime.RegisterSound(SoundDesc{}).HasValue(), "invalid sound id should fail");
    ok &= Expect(!runtime.CreateEmitter(AudioEmitterDesc{}).HasValue(), "empty emitter desc should fail");
    ok &= Expect(!runtime.SetMainListener(epidemic::runtime::audio::AudioListenerId{99}).HasValue(), "unknown listener should fail main selection");
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
    ok &= Expect(runtime.FadeOut(handle.Value(), GameDuration{5}).HasValue(), "handle fade should succeed");
    const auto snapshot = runtime.GetEmitterSnapshot(handle.Value());
    ok &= Expect(snapshot.handle == handle.Value(), "snapshot should preserve emitter handle");
    ok &= Expect(snapshot.fade_duration.ticks == 5, "snapshot should expose fade duration");

    ok &= Expect(runtime.SubmitOneShot(AudioEvent{SoundId{1}, Vec3{}, 1.0f}).HasValue(), "first event should queue");
    ok &= Expect(!runtime.SubmitOneShot(AudioEvent{SoundId{1}, Vec3{}, 1.0f}).HasValue(), "bounded queue should reject overflow");

    const auto listener = runtime.CreateListener(AudioListenerDesc{AudioTransformId{44}});
    ok &= Expect(listener.HasValue(), "listener should be created");
    ok &= Expect(runtime.SetMainListener(listener.Value()).HasValue(), "listener should become main");
    ok &= Expect(runtime.DestroyListener(listener.Value()).HasValue(), "listener should destroy");
    ok &= Expect(!runtime.GetMainListener().has_value(), "destroyed main listener should clear selection");

    ok &= Expect(runtime.SetMixerGroup(MixerGroupState{MixerGroupId{3}, 1.0f, MixerFadeState::Stable}).HasValue(),
                 "parent mixer group should exist");
    ok &= Expect(runtime.SetMixerGroup(MixerGroupState{MixerGroupId{4}, 1.0f, MixerFadeState::FadingIn, MixerGroupId{3}, GameDuration{2}, 0.5f}).HasValue(),
                 "hierarchical mixer group should update");
    const auto mixer = runtime.GetMixerGroup(MixerGroupId{4});
    ok &= Expect(mixer.has_value() && mixer->parent == MixerGroupId{3} && mixer->fade_progress == 0.5f,
                 "mixer hierarchy and fade progress should persist");

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

    const auto emitter = services.Value().runtime->CreateEmitter(MakeEmitterDesc());
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
    ok &= Expect(!runtime.DestroyEmitter(AudioEmitterId{99}).HasValue(), "unknown emitter should fail destruction");
    ok &= Expect(!runtime.DestroyListener(AudioListenerId{99}).HasValue(), "unknown listener should fail destruction");
    return ok;
}

bool TestFadeProgressTickAndAttachedTransformUpdate()
{
    auto backend = std::make_shared<TestBackend>();
    auto resources = std::make_shared<TestResourceSource>();
    auto transforms = std::make_shared<TestTransformSource>();
    transforms->transform.position = Vec3{4.0f, 5.0f, 6.0f};
    AudioRuntime runtime{AudioOptions{.enable_mock_backend = false}, AudioDependencies{backend, resources, transforms}};

    const auto emitter = runtime.CreateEmitter(MakeEmitterDesc());
    bool ok = Expect(emitter.HasValue(), "attached emitter should create");
    ok &= Expect(runtime.Play(emitter.Value()).HasValue(), "attached emitter should play through backend");
    ok &= Expect(backend->spatial_updates > 0 && backend->last_spatial.transform.position == transforms->transform.position,
                 "play should publish attached transform to backend");
    const auto handle = runtime.CreateEmitterHandle(MakeEmitterDesc());
    ok &= Expect(handle.HasValue(), "second emitter handle should create");
    ok &= Expect(runtime.Play(handle.Value().id).HasValue(), "second emitter should play");
    ok &= Expect(runtime.FadeOut(handle.Value(), GameDuration{4}).HasValue(), "fade-out should start");
    ok &= Expect(runtime.Tick(GameDuration{2}).HasValue(), "audio tick should advance fade");
    const auto snapshot = runtime.GetEmitterSnapshot(handle.Value());
    ok &= Expect(snapshot.fade_progress > 0.49f && snapshot.fade_progress < 0.51f, "fade should advance halfway");
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
    return ok ? 0 : 1;
}
