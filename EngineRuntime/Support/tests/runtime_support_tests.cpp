#include "Epidemic/Runtime/Support/runtime_support.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

using epidemic::core::Application;
using namespace epidemic::runtime;

namespace epidemic::runtime::support_testing
{
std::shared_ptr<renderer::IRenderResourceBridge> CreateRenderResourceBridge(std::shared_ptr<IResourceManager> manager);
std::shared_ptr<streaming::IStreamingDataSource> CreateStreamingAdapter(std::shared_ptr<WorldServices> world,
                                                                       std::shared_ptr<ResourceServices> resources,
                                                                       std::shared_ptr<PersistenceServices> persistence,
                                                                       std::shared_ptr<IChunkStreamingManifestSource> manifests);
void FailNextRenderLeasePublication() noexcept;
void FailNextStreamingLeasePublication() noexcept;
void FailStreamingLeasePublicationAfter(std::size_t successful_publications) noexcept;
void FailNextStreamingPlanConstruction() noexcept;
void FailNextPersistenceException() noexcept;
void FailNextStreamingCleanupWorldTransition() noexcept;
void FailNextMainViewCreation() noexcept;
void FailNextMainViewPublication() noexcept;
foundation::Result<void> EnsureMainView(renderer::RendererServices& renderer_services,
                                        const std::shared_ptr<SceneServices>& scene);
foundation::Result<void> SetPreparedRevision(streaming::IStreamingDataSource& source,
                                             std::uint64_t request_id,
                                             std::uint64_t revision);
}

namespace
{
namespace foundation = epidemic::foundation;

[[nodiscard]] ResourceId TestResourceId(std::size_t index)
{
    const std::string value = "support.resource." + std::to_string(index);
    return ResourceId::FromString(value);
}

bool Expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }
    return condition;
}

template <typename TLeft, typename TRight>
bool SameOwner(const std::shared_ptr<TLeft>& left, const std::shared_ptr<TRight>& right)
{
    return left && right && !left.owner_before(right) && !right.owner_before(left);
}

class EmptyChunkManifestSource final : public IChunkStreamingManifestSource
{
  public:
    [[nodiscard]] epidemic::foundation::Result<ChunkStreamingManifest> GetManifest(ChunkId chunk) const override
    {
        return epidemic::foundation::Result<ChunkStreamingManifest>::Success(ChunkStreamingManifest{chunk, {}, {}});
    }
};

class TestResourceManager final : public IResourceManager
{
  public:
    [[nodiscard]] foundation::Result<ResourceLease> RequestLease(ResourceRequest request) override
    {
        ++request_attempts[request.resource_id.Raw()];
        if (fail_request_resource && *fail_request_resource == request.resource_id.Raw())
        {
            return foundation::Result<ResourceLease>::Failure(
                foundation::Error::Create("test.request_failed", "injected resource request failure"));
        }
        const ResourceLease lease{ResourceHandle{request.resource_id, 1}, next_acquisition++};
        active.emplace(lease.acquisition, lease);
        ++successful_requests[request.resource_id.Raw()];
        return foundation::Result<ResourceLease>::Success(lease);
    }

    [[nodiscard]] foundation::Result<ResourceProcessingStats> ProcessPendingLoads(RuntimeBudget = {}) override
    {
        return foundation::Result<ResourceProcessingStats>::Success({});
    }

