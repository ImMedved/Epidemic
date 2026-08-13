#include "Epidemic/Runtime/Support/runtime_support.h"

#include "Epidemic/Runtime/Resources/resource_loader.h"
#include "Epidemic/Runtime/Resources/resource_payload.h"

#include <Epidemic/Core/application.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using epidemic::core::Application;
using namespace epidemic::runtime;

bool Expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }
    return condition;
}

class EmptyChunkManifestSource final : public IChunkStreamingManifestSource
{
  public:
    [[nodiscard]] epidemic::foundation::Result<ChunkStreamingManifest> GetManifest(ChunkId chunk) const override
    {
        return epidemic::foundation::Result<ChunkStreamingManifest>::Success(ChunkStreamingManifest{chunk, {}, {}});
    }
};

class FailingChunkManifestSource final : public IChunkStreamingManifestSource
{
  public:
    [[nodiscard]] epidemic::foundation::Result<ChunkStreamingManifest> GetManifest(ChunkId) const override
    {
        return epidemic::foundation::Result<ChunkStreamingManifest>::Failure(
            epidemic::foundation::Error::Create("integration.manifest_failure", "manifest failed on purpose"));
    }
};

class ManifestSource final : public IChunkStreamingManifestSource
{
  public:
    ChunkStreamingManifest manifest{};

    [[nodiscard]] epidemic::foundation::Result<ChunkStreamingManifest> GetManifest(ChunkId chunk) const override
    {
        if (chunk != manifest.chunk)
        {
            return epidemic::foundation::Result<ChunkStreamingManifest>::Failure(
                epidemic::foundation::Error::Create("integration.manifest_not_found", "manifest not found"));
        }
        return epidemic::foundation::Result<ChunkStreamingManifest>::Success(manifest);
    }
};

class TestResourceLoader final : public IResourceLoader
{
  public:
    explicit TestResourceLoader(ResourceType type) : type_(type) {}

    [[nodiscard]] ResourceType GetResourceType() const override { return type_; }

    [[nodiscard]] epidemic::foundation::Result<ResourceLoadArtifact> Load(ResourceRequest request) override
    {
        return epidemic::foundation::Result<ResourceLoadArtifact>::Success(
            ResourceLoadArtifact{request.resource_id,
                                 request.type,
                                 std::make_shared<ByteResourcePayload>(std::vector<std::byte>{std::byte{0x2a}}),
                                 {}});
    }

  private:
    ResourceType type_{};
};

class TestMesh final : public renderer::IRenderMeshResource
{
};

class TestMaterial final : public renderer::IRenderMaterialResource
{
};

class ReadyRenderBridge final : public renderer::IRenderResourceBridge
{
  public:
    [[nodiscard]] epidemic::foundation::Result<void> AcquirePayloads(ResourceId, ResourceId) override
    {
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] epidemic::foundation::Result<void> ReleasePayloads(ResourceId, ResourceId) override
    {
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] epidemic::foundation::Result<renderer::RenderResourcePayloads>
        GetPayloads(ResourceId, ResourceId) const override
    {
        return epidemic::foundation::Result<renderer::RenderResourcePayloads>::Success(
            renderer::RenderResourcePayloads{std::make_shared<TestMesh>(), std::make_shared<TestMaterial>()});
    }
};

class RecordingRenderSink final : public renderer::IRenderCommandSink
{
  public:
    [[nodiscard]] epidemic::foundation::Result<void> BeginFrame(const renderer::RenderFrameContext&) override
    {
        submissions.clear();
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] epidemic::foundation::Result<void>
        SubmitProxy(const renderer::RenderProxySubmission& submission) override
    {
        submissions.push_back(submission);
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] epidemic::foundation::Result<void> EndFrame() override
    {
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] epidemic::foundation::Result<void> AbortFrame() override
    {
        return epidemic::foundation::Result<void>::Success();
    }

    std::vector<renderer::RenderProxySubmission> submissions;
};


class TestAudioPayload final : public IResourcePayload,
                               public audio::IAudioClipResource
{
  public:
    [[nodiscard]] std::size_t GetSizeBytes() const noexcept override { return bytes_.size(); }

    [[nodiscard]] audio::AudioClipFormat GetFormat() const override
    {
        return audio::AudioClipFormat{2, 48000, 16, false};
    }

    [[nodiscard]] audio::AudioClipStorage GetStorage() const override
    {
        return audio::AudioClipStorage::InMemoryEncoded;
    }

    [[nodiscard]] std::span<const std::byte> GetEncodedData() const override { return bytes_; }

    [[nodiscard]] std::shared_ptr<audio::IAudioStreamSource> GetStreamSource() const override { return {}; }

  private:
    std::vector<std::byte> bytes_{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}};
};

