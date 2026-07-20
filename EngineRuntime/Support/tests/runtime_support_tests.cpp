#include "Epidemic/Runtime/Support/runtime_support.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

using epidemic::core::Application;
using namespace epidemic::runtime;

namespace
{
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

    EngineRuntimeDependencies manifest_only_dependencies{};
    manifest_only_dependencies.chunk_manifests = std::make_shared<EmptyChunkManifestSource>();
    const auto manifest_only_failed =
        RegisterDefaultEngineRuntime(app, options, std::move(manifest_only_dependencies));
    ok &= Expect(!manifest_only_failed.HasValue(),
                 "production streaming must require external roles, not only a chunk manifest");
    ok &= Expect(!app.Services().Contains<EngineRuntimeServices>(),
                 "manifest-only production failure changed the application");
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
    return ok;
}
}

int main()
{
    bool ok = true;
    ok &= TestIndividualRegistration();
    ok &= TestAtomicDefaultCompositionAndTypedOwnership();
    ok &= TestProductionPreflightIsAtomic();
    ok &= TestSceneProjectionQueue();
    ok &= TestStreamingWorldLifecycle();
    ok &= TestFullTickAndTerminalShutdown();
    return ok ? 0 : 1;
}