    [[nodiscard]] foundation::Result<void> Release(ResourceLease lease) override
    {
        ++release_attempts[lease.resource.id.Raw()];
        if (fail_release_resource_once && *fail_release_resource_once == lease.resource.id.Raw())
        {
            fail_release_resource_once.reset();
            return foundation::Result<void>::Failure(
                foundation::Error::Create("test.release_failed", "injected resource release failure"));
        }
        const auto iterator = active.find(lease.acquisition);
        if (iterator == active.end())
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("test.double_release", "resource lease was released twice"));
        }
        active.erase(iterator);
        ++successful_releases[lease.resource.id.Raw()];
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<void> Evict(ResourceId) override
    {
        return foundation::Result<void>::Success();
    }
    [[nodiscard]] std::size_t EvictUnreferenced() override { return 0; }
    [[nodiscard]] foundation::Result<void> ValidateHandle(ResourceHandle handle) const override
    {
        return handle.IsValid() ? foundation::Result<void>::Success()
                                : foundation::Result<void>::Failure(
                                      foundation::Error::Create("test.invalid_handle", "invalid test resource handle"));
    }
    [[nodiscard]] ResourceState GetState(ResourceHandle handle) const override
    {
        return handle.IsValid() ? ResourceState::Ready : ResourceState::Unknown;
    }
    [[nodiscard]] bool IsReady(ResourceHandle handle) const override { return handle.IsValid(); }
    [[nodiscard]] std::optional<ResourceId> GetResourceId(ResourceHandle handle) const override
    {
        return handle.IsValid() ? std::optional<ResourceId>{handle.id} : std::nullopt;
    }
    [[nodiscard]] ResourcePayloadPtr GetPayload(ResourceHandle) const override { return {}; }
    void SetMemoryBudgetBytes(std::size_t) override {}
    [[nodiscard]] ResourceMemoryStats GetMemoryStatistics() const override { return {}; }

    [[nodiscard]] std::size_t ActiveCount() const noexcept { return active.size(); }
    [[nodiscard]] int SuccessfulReleaseCount(ResourceId id) const
    {
        const auto iterator = successful_releases.find(id.Raw());
        return iterator == successful_releases.end() ? 0 : iterator->second;
    }
    [[nodiscard]] int ReleaseAttemptCount(ResourceId id) const
    {
        const auto iterator = release_attempts.find(id.Raw());
        return iterator == release_attempts.end() ? 0 : iterator->second;
    }
    [[nodiscard]] int RequestAttemptCount(ResourceId id) const
    {
        const auto iterator = request_attempts.find(id.Raw());
        return iterator == request_attempts.end() ? 0 : iterator->second;
    }

    std::optional<std::uint64_t> fail_request_resource{};
    std::optional<std::uint64_t> fail_release_resource_once{};

  private:
    ResourceAcquisitionId next_acquisition = 1;
    std::unordered_map<ResourceAcquisitionId, ResourceLease> active;
    std::unordered_map<std::uint64_t, int> request_attempts;
    std::unordered_map<std::uint64_t, int> successful_requests;
    std::unordered_map<std::uint64_t, int> release_attempts;
    std::unordered_map<std::uint64_t, int> successful_releases;
};

class TestViewSystem final : public renderer::IViewSystem
{
  public:
    [[nodiscard]] foundation::Result<renderer::ViewId> CreateView(const renderer::ViewDesc&) override
    {
        const renderer::ViewId id{next_id++};
        views[id.value] = renderer::ViewLifecycle::Active;
        return foundation::Result<renderer::ViewId>::Success(id);
    }
    [[nodiscard]] foundation::Result<void> DestroyView(renderer::ViewId view) override
    {
        const auto iterator = views.find(view.value);
        if (iterator == views.end())
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("test.view_missing", "test view missing"));
        }
        views.erase(iterator);
        if (main == view) main = {};
        return foundation::Result<void>::Success();
    }
    [[nodiscard]] foundation::Result<void> SetMainView(renderer::ViewId view) override
    {
        if (!views.contains(view.value))
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("test.view_missing", "test view missing"));
        }
        main = view;
        return foundation::Result<void>::Success();
    }
    [[nodiscard]] renderer::ViewId GetMainView() const override { return main; }
    [[nodiscard]] renderer::ViewLifecycle GetViewLifecycle(renderer::ViewId view) const override
    {
        const auto iterator = views.find(view.value);
        return iterator == views.end() ? renderer::ViewLifecycle::Destroyed : iterator->second;
    }

  private:
    std::uint64_t next_id = 1;
    std::unordered_map<std::uint64_t, renderer::ViewLifecycle> views;
    renderer::ViewId main{};
};

class TestChunkManifestSource final : public IChunkStreamingManifestSource
{
  public:
    ChunkStreamingManifest manifest{};
    [[nodiscard]] foundation::Result<ChunkStreamingManifest> GetManifest(ChunkId chunk) const override
    {
        if (manifest.chunk != chunk)
        {
            return foundation::Result<ChunkStreamingManifest>::Failure(
                foundation::Error::Create("test.manifest_chunk", "manifest chunk mismatch"));
        }
        return foundation::Result<ChunkStreamingManifest>::Success(manifest);
    }
};

struct StreamingFixture
{
    RegionId region{501};
    ChunkId chunk{601};
    std::shared_ptr<WorldServices> world;
    std::shared_ptr<ResourceServices> resources;
    std::shared_ptr<PersistenceServices> persistence;
    std::shared_ptr<TestChunkManifestSource> manifests;
    std::shared_ptr<TestResourceManager> manager;
    std::shared_ptr<streaming::IStreamingDataSource> source;
    std::shared_ptr<streaming::IStreamingCommitTarget> commit;
    std::shared_ptr<streaming::IResidencyController> residency;
    std::shared_ptr<streaming::IStreamingPersistenceSource> persistence_source;
    std::shared_ptr<IRuntimeAdapterLifecycle> lifecycle;
    std::shared_ptr<IStreamingPreparedChunkDataQuery> prepared_query;