class TestAudioLoader final : public IResourceLoader
{
  public:
    explicit TestAudioLoader(ResourceType type) : type_(type) {}

    [[nodiscard]] ResourceType GetResourceType() const override { return type_; }

    [[nodiscard]] epidemic::foundation::Result<ResourceLoadArtifact> Load(ResourceRequest request) override
    {
        return epidemic::foundation::Result<ResourceLoadArtifact>::Success(
            ResourceLoadArtifact{request.resource_id, request.type, std::make_shared<TestAudioPayload>(), {}});
    }

  private:
    ResourceType type_{};
};

class TestAudioMapper final : public IAudioResourceMapper
{
  public:
    TestAudioMapper(audio::SoundId sound, ResourceId resource, ResourceType type)
        : sound_(sound), resource_(resource), type_(type)
    {
    }

    [[nodiscard]] epidemic::foundation::Result<ResourceRequest> ResolveSound(audio::SoundId id) const override
    {
        if (id != sound_)
        {
            return epidemic::foundation::Result<ResourceRequest>::Failure(
                epidemic::foundation::Error::Create("integration.sound_not_found", "sound mapping not found"));
        }
        return epidemic::foundation::Result<ResourceRequest>::Success(ResourceRequest{resource_, type_});
    }

  private:
    audio::SoundId sound_{};
    ResourceId resource_{};
    ResourceType type_{};
};

class FlakyEventSink final : public IRuntimeEventSink
{
  public:
    [[nodiscard]] epidemic::foundation::Result<void> Publish(const RuntimeFrameEvents& events) override
    {
        ++calls;
        if (fail_first)
        {
            fail_first = false;
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("integration.event_publish_failure", "event sink failed on purpose"));
        }
        published_animation_events += events.animation_events.size();
        return epidemic::foundation::Result<void>::Success();
    }

    bool fail_first = true;
    int calls = 0;
    std::size_t published_animation_events = 0;
};

class FlakyStreamingLifecycleAdapter final : public streaming::IStreamingDataSource,
                                             public streaming::IStreamingCommitTarget,
                                             public streaming::IStreamingPriorityProvider,
                                             public streaming::IResidencyController,
                                             public streaming::IStreamingWorldSource,
                                             public streaming::IStreamingPersistenceSource,
                                             public streaming::IStreamingResourceSource,
                                             public IRuntimeAdapterLifecycle
{
  public:
    [[nodiscard]] epidemic::foundation::Result<streaming::ProgressiveLoadPlan>
        BuildLoadPlan(const streaming::StreamingRequest&) override
    {
        return epidemic::foundation::Result<streaming::ProgressiveLoadPlan>::Success({});
    }

    [[nodiscard]] epidemic::foundation::Result<streaming::StreamingStepResult>
        ExecuteStep(const streaming::StreamingRequest&,
                    streaming::StreamingPlanStepRecord,
                    const RuntimeBudget&) override
    {
        return epidemic::foundation::Result<streaming::StreamingStepResult>::Success({0, true});
    }

    [[nodiscard]] epidemic::foundation::Result<void> Commit(const streaming::StreamingRequest&) override
    {
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] epidemic::foundation::Result<void> Rollback(const streaming::StreamingRequest&) override
    {
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] streaming::StreamingPriorityClass
        GetPriority(const streaming::StreamingTarget&) const override
    {
        return streaming::StreamingPriorityClass::Normal;
    }

    [[nodiscard]] epidemic::foundation::Result<void> ActivateChunk(ChunkId) override
    {
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] epidemic::foundation::Result<void> DeactivateChunk(ChunkId) override
    {
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] epidemic::foundation::Result<void> UnloadChunk(ChunkId) override
    {
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] std::optional<RegionId> ResolveRegion(ChunkId) const override { return std::nullopt; }

    [[nodiscard]] epidemic::foundation::Result<void>
        PrepareChunkData(const streaming::StreamingRequest&) override
    {
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] epidemic::foundation::Result<void>
        PrepareChunkResources(const streaming::StreamingRequest&) override
    {
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] epidemic::foundation::Result<void> ReleaseChunkResources(ChunkId) override
    {
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] epidemic::foundation::Result<void> Shutdown() override
    {
        ++shutdown_calls;
        if (fail_next_shutdown)
        {
            fail_next_shutdown = false;
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("integration.adapter_shutdown_failure", "adapter shutdown failed on purpose"));
        }
        return epidemic::foundation::Result<void>::Success();
    }

    bool fail_next_shutdown = true;
    int shutdown_calls = 0;
};