    [[nodiscard]] streaming::StreamingRequest Request(std::uint64_t id = 1) const
    {
        streaming::StreamingRequest request{};
        request.id = streaming::StreamingRequestId{id};
        request.handle = streaming::StreamingRequestHandle{request.id, 1};
        request.target = streaming::ChunkStreamingTarget{chunk};
        return request;
    }
};

[[nodiscard]] std::optional<StreamingFixture> MakeStreamingFixture(std::size_t resource_count)
{
    const auto world_result = CreateWorldServices();
    const auto persistence_result = CreatePersistenceServices();
    if (!world_result || !persistence_result)
    {
        return std::nullopt;
    }

    StreamingFixture fixture{};
    fixture.world = std::make_shared<WorldServices>(world_result.Value());
    fixture.persistence = std::make_shared<PersistenceServices>(persistence_result.Value());
    fixture.manager = std::make_shared<TestResourceManager>();
    fixture.resources = std::make_shared<ResourceServices>();
    fixture.resources->manager = fixture.manager;
    fixture.manifests = std::make_shared<TestChunkManifestSource>();
    fixture.manifests->manifest.chunk = fixture.chunk;
    fixture.manifests->manifest.persistence_location = PersistenceLocation{
        fixture.region, fixture.chunk, foundation::StringId::FromString("support-test")};
    for (std::size_t index = 0; index < resource_count; ++index)
    {
        fixture.manifests->manifest.resources.push_back(ChunkResourceRequirement{
            TestResourceId(index),
            ResourceType{foundation::StringId::FromString("streaming.test")},
            1});
    }

    if (!fixture.world->regions->RegisterRegion(
            RegionDescriptor{fixture.region, foundation::StringId::FromString("support-test-region")}) ||
        !fixture.world->chunks->RegisterChunk(ChunkDescriptor{fixture.chunk, fixture.region, 0, 0, 0}))
    {
        return std::nullopt;
    }

    fixture.source = support_testing::CreateStreamingAdapter(fixture.world, fixture.resources,
                                                              fixture.persistence, fixture.manifests);
    fixture.commit = std::dynamic_pointer_cast<streaming::IStreamingCommitTarget>(fixture.source);
    fixture.residency = std::dynamic_pointer_cast<streaming::IResidencyController>(fixture.source);
    fixture.persistence_source = std::dynamic_pointer_cast<streaming::IStreamingPersistenceSource>(fixture.source);
    fixture.lifecycle = std::dynamic_pointer_cast<IRuntimeAdapterLifecycle>(fixture.source);
    fixture.prepared_query = std::dynamic_pointer_cast<IStreamingPreparedChunkDataQuery>(fixture.source);
    if (!fixture.commit || !fixture.residency || !fixture.persistence_source || !fixture.lifecycle || !fixture.prepared_query)
    {
        return std::nullopt;
    }
    return fixture;
}

bool TestRenderLeaseRollbackAndCompositeRetry()
{
    const ResourceId mesh = ResourceId::FromString("support.mesh");
    const ResourceId material = ResourceId::FromString("support.material");
    auto manager = std::make_shared<TestResourceManager>();
    auto bridge = support_testing::CreateRenderResourceBridge(manager);
    auto lifecycle = std::dynamic_pointer_cast<IRuntimeAdapterLifecycle>(bridge);
    if (!bridge || !lifecycle) return false;

    support_testing::FailNextRenderLeasePublication();
    const auto local_publication_failure = bridge->AcquirePayloads(mesh, material);
    if (local_publication_failure || !local_publication_failure.GetError().HasCode("runtime_support.allocation_failure") ||
        manager->ActiveCount() != 0 || manager->SuccessfulReleaseCount(mesh) != 1)
        return false;

    manager->fail_request_resource = material.Raw();
    manager->fail_release_resource_once = mesh.Raw();
    const auto composite_failure = bridge->AcquirePayloads(mesh, material);
    if (composite_failure || !composite_failure.GetError().HasCode("runtime_support.render_payload_cleanup_pending") ||
        manager->ActiveCount() != 1 || manager->ReleaseAttemptCount(mesh) != 2)
        return false;
    if (!bridge->ReleasePayloads(mesh, material) || manager->ActiveCount() != 0 ||
        manager->SuccessfulReleaseCount(mesh) != 2)
        return false;

    manager->fail_request_resource.reset();
    if (!bridge->AcquirePayloads(mesh, material) || manager->ActiveCount() != 2)
        return false;
    manager->fail_release_resource_once = material.Raw();
    const auto partial = bridge->ReleasePayloads(mesh, material);
    if (partial || manager->ActiveCount() != 1 || manager->SuccessfulReleaseCount(mesh) != 3)
        return false;
    if (!bridge->ReleasePayloads(mesh, material) || manager->ActiveCount() != 0 ||
        manager->SuccessfulReleaseCount(material) != 1)
        return false;

    if (!bridge->AcquirePayloads(mesh, material)) return false;
    manager->fail_release_resource_once = mesh.Raw();
    const auto first_shutdown = lifecycle->Shutdown();
    if (first_shutdown || manager->ActiveCount() != 1) return false;
    const auto second_shutdown = lifecycle->Shutdown();
    return second_shutdown && manager->ActiveCount() == 0;
}