EngineRuntimeOptions StreamingOnlyProductionOptions()
{
    EngineRuntimeOptions options{};
    options.profile = RuntimeProfile::Production;
    options.enable_assets = false;
    options.enable_serialization = false;
    options.enable_resources = true;
    options.enable_persistence = true;
    options.enable_time = false;
    options.enable_environment = false;
    options.enable_scene = false;
    options.enable_world = true;
    options.enable_streaming = true;
    options.enable_simulation = false;
    options.enable_navigation = false;
    options.enable_animation = false;
    options.enable_physics = false;
    options.enable_audio = false;
    options.enable_renderer = false;
    return options;
}

bool TestDefaultCompositionSmoke()
{
    Application app{};
    const auto runtime = RegisterDefaultEngineRuntime(app);
    bool ok = Expect(runtime.HasValue(), "default runtime composition should register");
    ok &= Expect(runtime.HasValue() && runtime.Value().registered_majors.size() == 16,
                 "default runtime should include all freeze majors");
    ok &= Expect(runtime.HasValue() && runtime.Value().coordinator != nullptr,
                 "default runtime should include coordinator");
    ok &= Expect(runtime.HasValue() && runtime.Value().integrations != nullptr,
                 "default runtime should own integrations");
    return ok;
}

bool TestProductionUsesStandardStreamingAdapter()
{
    EngineRuntimeDependencies dependencies{};
    dependencies.chunk_manifests = std::make_shared<EmptyChunkManifestSource>();
    const auto prepared = PrepareEngineRuntime(StreamingOnlyProductionOptions(), std::move(dependencies));
    bool ok = Expect(prepared.HasValue(), "production standard Streaming composition should prepare");
    if (!prepared)
    {
        return false;
    }
    ok &= Expect(prepared.Value().services.streaming != nullptr, "production Streaming services missing");
    ok &= Expect(prepared.Value().services.integrations->streaming_prepared_data != nullptr,
                 "standard Streaming adapter did not expose prepared-data query");
    return ok;
}