bool TestStreamingPublicationDuplicateAndPlanAtomicity()
{
    auto fixture_opt = MakeStreamingFixture(3);
    if (!fixture_opt) return false;
    auto& fixture = *fixture_opt;
    const auto request = fixture.Request();

    support_testing::FailNextStreamingPlanConstruction();
    const auto failed_plan = fixture.source->BuildLoadPlan(request);
    if (failed_plan || !failed_plan.GetError().HasCode("runtime_support.allocation_failure")) return false;
    const auto plan = fixture.source->BuildLoadPlan(request);
    if (!plan || plan.Value().steps.size() != 4) return false;

    const auto duplicate = fixture.source->BuildLoadPlan(request);
    if (duplicate || !duplicate.GetError().HasCode("runtime_support.duplicate_streaming_request")) return false;

    const auto resolve = fixture.source->ExecuteStep(
        request, streaming::StreamingPlanStepRecord{streaming::StreamingPlanStep::ResolveTarget, 1}, RuntimeBudget{});
    if (!resolve) return false;

    support_testing::FailStreamingLeasePublicationAfter(1);
    const auto publication_failure = fixture.source->ExecuteStep(
        request, streaming::StreamingPlanStepRecord{streaming::StreamingPlanStep::PrepareResources, 3}, RuntimeBudget{});
    if (publication_failure || !publication_failure.GetError().HasCode("runtime_support.allocation_failure") ||
        fixture.manager->ActiveCount() != 1 || fixture.manager->SuccessfulReleaseCount(TestResourceId(1)) != 1)
        return false;

    const auto retry = fixture.source->ExecuteStep(
        request, streaming::StreamingPlanStepRecord{streaming::StreamingPlanStep::PrepareResources, 3}, RuntimeBudget{});
    if (!retry || !retry.Value().completed || fixture.manager->ActiveCount() != 3) return false;

    const auto duplicate_with_leases = fixture.source->BuildLoadPlan(request);
    if (duplicate_with_leases || fixture.manager->ActiveCount() != 3) return false;
    if (!fixture.commit->Rollback(request) || fixture.manager->ActiveCount() != 0) return false;
    return true;
}

bool TestStreamingPrepareDataAndCleanupRetry()
{
    {
        auto fixture_opt = MakeStreamingFixture(1);
        if (!fixture_opt) return false;
        auto& fixture = *fixture_opt;
        const auto request = fixture.Request(11);
        if (!fixture.source->BuildLoadPlan(request)) return false;
        const auto before = fixture.prepared_query->GetPreparedData(fixture.chunk);
        if (!before) return false;
        if (!support_testing::SetPreparedRevision(*fixture.source, request.id.value,
                                                  std::numeric_limits<std::uint64_t>::max())) return false;
        const auto overflow = fixture.persistence_source->PrepareChunkData(request);
        const auto after_overflow = fixture.prepared_query->GetPreparedData(fixture.chunk);
        if (overflow || !overflow.GetError().HasCode("runtime_support.prepared_revision_overflow") ||
            !after_overflow || after_overflow.Value().revision != std::numeric_limits<std::uint64_t>::max() ||
            after_overflow.Value().persisted_state.has_value() != before.Value().persisted_state.has_value())
            return false;
    }

    {
        auto fixture_opt = MakeStreamingFixture(1);
        if (!fixture_opt) return false;
        auto& fixture = *fixture_opt;
        const auto request = fixture.Request(12);
        if (!fixture.source->BuildLoadPlan(request)) return false;
        const auto before = fixture.prepared_query->GetPreparedData(fixture.chunk);
        support_testing::FailNextPersistenceException();
        const auto failed = fixture.persistence_source->PrepareChunkData(request);
        const auto after = fixture.prepared_query->GetPreparedData(fixture.chunk);
        if (failed || !failed.GetError().HasCode("runtime_support.persistence_exception") || !before || !after ||
            before.Value().revision != after.Value().revision || before.Value().persisted_state.has_value() != after.Value().persisted_state.has_value())
            return false;
    }

    {
        auto fixture_opt = MakeStreamingFixture(1);
        if (!fixture_opt) return false;
        auto& fixture = *fixture_opt;
        const auto request = fixture.Request(13);
        if (!fixture.source->BuildLoadPlan(request)) return false;
        if (!fixture.source->ExecuteStep(request,
                                         streaming::StreamingPlanStepRecord{streaming::StreamingPlanStep::ResolveTarget, 1},
                                         RuntimeBudget{})) return false;
        if (!fixture.source->ExecuteStep(request,
                                         streaming::StreamingPlanStepRecord{streaming::StreamingPlanStep::PrepareResources, 1},
                                         RuntimeBudget{})) return false;
        support_testing::FailNextStreamingCleanupWorldTransition();
        const auto first = fixture.commit->Rollback(request);
        const auto loading = fixture.world->chunks->GetChunkSnapshot(fixture.chunk);
        if (first || !loading || loading.Value().state != ChunkState::Loading || fixture.manager->ActiveCount() != 0 ||
            fixture.manager->SuccessfulReleaseCount(TestResourceId(0)) != 1)
            return false;
        if (!fixture.lifecycle->Shutdown()) return false;
        const auto unloaded = fixture.world->chunks->GetChunkSnapshot(fixture.chunk);
        if (!unloaded || unloaded.Value().state != ChunkState::Unloaded ||
            fixture.manager->SuccessfulReleaseCount(TestResourceId(0)) != 1)
            return false;
    }

    {
        auto fixture_opt = MakeStreamingFixture(1);
        if (!fixture_opt) return false;
        auto& fixture = *fixture_opt;
        const auto request = fixture.Request(14);
        if (!fixture.source->BuildLoadPlan(request)) return false;
        if (!fixture.source->ExecuteStep(request,
                                         streaming::StreamingPlanStepRecord{streaming::StreamingPlanStep::ResolveTarget, 1},
                                         RuntimeBudget{})) return false;
        if (!fixture.source->ExecuteStep(request,
                                         streaming::StreamingPlanStepRecord{streaming::StreamingPlanStep::PrepareResources, 1},
                                         RuntimeBudget{})) return false;
        if (!fixture.commit->Commit(request)) return false;
        if (!fixture.residency->ActivateChunk(fixture.chunk) || !fixture.residency->DeactivateChunk(fixture.chunk)) return false;
        support_testing::FailNextStreamingCleanupWorldTransition();
        const auto first = fixture.residency->UnloadChunk(fixture.chunk);
        const auto unloading = fixture.world->chunks->GetChunkSnapshot(fixture.chunk);
        if (first || !unloading || unloading.Value().state != ChunkState::Unloading || fixture.manager->ActiveCount() != 0 ||
            fixture.manager->SuccessfulReleaseCount(TestResourceId(0)) != 1)
            return false;
        if (!fixture.residency->UnloadChunk(fixture.chunk)) return false;
        const auto unloaded = fixture.world->chunks->GetChunkSnapshot(fixture.chunk);
        return unloaded && unloaded.Value().state == ChunkState::Unloaded &&
               fixture.manager->SuccessfulReleaseCount(TestResourceId(0)) == 1;
    }
}

bool TestMainViewCreationRollback()
{
    const auto scene_result = CreateSceneServices();
    if (!scene_result) return false;
    auto scene = std::make_shared<SceneServices>(scene_result.Value());
    renderer::RendererServices renderer_services{};
    renderer_services.views = std::make_shared<TestViewSystem>();

    support_testing::FailNextMainViewCreation();
    const auto create_failure = support_testing::EnsureMainView(renderer_services, scene);
    if (create_failure || renderer_services.views->GetMainView().IsValid() || scene->nodes->Exists(SceneNodeId{1}))
        return false;

    support_testing::FailNextMainViewPublication();
    const auto publish_failure = support_testing::EnsureMainView(renderer_services, scene);
    if (publish_failure || renderer_services.views->GetMainView().IsValid() || scene->nodes->Exists(SceneNodeId{2}) ||
        renderer_services.views->GetViewLifecycle(renderer::ViewId{1}) != renderer::ViewLifecycle::Destroyed)
        return false;

    return support_testing::EnsureMainView(renderer_services, scene).HasValue() &&
           renderer_services.views->GetMainView().IsValid();
}