bool TestStreamingUsesResourcesAndPersistence()
{
    Application app{};
    auto manifests = std::make_shared<ManifestSource>();
    EngineRuntimeDependencies dependencies{};
    dependencies.chunk_manifests = manifests;
    const auto runtime = RegisterDefaultEngineRuntime(app, {}, std::move(dependencies));
    bool ok = Expect(runtime.HasValue(), "runtime composition failed for Streaming integration");
    if (!runtime)
    {
        return false;
    }

    const RegionId region{101};
    const ChunkId chunk{202};
    const ResourceType type{epidemic::foundation::StringId::FromString("integration.chunk")};
    const ResourceId resource = ResourceId::FromString("integration/chunk/resource");
    const PersistenceLocation location{region, chunk, epidemic::foundation::StringId::FromString("integration/zone")};
    manifests->manifest = ChunkStreamingManifest{
        chunk,
        {ChunkResourceRequirement{resource, type, 1}},
        location};

    ok &= Expect(runtime.Value().world->regions->RegisterRegion(
                     RegionDescriptor{region, epidemic::foundation::StringId::FromString("integration-region")})
                     .HasValue(),
                 "integration region registration failed");
    ok &= Expect(runtime.Value().world->chunks->RegisterChunk(ChunkDescriptor{chunk, region, 0, 0, 0}).HasValue(),
                 "integration chunk registration failed");
    TestResourceLoader loader{type};
    ok &= Expect(runtime.Value().resources->loaders->RegisterLoader(loader).HasValue(),
                 "integration resource loader registration failed");

    ZoneOverrideSnapshot persisted{};
    persisted.location = location;
    persisted.record_ids.push_back(PersistentObjectId{9001});
    auto transaction = runtime.Value().persistence->store->OpenTransaction();
    ok &= Expect(transaction != nullptr, "persistence transaction creation failed");
    if (transaction)
    {
        ok &= Expect(transaction->UpsertZoneOverride(persisted).HasValue(), "zone override staging failed");
        ok &= Expect(transaction->Commit().HasValue(), "zone override commit failed");
    }

    const auto demand = runtime.Value().streaming->runtime->Request(
        streaming::StreamingTarget{streaming::ChunkStreamingTarget{chunk}},
        streaming::StreamingPriorityClass::High);
    ok &= Expect(demand.HasValue(), "streaming integration demand failed");
    if (!demand)
    {
        return false;
    }

    RuntimeFrameInput frame{};
    frame.real_delta = RuntimeFrameDuration{std::chrono::microseconds{16667}};
    for (int iteration = 0; iteration < 32 &&
                            runtime.Value().streaming->runtime->GetChunkState(chunk) != streaming::StreamingState::Active;
         ++iteration)
    {
        const auto tick = runtime.Value().coordinator->Tick(frame);
        ok &= Expect(tick.HasValue(), "coordinator failed while loading streamed chunk");
        if (!tick)
        {
            return false;
        }
    }

    ok &= Expect(runtime.Value().streaming->runtime->GetChunkState(chunk) == streaming::StreamingState::Active,
                 "streamed chunk did not become active");
    const auto prepared = runtime.Value().integrations->streaming_prepared_data->GetPreparedData(chunk);
    ok &= Expect(prepared.HasValue() && prepared.Value().persisted_state.has_value(),
                 "streaming did not expose prepared persistence data");
    if (prepared && prepared.Value().persisted_state)
    {
        ok &= Expect(prepared.Value().persisted_state->record_ids.size() == 1 &&
                         prepared.Value().persisted_state->record_ids.front() == PersistentObjectId{9001},
                     "prepared persistence data does not match persisted zone override");
    }

    const auto in_use = runtime.Value().resources->manager->Evict(resource);
    ok &= Expect(!in_use.HasValue() && in_use.GetError().HasCode("resource.in_use"),
                 "active streamed resource was not retained by a lease");

    ok &= Expect(runtime.Value().streaming->runtime->ReleaseDemand(demand.Value()).HasValue(),
                 "streaming integration demand release failed");
    for (int iteration = 0; iteration < 32 &&
                            runtime.Value().streaming->runtime->GetChunkState(chunk) != streaming::StreamingState::Unloaded;
         ++iteration)
    {
        const auto tick = runtime.Value().coordinator->Tick(frame);
        ok &= Expect(tick.HasValue(), "coordinator failed while unloading streamed chunk");
        if (!tick)
        {
            return false;
        }
    }

    ok &= Expect(runtime.Value().streaming->runtime->GetChunkState(chunk) == streaming::StreamingState::Unloaded,
                 "streamed chunk did not unload");
    ok &= Expect(!runtime.Value().integrations->streaming_prepared_data->GetPreparedData(chunk).HasValue(),
                 "prepared persistence data survived chunk unload");
    ok &= Expect(runtime.Value().resources->manager->Evict(resource).HasValue(),
                 "streaming did not release resource lease on unload");
    return ok;
}