bool TestIndividualRegistration()
{
    Application app{};
    bool ok = true;
    ok &= Expect(RegisterRuntimeFoundation(app).HasValue(), "foundation registration failed");
    ok &= Expect(RegisterAssets(app).HasValue(), "assets registration failed");
    ok &= Expect(RegisterSerialization(app).HasValue(), "serialization registration failed");
    ok &= Expect(RegisterResources(app).HasValue(), "resources registration failed");
    ok &= Expect(RegisterPersistence(app).HasValue(), "persistence registration failed");
    ok &= Expect(RegisterTime(app).HasValue(), "time registration failed");
    ok &= Expect(RegisterEnvironment(app).HasValue(), "environment registration failed");
    ok &= Expect(RegisterScene(app).HasValue(), "scene registration failed");
    ok &= Expect(RegisterWorld(app).HasValue(), "world registration failed");
    ok &= Expect(RegisterStreaming(app).HasValue(), "streaming registration failed");
    ok &= Expect(RegisterSimulation(app).HasValue(), "simulation registration failed");
    ok &= Expect(RegisterNavigation(app).HasValue(), "navigation registration failed");
    ok &= Expect(RegisterAnimation(app).HasValue(), "animation registration failed");
    ok &= Expect(RegisterPhysics(app).HasValue(), "physics registration failed");
    ok &= Expect(RegisterAudio(app).HasValue(), "audio registration failed");
    ok &= Expect(RegisterRenderer(app).HasValue(), "renderer registration failed");
    ok &= Expect(!RegisterRenderer(app).HasValue(), "duplicate registration must fail");
    return ok;
}

bool TestAtomicDefaultCompositionAndTypedOwnership()
{
    Application app{};
    const auto runtime = RegisterDefaultEngineRuntime(app);
    bool ok = Expect(runtime.HasValue(), "default runtime composition failed");
    if (!runtime)
    {
        return false;
    }

    const EngineRuntimeServices& services = runtime.Value();
    ok &= Expect(app.Services().Contains<EngineRuntimeServices>(), "aggregate service was not registered");
    ok &= Expect(!app.Services().Contains<RuntimeFoundationRegistration>(),
                 "default composition must not register partial individual services");
    ok &= Expect(services.registered_majors.size() == 16, "default composition did not create every major");
    ok &= Expect(services.integrations != nullptr && services.coordinator != nullptr,
                 "default composition did not create integrations/coordinator");

    const RuntimeIntegrationServices& integrations = *services.integrations;
    ok &= Expect(SameOwner(integrations.streaming_data_source, integrations.streaming_commit_target),
                 "streaming roles do not share one adapter instance");
    ok &= Expect(SameOwner(integrations.streaming_data_source, integrations.streaming_residency_controller),
                 "streaming residency role does not share the adapter instance");
    ok &= Expect(SameOwner(integrations.physics_transform_source, integrations.physics_transform_sink),
                 "physics source and sink do not share one scene adapter");
    ok &= Expect(SameOwner(integrations.physics_transform_sink, integrations.scene_projections),
                 "scene projection queue is not the physics sink instance");
    ok &= Expect(SameOwner(integrations.animation_pose_sink, integrations.animation_pose_cache),
                 "animation pose sink/cache ownership differs");
    ok &= Expect(SameOwner(integrations.animation_pose_sink, integrations.render_pose_source),
                 "animation pose sink/render pose source ownership differs");

    // Replacing an animator for the same runtime object must replace the owner's pose even
    // when the new animator starts its own revision sequence from a lower value.
    auto first_pose = std::make_shared<animation::PoseBuffer>();
    first_pose->animator = animation::AnimatorHandle{animation::AnimatorInstanceId{41}, 1};
    first_pose->owner = RuntimeObjectId{77};
    first_pose->revision = 9;
    ok &= Expect(integrations.animation_pose_sink->Publish(first_pose).HasValue(),
                 "initial animation pose publication failed");

    auto replacement_pose = std::make_shared<animation::PoseBuffer>();
    replacement_pose->animator = animation::AnimatorHandle{animation::AnimatorInstanceId{42}, 1};
    replacement_pose->owner = RuntimeObjectId{77};
    replacement_pose->revision = 1;
    ok &= Expect(integrations.animation_pose_sink->Publish(replacement_pose).HasValue(),
                 "replacement animator pose must be allowed to restart its revision sequence");

    const auto rendered_pose = integrations.render_pose_source->GetPose(RuntimeObjectId{77});
    ok &= Expect(rendered_pose.HasValue() && rendered_pose.Value() && rendered_pose.Value()->revision == 1,
                 "render pose source did not replace the previous animator pose for the owner");
    ok &= Expect(!integrations.animation_pose_cache->GetPose(first_pose->animator).HasValue(),
                 "replaced animator pose remained reachable after owner replacement");
    return ok;
}

bool TestProductionPreflightIsAtomic()
{
    Application app{};
    EngineRuntimeOptions options{};
    options.profile = RuntimeProfile::Production;

    const auto failed = RegisterDefaultEngineRuntime(app, options, {});
    bool ok = Expect(!failed.HasValue(), "production composition without backends must fail");
    ok &= Expect(!app.Services().Contains<EngineRuntimeServices>(),
                 "failed production composition changed the application");
    ok &= Expect(!app.Services().Contains<RuntimeFoundationRegistration>(),
                 "failed production composition registered a partial runtime");

    // Production is allowed to use the standard Support Streaming adapter. In that mode
    // the application supplies only the real chunk manifest source; Support wires World,
    // Resources and Persistence itself.
    EngineRuntimeOptions streaming_only{};
    streaming_only.profile = RuntimeProfile::Production;
    streaming_only.enable_assets = false;
    streaming_only.enable_serialization = false;
    streaming_only.enable_resources = true;
    streaming_only.enable_persistence = true;
    streaming_only.enable_time = false;
    streaming_only.enable_environment = false;
    streaming_only.enable_scene = false;
    streaming_only.enable_world = true;
    streaming_only.enable_streaming = true;
    streaming_only.enable_simulation = false;
    streaming_only.enable_navigation = false;
    streaming_only.enable_animation = false;
    streaming_only.enable_physics = false;
    streaming_only.enable_audio = false;
    streaming_only.enable_renderer = false;

    EngineRuntimeDependencies manifest_dependencies{};
    manifest_dependencies.chunk_manifests = std::make_shared<EmptyChunkManifestSource>();
    const auto prepared = PrepareEngineRuntime(streaming_only, std::move(manifest_dependencies));
    ok &= Expect(prepared.HasValue(),
                 "production standard Streaming composition should accept a chunk manifest source");
    if (prepared)
    {
        ok &= Expect(prepared.Value().services.streaming != nullptr,
                     "production standard Streaming composition did not create Streaming services");
        ok &= Expect(prepared.Value().services.integrations->streaming_prepared_data != nullptr,
                     "production standard Streaming composition did not expose prepared-data query");
        ok &= Expect(SameOwner(prepared.Value().services.integrations->streaming_data_source,
                               prepared.Value().services.integrations->streaming_commit_target),
                     "production standard Streaming roles do not share one Support adapter");
    }
    ok &= Expect(!app.Services().Contains<EngineRuntimeServices>(),
                 "PrepareEngineRuntime must not mutate Application");
    return ok;
}

bool TestSceneProjectionQueue()
{
    Application app{};
    const auto runtime = RegisterDefaultEngineRuntime(app);
    bool ok = Expect(runtime.HasValue(), "runtime composition failed for projection test");
    if (!runtime)
    {
        return false;
    }

    const auto node = runtime.Value().scene->nodes->CreateNode();
    ok &= Expect(node.HasValue(), "scene node creation failed");
    if (!node)
    {
        return false;
    }

    Transform projected{};
    projected.position = Vec3{5.0f, 2.0f, -3.0f};
    const auto queued = runtime.Value().integrations->physics_transform_sink->WriteTransform(
        physics::PhysicsTransformId{node.Value().Raw()}, projected);
    ok &= Expect(queued.HasValue(), "physics transform write was not queued");
    ok &= Expect(runtime.Value().integrations->scene_projections->PendingCount() == 1,
                 "scene projection queue count is incorrect");

    const auto before = runtime.Value().scene->transforms->GetWorldTransform(node.Value());
    ok &= Expect(before.has_value() && before->position.x == 0.0f,
                 "physics sink mutated Scene before projection commit");

    const auto flushed = runtime.Value().integrations->scene_projections->Flush();
    ok &= Expect(flushed.HasValue() && flushed.Value() == 1, "scene projection flush failed");
    const auto after = runtime.Value().scene->transforms->GetWorldTransform(node.Value());
    ok &= Expect(after.has_value() && after->position == projected.position,
                 "scene projection did not apply the world transform");
    return ok;
}