bool TestAnimationPoseReachesRenderer()
{
    Application app{};
    auto render_bridge = std::make_shared<ReadyRenderBridge>();
    auto render_sink = std::make_shared<RecordingRenderSink>();
    EngineRuntimeDependencies dependencies{};
    dependencies.renderer.resource_bridge = render_bridge;
    dependencies.renderer.command_sink = render_sink;

    const auto runtime = RegisterDefaultEngineRuntime(app, {}, std::move(dependencies));
    bool ok = Expect(runtime.HasValue(), "runtime composition failed for animation/render integration");
    if (!runtime)
    {
        return false;
    }

    const auto node = runtime.Value().scene->nodes->CreateNode();
    ok &= Expect(node.HasValue(), "animated render scene node creation failed");
    if (!node)
    {
        return false;
    }
    const RuntimeObjectId owner{node.Value().Raw()};
    const animation::SkeletonId skeleton{11};
    const animation::AnimationClipId clip{12};
    ok &= Expect(runtime.Value().animation->skeletons->RegisterSkeleton(animation::SkeletonDesc{skeleton, 3}).HasValue(),
                 "animation skeleton registration failed");
    ok &= Expect(runtime.Value().animation->clips->RegisterClip(
                     animation::AnimationClipDesc{clip, skeleton, 1.0f})
                     .HasValue(),
                 "animation clip registration failed");
    const auto animator = runtime.Value().animation->runtime->CreateAnimatorHandle(
        animation::AnimatorDesc{owner, skeleton, animation::AnimationLodLevel::Full});
    ok &= Expect(animator.HasValue(), "animator creation failed");
    if (!animator)
    {
        return false;
    }
    ok &= Expect(runtime.Value().animation->runtime->Play(
                     animation::AnimationPlaybackCommand{animator.Value(), clip, true, 1.0})
                     .HasValue(),
                 "animation playback start failed");
    ok &= Expect(runtime.Value().animation->runtime->Tick(
                     RuntimeFrameDuration{std::chrono::microseconds{16667}}, 1)
                     .HasValue(),
                 "animation evaluation failed");

    const auto proxy = runtime.Value().renderer->scene->RegisterProxy(renderer::RenderProxyDesc{
        owner,
        ResourceId::FromString("integration/render/mesh"),
        ResourceId::FromString("integration/render/material"),
        renderer::RenderTransformId{node.Value().Raw()},
        renderer::RenderLayer::Opaque,
        renderer::RenderProxyVisibility::Visible});
    ok &= Expect(proxy.HasValue(), "render proxy registration failed");
    ok &= Expect(runtime.Value().renderer->runtime->PrepareFrame().HasValue(), "renderer frame preparation failed");
    ok &= Expect(runtime.Value().renderer->runtime->RenderFrame().HasValue(), "renderer frame submission failed");
    ok &= Expect(render_sink->submissions.size() == 1, "renderer did not submit animated proxy");
    if (render_sink->submissions.size() == 1)
    {
        const auto& submission = render_sink->submissions.front();
        ok &= Expect(submission.pose != nullptr, "animated render submission has no pose");
        if (submission.pose)
        {
            ok &= Expect(submission.pose->owner == owner, "render pose owner mismatch");
            ok &= Expect(submission.pose->bone_transforms.size() == 3, "render pose bone count mismatch");
            ok &= Expect(submission.pose->revision != 0, "render pose revision was not propagated");
        }
    }
    return ok;
}


bool TestAudioResourceLeaseFollowsVoiceLifetime()
{
    Application app{};
    const audio::SoundId sound{501};
    const ResourceId resource = ResourceId::FromString("integration/audio/clip");
    const ResourceType type{epidemic::foundation::StringId::FromString("integration.audio")};

    EngineRuntimeDependencies dependencies{};
    dependencies.audio_resource_mapper = std::make_shared<TestAudioMapper>(sound, resource, type);
    const auto runtime = RegisterDefaultEngineRuntime(app, {}, std::move(dependencies));
    bool ok = Expect(runtime.HasValue(), "runtime composition failed for audio lease integration");
    if (!runtime)
    {
        return false;
    }

    TestAudioLoader loader{type};
    ok &= Expect(runtime.Value().resources->loaders->RegisterLoader(loader).HasValue(),
                 "audio resource loader registration failed");
    const auto node = runtime.Value().scene->nodes->CreateNode();
    ok &= Expect(node.HasValue(), "audio scene node creation failed");
    if (!node)
    {
        return false;
    }

    const RuntimeObjectId owner{node.Value().Raw()};
    const auto emitter = runtime.Value().audio->runtime->CreateEmitterHandle(audio::AudioEmitterDesc{
        owner,
        sound,
        audio::AudioTransformId{node.Value().Raw()},
        false,
        {},
        1.0f,
        true});
    ok &= Expect(emitter.HasValue(), "audio emitter creation failed");
    if (!emitter)
    {
        return false;
    }

    ok &= Expect(runtime.Value().resources->manager->ProcessPendingLoads({}).HasValue(),
                 "audio resource processing failed");
    ok &= Expect(runtime.Value().audio->runtime->Play(emitter.Value()).HasValue(),
                 "audio emitter playback failed");
    const auto while_playing = runtime.Value().resources->manager->Evict(resource);
    ok &= Expect(!while_playing.HasValue() && while_playing.GetError().HasCode("resource.in_use"),
                 "audio voice did not retain the resource lease");

    ok &= Expect(runtime.Value().audio->runtime->DestroyEmitter(emitter.Value()).HasValue(),
                 "audio emitter destruction failed");
    ok &= Expect(runtime.Value().resources->manager->Evict(resource).HasValue(),
                 "audio resource lease survived voice destruction");
    return ok;
}