bool TestStreamingWorldLifecycle()
{
    Application app{};
    const auto runtime = RegisterDefaultEngineRuntime(app);
    bool ok = Expect(runtime.HasValue(), "runtime composition failed for streaming test");
    if (!runtime)
    {
        return false;
    }

    const RegionId region{11};
    const ChunkId chunk{22};
    ok &= Expect(runtime.Value().world->regions->RegisterRegion(
                     RegionDescriptor{region, epidemic::foundation::StringId::FromString("test-region")})
                     .HasValue(),
                 "region registration failed");
    ok &= Expect(runtime.Value().world->chunks->RegisterChunk(
                     ChunkDescriptor{chunk, region, 0, 0, 0})
                     .HasValue(),
                 "chunk registration failed");

    const auto demand = runtime.Value().streaming->runtime->Request(
        streaming::StreamingTarget{streaming::ChunkStreamingTarget{chunk}},
        streaming::StreamingPriorityClass::High);
    ok &= Expect(demand.HasValue(), "streaming demand creation failed");
    if (!demand)
    {
        return false;
    }

    runtime.Value().streaming->runtime->SetBudget(streaming::StreamingBudget{});
    for (int index = 0; index < 32 &&
                        runtime.Value().streaming->runtime->GetChunkState(chunk) != streaming::StreamingState::Active;
         ++index)
    {
        const auto tick = runtime.Value().streaming->runtime->Tick();
        ok &= Expect(tick.failures.empty(), "streaming load tick failed");
    }

    ok &= Expect(runtime.Value().streaming->runtime->GetChunkState(chunk) == streaming::StreamingState::Active,
                 "streaming request did not activate the chunk");
    const auto active_snapshot = runtime.Value().world->chunks->GetChunkSnapshot(chunk);
    ok &= Expect(active_snapshot.HasValue() && active_snapshot.Value().state == ChunkState::Active,
                 "streaming adapter did not commit Active state to World");

    ok &= Expect(runtime.Value().streaming->runtime->ReleaseDemand(demand.Value()).HasValue(),
                 "streaming demand release failed");
    for (int index = 0; index < 32 &&
                        runtime.Value().streaming->runtime->GetChunkState(chunk) != streaming::StreamingState::Unloaded;
         ++index)
    {
        const auto tick = runtime.Value().streaming->runtime->Tick();
        ok &= Expect(tick.failures.empty(), "streaming unload tick failed");
    }

    ok &= Expect(runtime.Value().streaming->runtime->GetChunkState(chunk) == streaming::StreamingState::Unloaded,
                 "streaming request did not unload the chunk");
    const auto unloaded_snapshot = runtime.Value().world->chunks->GetChunkSnapshot(chunk);
    ok &= Expect(unloaded_snapshot.HasValue() && unloaded_snapshot.Value().state == ChunkState::Unloaded,
                 "streaming adapter did not commit Unloaded state to World");
    return ok;
}

bool TestFullTickAndTerminalShutdown()
{
    Application app{};
    const auto runtime = RegisterDefaultEngineRuntime(app);
    bool ok = Expect(runtime.HasValue(), "runtime composition failed for coordinator test");
    if (!runtime)
    {
        return false;
    }

    RuntimeFrameInput input{};
    input.real_delta = RuntimeFrameDuration{std::chrono::microseconds{16667}};
    const auto tick = runtime.Value().coordinator->Tick(input);
    ok &= Expect(tick.HasValue(), "full reference coordinator tick failed");
    if (tick)
    {
        ok &= Expect(tick.Value().executed_steps == GetRuntimeUpdateOrder(),
                     "coordinator did not execute the complete reference update order");
        ok &= Expect(tick.Value().failures.empty(), "full reference tick reported phase failures");
    }

    ok &= Expect(runtime.Value().coordinator->Shutdown().HasValue(), "runtime shutdown failed");
    ok &= Expect(runtime.Value().coordinator->IsShutdownStarted(), "shutdown-started flag was not set");
    ok &= Expect(runtime.Value().coordinator->IsShutdownComplete(), "shutdown-complete flag was not set");
    ok &= Expect(runtime.Value().coordinator->Shutdown().HasValue(), "second shutdown was not idempotent");
    ok &= Expect(!runtime.Value().coordinator->Tick(input).HasValue(), "coordinator accepted a frame after shutdown");
    ok &= Expect(runtime.Value().integrations->owned_adapters.empty(), "shutdown retained adapter ownership metadata");
    ok &= Expect(runtime.Value().integrations->render_pose_source == nullptr,
                 "shutdown retained Animation-to-Renderer pose adapter");
    ok &= Expect(runtime.Value().integrations->streaming_prepared_data == nullptr,
                 "shutdown retained Streaming prepared-data adapter");
    return ok;
}
}

int main()
{
    bool ok = true;
    ok &= Expect(TestRenderLeaseRollbackAndCompositeRetry(), "render lease transaction regression failed");
    ok &= Expect(TestStreamingPublicationDuplicateAndPlanAtomicity(), "streaming publication/plan regression failed");
    ok &= Expect(TestStreamingPrepareDataAndCleanupRetry(), "streaming prepare/cleanup regression failed");
    ok &= Expect(TestMainViewCreationRollback(), "main-view rollback regression failed");
    ok &= TestIndividualRegistration();
    ok &= TestAtomicDefaultCompositionAndTypedOwnership();
    ok &= TestProductionPreflightIsAtomic();
    ok &= TestSceneProjectionQueue();
    ok &= TestStreamingWorldLifecycle();
    ok &= TestFullTickAndTerminalShutdown();
    return ok ? 0 : 1;
}