bool TestEventPublishFailureRetainsSourceEvents()
{
    Application app{};
    auto sink = std::make_shared<FlakyEventSink>();
    EngineRuntimeDependencies dependencies{};
    dependencies.event_sink = sink;
    const auto runtime = RegisterDefaultEngineRuntime(app, {}, std::move(dependencies));
    bool ok = Expect(runtime.HasValue(), "runtime composition failed for event retry integration");
    if (!runtime)
    {
        return false;
    }

    const animation::SkeletonId skeleton{601};
    const animation::AnimationClipId clip{602};
    const RuntimeObjectId owner{603};
    ok &= Expect(runtime.Value().animation->skeletons->RegisterSkeleton(animation::SkeletonDesc{skeleton, 1}).HasValue(),
                 "event retry skeleton registration failed");
    ok &= Expect(runtime.Value().animation->clips->RegisterClip(animation::AnimationClipDesc{clip, skeleton, 2.0f}).HasValue(),
                 "event retry clip registration failed");
    const auto animator = runtime.Value().animation->runtime->CreateAnimatorHandle(
        animation::AnimatorDesc{owner, skeleton, animation::AnimationLodLevel::Full});
    ok &= Expect(animator.HasValue(), "event retry animator creation failed");
    if (!animator)
    {
        return false;
    }
    ok &= Expect(runtime.Value().animation->runtime->Play(
                     animation::AnimationPlaybackCommand{animator.Value(), clip, true, 1.0})
                     .HasValue(),
                 "event retry animation playback failed");

    RuntimeFrameInput frame{};
    frame.real_delta = RuntimeFrameDuration{std::chrono::microseconds{16667}};
    const auto first = runtime.Value().coordinator->Tick(frame);
    ok &= Expect(first.HasValue(), "event publish failure made the frame fatal");
    ok &= Expect(first.HasValue() && std::find_if(first.Value().failures.begin(), first.Value().failures.end(),
                                                  [](const RuntimePhaseFailure& failure) {
                                                      return failure.phase == RuntimeUpdateStep::DiagnosticsEvents;
                                                  }) != first.Value().failures.end(),
                 "event sink failure was not reported");
    ok &= Expect(!runtime.Value().animation->events->Events().empty(),
                 "animation events were cleared after failed event publication");

    const auto second = runtime.Value().coordinator->Tick(frame);
    ok &= Expect(second.HasValue(), "event publication retry frame failed");
    ok &= Expect(sink->calls >= 2 && sink->published_animation_events >= 1,
                 "retained animation event was not published on retry");
    ok &= Expect(runtime.Value().animation->events->Events().empty(),
                 "animation events were not cleared after successful publication");
    return ok;
}

bool TestCoordinatorShutdownRetriesAdapterCleanup()
{
    Application app{};
    auto adapter = std::make_shared<FlakyStreamingLifecycleAdapter>();
    EngineRuntimeOptions options{};
    options.enable_assets = false;
    options.enable_serialization = false;
    options.enable_resources = true;
    options.enable_persistence = true;
    options.enable_time = false;
    options.enable_environment = false;
    options.enable_scene = false;
    options.enable_world = true;
    options.enable_streaming = true;
    options.enable_simulation = false;
    options.enable_navigation = false;
    options.enable_animation = false;
    options.enable_physics = false;
    options.enable_audio = false;
    options.enable_renderer = false;

    EngineRuntimeDependencies dependencies{};
    dependencies.streaming.data_source = adapter;
    dependencies.streaming.commit_target = adapter;
    dependencies.streaming.priority_provider = adapter;
    dependencies.streaming.residency_controller = adapter;
    dependencies.streaming.world_source = adapter;
    dependencies.streaming.persistence_source = adapter;
    dependencies.streaming.resource_source = adapter;

    const auto runtime = RegisterDefaultEngineRuntime(app, options, std::move(dependencies));
    bool ok = Expect(runtime.HasValue(), "runtime composition failed for shutdown retry integration");
    if (!runtime)
    {
        return false;
    }

    const auto first = runtime.Value().coordinator->Shutdown();
    ok &= Expect(!first.HasValue() && first.GetError().HasCode("integration.adapter_shutdown_failure"),
                 "first coordinator shutdown should surface adapter cleanup failure");
    ok &= Expect(runtime.Value().coordinator->IsShutdownStarted() && !runtime.Value().coordinator->IsShutdownComplete(),
                 "failed shutdown lifecycle flags are incorrect");
    RuntimeFrameInput frame{};
    frame.real_delta = RuntimeFrameDuration{std::chrono::microseconds{1000}};
    ok &= Expect(!runtime.Value().coordinator->Tick(frame).HasValue(),
                 "coordinator accepted a frame after failed shutdown began");

    ok &= Expect(runtime.Value().coordinator->Shutdown().HasValue(),
                 "second coordinator shutdown did not retry adapter cleanup");
    ok &= Expect(runtime.Value().coordinator->IsShutdownComplete(),
                 "coordinator did not complete after successful cleanup retry");
    ok &= Expect(adapter->shutdown_calls == 2, "adapter lifecycle was not retried exactly once");
    return ok;
}

bool TestPhaseFailureDoesNotStopLaterPhases()
{
    Application app{};
    auto manifests = std::make_shared<ManifestSource>();
    const RegionId region{301};
    const ChunkId chunk{302};
    const ResourceType missing_type{epidemic::foundation::StringId::FromString("integration.missing")};
    manifests->manifest = ChunkStreamingManifest{
        chunk,
        {ChunkResourceRequirement{ResourceId::FromString("integration/missing/resource"), missing_type, 1}},
        PersistenceLocation{region, chunk, epidemic::foundation::StringId::FromString("failure-zone")}};

    EngineRuntimeDependencies dependencies{};
    dependencies.chunk_manifests = manifests;
    const auto runtime = RegisterDefaultEngineRuntime(app, {}, std::move(dependencies));
    bool ok = Expect(runtime.HasValue(), "runtime composition failed for failure-isolation integration");
    if (!runtime)
    {
        return false;
    }

    ok &= Expect(runtime.Value().world->regions->RegisterRegion(
                     RegionDescriptor{region, epidemic::foundation::StringId::FromString("failure-region")})
                     .HasValue(),
                 "failure-isolation region registration failed");
    ok &= Expect(runtime.Value().world->chunks->RegisterChunk(ChunkDescriptor{chunk, region, 0, 0, 0}).HasValue(),
                 "failure-isolation chunk registration failed");
    const auto demand = runtime.Value().streaming->runtime->Request(
        streaming::StreamingTarget{streaming::ChunkStreamingTarget{chunk}},
        streaming::StreamingPriorityClass::High);
    ok &= Expect(demand.HasValue(), "failure-isolation streaming request failed");
    if (!demand)
    {
        return false;
    }

    RuntimeFrameInput frame{};
    frame.real_delta = RuntimeFrameDuration{std::chrono::microseconds{16667}};
    bool observed_streaming_failure = false;
    for (int iteration = 0; iteration < 12 && !observed_streaming_failure; ++iteration)
    {
        const auto tick = runtime.Value().coordinator->Tick(frame);
        ok &= Expect(tick.HasValue(), "recoverable Streaming failure made the whole frame fatal");
        if (!tick)
        {
            return false;
        }
        const auto failure = std::find_if(
            tick.Value().failures.begin(), tick.Value().failures.end(), [](const RuntimePhaseFailure& current) {
                return current.phase == RuntimeUpdateStep::Streaming;
            });
        if (failure != tick.Value().failures.end())
        {
            observed_streaming_failure = true;
            ok &= Expect(std::find(tick.Value().executed_steps.begin(), tick.Value().executed_steps.end(),
                                   RuntimeUpdateStep::Renderer) != tick.Value().executed_steps.end(),
                         "Renderer did not execute after independent Streaming failure");
            ok &= Expect(std::find(tick.Value().executed_steps.begin(), tick.Value().executed_steps.end(),
                                   RuntimeUpdateStep::Audio) != tick.Value().executed_steps.end(),
                         "Audio did not execute after independent Streaming failure");
        }
    }
    ok &= Expect(observed_streaming_failure, "Streaming failure was not reported");
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok &= TestDefaultCompositionSmoke();
    ok &= TestProductionUsesStandardStreamingAdapter();
    ok &= TestStreamingUsesResourcesAndPersistence();
    ok &= TestAnimationPoseReachesRenderer();
    ok &= TestAudioResourceLeaseFollowsVoiceLifetime();
    ok &= TestEventPublishFailureRetainsSourceEvents();
    ok &= TestCoordinatorShutdownRetriesAdapterCleanup();
    ok &= TestPhaseFailureDoesNotStopLaterPhases();
    return ok ? 0 : 1;
}
