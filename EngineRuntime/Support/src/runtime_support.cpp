#include "Epidemic/Runtime/Support/runtime_support.h"

#include "Epidemic/Foundation/error.h"

#include <memory>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace epidemic::runtime
{
namespace
{
template <typename TService>
[[nodiscard]] bool Contains(core::Application& app)
{
    return app.Services().Contains<TService>();
}

template <typename TService>
[[nodiscard]] foundation::Result<void> Require(core::Application& app, std::string_view name)
{
    if (!Contains<TService>(app))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("runtime_support.missing_dependency", std::string("required runtime service is not registered: ") + std::string(name)));
    }
    return foundation::Result<void>::Success();
}

template <typename TService>
[[nodiscard]] foundation::Result<void> RequireAbsent(core::Application& app, std::string_view name)
{
    if (Contains<TService>(app))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("runtime_support.duplicate_registration", std::string("runtime service is already registered: ") + std::string(name)));
    }
    return foundation::Result<void>::Success();
}

template <typename TService>
[[nodiscard]] foundation::Result<void> RegisterShared(core::Application& app, std::shared_ptr<TService> service, std::string_view name)
{
    if (Contains<TService>(app))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("runtime_support.duplicate_registration", std::string("runtime service is already registered: ") + std::string(name)));
    }
    try
    {
        app.Services().RegisterInstance<TService>(std::move(service));
    }
    catch (const std::exception& exception)
    {
        return foundation::Result<void>::Failure(foundation::Error::Create("runtime_support.registration_failed", exception.what()));
    }
    return foundation::Result<void>::Success();
}

[[nodiscard]] ResourceType MeshType()
{
    return ResourceType{foundation::StringId::FromString("mesh")};
}

[[nodiscard]] ResourceType MaterialType()
{
    return ResourceType{foundation::StringId::FromString("material")};
}

class RuntimeRenderResourceBridge final : public renderer::IRenderResourceBridge
{
  public:
    explicit RuntimeRenderResourceBridge(std::shared_ptr<IResourceManager> manager) : manager_(std::move(manager))
    {
    }

    ~RuntimeRenderResourceBridge() override
    {
        for (auto& [key, lease] : leases_)
        {
            (void)key;
            if (lease.handle.IsValid())
            {
                (void)manager_->Release(lease.handle);
            }
        }
    }

    [[nodiscard]] foundation::Result<void> AcquirePayloads(ResourceId mesh, ResourceId material) override
    {
        const auto mesh_result = AcquireResource(mesh, MeshType());
        if (!mesh_result)
        {
            return foundation::Result<void>::Failure(mesh_result.GetError());
        }

        const auto material_result = AcquireResource(material, MaterialType());
        if (!material_result)
        {
            ReleaseResource(mesh, MeshType());
            return foundation::Result<void>::Failure(material_result.GetError());
        }

        return foundation::Result<void>::Success();
    }

    void ReleasePayloads(ResourceId mesh, ResourceId material) override
    {
        ReleaseResource(mesh, MeshType());
        ReleaseResource(material, MaterialType());
    }

    [[nodiscard]] foundation::Result<renderer::RenderResourcePayloads> GetPayloads(ResourceId mesh, ResourceId material) const override
    {
        renderer::RenderResourcePayloads payloads{};
        payloads.mesh = GetPayload(mesh, MeshType());
        payloads.material = GetPayload(material, MaterialType());
        if (!payloads.mesh || !payloads.material)
        {
            return foundation::Result<renderer::RenderResourcePayloads>::Failure(
                foundation::Error::Create("renderer.resource_missing", "render resources are not ready"));
        }
        return foundation::Result<renderer::RenderResourcePayloads>::Success(std::move(payloads));
    }

  private:
    struct ResourceLeaseKey
    {
        ResourceId id{};
        ResourceType type{};

        [[nodiscard]] bool operator==(const ResourceLeaseKey&) const noexcept = default;
    };

    struct ResourceLeaseKeyHash
    {
        [[nodiscard]] std::size_t operator()(const ResourceLeaseKey& key) const noexcept
        {
            return std::hash<std::uint64_t>{}(key.id.Raw()) ^ (std::hash<std::uint64_t>{}(key.type.value.Raw()) << 1u);
        }
    };

    struct ResourceLease
    {
        ResourceHandle handle{};
        std::size_t references = 0;
    };

    [[nodiscard]] foundation::Result<void> AcquireResource(ResourceId id, ResourceType type)
    {
        const ResourceLeaseKey key{id, type};
        auto iterator = leases_.find(key);
        if (iterator != leases_.end())
        {
            ++iterator->second.references;
            return foundation::Result<void>::Success();
        }

        const auto handle = manager_->Request(ResourceRequest{id, type, {}});
        if (!handle)
        {
            return foundation::Result<void>::Failure(handle.GetError());
        }

        leases_.emplace(key, ResourceLease{handle.Value(), 1});
        return foundation::Result<void>::Success();
    }

    void ReleaseResource(ResourceId id, ResourceType type)
    {
        const ResourceLeaseKey key{id, type};
        auto iterator = leases_.find(key);
        if (iterator == leases_.end())
        {
            return;
        }
        if (iterator->second.references > 1)
        {
            --iterator->second.references;
            return;
        }
        (void)manager_->Release(iterator->second.handle);
        leases_.erase(iterator);
    }

    [[nodiscard]] ResourcePayloadPtr GetPayload(ResourceId id, ResourceType type) const
    {
        const auto iterator = leases_.find(ResourceLeaseKey{id, type});
        if (iterator == leases_.end())
        {
            return {};
        }
        return manager_->GetPayload(iterator->second.handle);
    }

    std::shared_ptr<IResourceManager> manager_;
    std::unordered_map<ResourceLeaseKey, ResourceLease, ResourceLeaseKeyHash> leases_;
};

class RuntimeRenderSceneSource final : public renderer::IRenderSceneSource
{
  public:
    RuntimeRenderSceneSource(std::shared_ptr<ISceneNodeRegistry> nodes, std::shared_ptr<ITransformRegistry> transforms)
        : nodes_(std::move(nodes)), transforms_(std::move(transforms))
    {
    }

    [[nodiscard]] foundation::Result<renderer::RenderTransformSnapshot> GetTransformSnapshot(renderer::RenderTransformId node) const override
    {
        const SceneNodeId scene_node{node.Raw()};
        if (!nodes_->Exists(scene_node))
        {
            return foundation::Result<renderer::RenderTransformSnapshot>::Failure(
                foundation::Error::Create("renderer.transform_missing", "scene node is not registered"));
        }
        const auto transform = transforms_->GetWorldTransform(scene_node);
        if (!transform)
        {
            return foundation::Result<renderer::RenderTransformSnapshot>::Failure(
                foundation::Error::Create("renderer.transform_missing", "scene node has no world transform"));
        }
        const auto node_record = nodes_->GetNode(scene_node);
        return foundation::Result<renderer::RenderTransformSnapshot>::Success(
            renderer::RenderTransformSnapshot{node, *transform, node_record ? node_record->revision : 0u});
    }

  private:
    std::shared_ptr<ISceneNodeRegistry> nodes_;
    std::shared_ptr<ITransformRegistry> transforms_;
};

template <typename TService>
void AppendIfPresent(core::Application& app, EngineRuntimeServices& services, std::string_view name)
{
    if (Contains<TService>(app))
    {
        services.registered_majors.push_back(std::string(name));
    }
}

[[nodiscard]] foundation::Result<void> PreflightDefaultRegistration(core::Application& app, const EngineRuntimeOptions& options)
{
#define EPIDEMIC_RUNTIME_REQUIRE_ABSENT(enabled, service, name) \
    if (enabled) \
    { \
        if (const auto result = RequireAbsent<service>(app, name); !result) \
        { \
            return result; \
        } \
    }

    EPIDEMIC_RUNTIME_REQUIRE_ABSENT(true, RuntimeFoundationRegistration, "RuntimeFoundation");
    EPIDEMIC_RUNTIME_REQUIRE_ABSENT(options.enable_assets, AssetServices, "Assets");
    EPIDEMIC_RUNTIME_REQUIRE_ABSENT(options.enable_serialization, SerializationServices, "Serialization");
    EPIDEMIC_RUNTIME_REQUIRE_ABSENT(options.enable_resources, ResourceServices, "Resources");
    EPIDEMIC_RUNTIME_REQUIRE_ABSENT(options.enable_persistence, PersistenceServices, "Persistence");
    EPIDEMIC_RUNTIME_REQUIRE_ABSENT(options.enable_time, TimeServices, "Time");
    EPIDEMIC_RUNTIME_REQUIRE_ABSENT(options.enable_environment, EnvironmentServices, "Environment");
    EPIDEMIC_RUNTIME_REQUIRE_ABSENT(options.enable_scene, SceneServices, "Scene");
    EPIDEMIC_RUNTIME_REQUIRE_ABSENT(options.enable_world, WorldServices, "World");
    EPIDEMIC_RUNTIME_REQUIRE_ABSENT(options.enable_streaming, streaming::StreamingServices, "Streaming");
    EPIDEMIC_RUNTIME_REQUIRE_ABSENT(options.enable_simulation, simulation::SimulationServices, "Simulation");
    EPIDEMIC_RUNTIME_REQUIRE_ABSENT(options.enable_navigation, navigation::NavigationServices, "Navigation");
    EPIDEMIC_RUNTIME_REQUIRE_ABSENT(options.enable_animation, animation::AnimationServices, "Animation");
    EPIDEMIC_RUNTIME_REQUIRE_ABSENT(options.enable_physics, physics::PhysicsServices, "Physics");
    EPIDEMIC_RUNTIME_REQUIRE_ABSENT(options.enable_audio, audio::AudioServices, "Audio");
    EPIDEMIC_RUNTIME_REQUIRE_ABSENT(options.enable_renderer, renderer::RendererServices, "Renderer");

#undef EPIDEMIC_RUNTIME_REQUIRE_ABSENT

    if (options.enable_streaming && (!options.enable_world || !options.enable_resources || !options.enable_persistence))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("runtime_support.missing_dependency", "Streaming requires World, Resources and Persistence in the default runtime"));
    }
    if (options.enable_simulation && !options.enable_time)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("runtime_support.missing_dependency", "Simulation requires Time in the default runtime"));
    }
    if (options.enable_navigation && !options.enable_environment)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("runtime_support.missing_dependency", "Navigation requires Environment in the default runtime"));
    }
    if (options.enable_animation && !options.enable_resources)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("runtime_support.missing_dependency", "Animation requires Resources in the default runtime"));
    }
    if (options.enable_physics && !options.enable_scene)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("runtime_support.missing_dependency", "Physics requires Scene in the default runtime"));
    }
    if (options.enable_audio && (!options.enable_resources || !options.enable_scene || !options.enable_environment))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("runtime_support.missing_dependency", "Audio requires Resources, Scene and Environment in the default runtime"));
    }
    if (options.enable_renderer && (!options.enable_resources || !options.enable_scene))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("runtime_support.missing_dependency", "Renderer requires Resources and Scene in the default runtime"));
    }

    return foundation::Result<void>::Success();
}
} // namespace

foundation::Result<void> RegisterRuntimeFoundation(core::Application& app)
{
    return RegisterShared(app, std::make_shared<RuntimeFoundationRegistration>(), "RuntimeFoundation");
}

foundation::Result<void> RegisterAssets(core::Application& app, const AssetsOptions& options)
{
    const auto dependency = Require<RuntimeFoundationRegistration>(app, "RuntimeFoundation");
    if (!dependency)
    {
        return dependency;
    }
    const auto services = CreateAssetServices(options);
    if (!services)
    {
        return foundation::Result<void>::Failure(services.GetError());
    }
    return RegisterShared(app, std::make_shared<AssetServices>(services.Value()), "Assets");
}

foundation::Result<void> RegisterSerialization(core::Application& app, const SerializationOptions& options)
{
    const auto dependency = Require<RuntimeFoundationRegistration>(app, "RuntimeFoundation");
    if (!dependency)
    {
        return dependency;
    }
    const auto services = CreateSerializationServices(options);
    if (!services)
    {
        return foundation::Result<void>::Failure(services.GetError());
    }
    return RegisterShared(app, std::make_shared<SerializationServices>(services.Value()), "Serialization");
}

foundation::Result<void> RegisterResources(core::Application& app, const ResourceOptions& options)
{
    const auto dependency = Require<RuntimeFoundationRegistration>(app, "RuntimeFoundation");
    if (!dependency)
    {
        return dependency;
    }
    const auto services = CreateResourceServices(options);
    if (!services)
    {
        return foundation::Result<void>::Failure(services.GetError());
    }
    return RegisterShared(app, std::make_shared<ResourceServices>(services.Value()), "Resources");
}

foundation::Result<void> RegisterPersistence(core::Application& app, const PersistenceOptions& options)
{
    const auto dependency = Require<RuntimeFoundationRegistration>(app, "RuntimeFoundation");
    if (!dependency)
    {
        return dependency;
    }
    const auto services = CreatePersistenceServices(options);
    if (!services)
    {
        return foundation::Result<void>::Failure(services.GetError());
    }
    return RegisterShared(app, std::make_shared<PersistenceServices>(services.Value()), "Persistence");
}

foundation::Result<void> RegisterTime(core::Application& app, const TimeOptions& options)
{
    const auto dependency = Require<RuntimeFoundationRegistration>(app, "RuntimeFoundation");
    if (!dependency)
    {
        return dependency;
    }
    const auto services = CreateTimeServices(options);
    if (!services)
    {
        return foundation::Result<void>::Failure(services.GetError());
    }
    return RegisterShared(app, std::make_shared<TimeServices>(services.Value()), "Time");
}

foundation::Result<void> RegisterEnvironment(core::Application& app, const EnvironmentOptions& options)
{
    const auto dependency = Require<RuntimeFoundationRegistration>(app, "RuntimeFoundation");
    if (!dependency)
    {
        return dependency;
    }
    const auto services = CreateEnvironmentServices(options);
    if (!services)
    {
        return foundation::Result<void>::Failure(services.GetError());
    }
    return RegisterShared(app, std::make_shared<EnvironmentServices>(services.Value()), "Environment");
}

foundation::Result<void> RegisterScene(core::Application& app, const SceneOptions& options)
{
    const auto dependency = Require<RuntimeFoundationRegistration>(app, "RuntimeFoundation");
    if (!dependency)
    {
        return dependency;
    }
    const auto services = CreateSceneServices(options);
    if (!services)
    {
        return foundation::Result<void>::Failure(services.GetError());
    }
    return RegisterShared(app, std::make_shared<SceneServices>(services.Value()), "Scene");
}

foundation::Result<void> RegisterWorld(core::Application& app, const WorldOptions& options)
{
    const auto dependency = Require<RuntimeFoundationRegistration>(app, "RuntimeFoundation");
    if (!dependency)
    {
        return dependency;
    }
    const auto services = CreateWorldServices(options);
    if (!services)
    {
        return foundation::Result<void>::Failure(services.GetError());
    }
    return RegisterShared(app, std::make_shared<WorldServices>(services.Value()), "World");
}

foundation::Result<void> RegisterStreaming(core::Application& app)
{
    if (const auto dependency = Require<WorldServices>(app, "World"); !dependency)
    {
        return dependency;
    }
    if (const auto dependency = Require<ResourceServices>(app, "Resources"); !dependency)
    {
        return dependency;
    }
    if (const auto dependency = Require<PersistenceServices>(app, "Persistence"); !dependency)
    {
        return dependency;
    }
    return RegisterShared(app, std::make_shared<streaming::StreamingServices>(streaming::CreateStreamingServices()), "Streaming");
}

foundation::Result<void> RegisterSimulation(core::Application& app, const simulation::SimulationOptions& options)
{
    const auto dependency = Require<TimeServices>(app, "Time");
    if (!dependency)
    {
        return dependency;
    }
    return RegisterShared(app, std::make_shared<simulation::SimulationServices>(simulation::CreateSimulationServices(options)), "Simulation");
}

foundation::Result<void> RegisterNavigation(core::Application& app, navigation::NavigationOptions options)
{
    const auto dependency = Require<EnvironmentServices>(app, "Environment");
    if (!dependency)
    {
        return dependency;
    }
    return RegisterShared(app, std::make_shared<navigation::NavigationServices>(navigation::CreateMockNavigationServices(options)), "Navigation");
}

foundation::Result<void> RegisterAnimation(core::Application& app, animation::AnimationOptions options)
{
    const auto dependency = Require<ResourceServices>(app, "Resources");
    if (!dependency)
    {
        return dependency;
    }
    return RegisterShared(app, std::make_shared<animation::AnimationServices>(animation::CreateAnimationServices(options)), "Animation");
}

foundation::Result<void> RegisterPhysics(core::Application& app)
{
    const auto dependency = Require<SceneServices>(app, "Scene");
    if (!dependency)
    {
        return dependency;
    }
    return RegisterShared(app, std::make_shared<physics::PhysicsServices>(physics::CreatePhysicsServices()), "Physics");
}

foundation::Result<void> RegisterAudio(core::Application& app, audio::AudioOptions options)
{
    if (const auto dependency = Require<ResourceServices>(app, "Resources"); !dependency)
    {
        return dependency;
    }
    if (const auto dependency = Require<SceneServices>(app, "Scene"); !dependency)
    {
        return dependency;
    }
    if (const auto dependency = Require<EnvironmentServices>(app, "Environment"); !dependency)
    {
        return dependency;
    }
    return RegisterShared(app, std::make_shared<audio::AudioServices>(audio::CreateMockAudioServices(options)), "Audio");
}

foundation::Result<void> RegisterRenderer(core::Application& app, const renderer::RendererOptions& options)
{
    const auto resources_dependency = Require<ResourceServices>(app, "Resources");
    if (!resources_dependency)
    {
        return resources_dependency;
    }
    const auto scene_dependency = Require<SceneServices>(app, "Scene");
    if (!scene_dependency)
    {
        return scene_dependency;
    }

    auto resource_bridge = options.resource_bridge == nullptr
                               ? std::make_shared<RuntimeRenderResourceBridge>(app.Services().Get<ResourceServices>()->manager)
                               : std::shared_ptr<renderer::IRenderResourceBridge>{};
    auto scene_source = options.scene_source == nullptr
                            ? std::make_shared<RuntimeRenderSceneSource>(app.Services().Get<SceneServices>()->nodes,
                                                                         app.Services().Get<SceneServices>()->transforms)
                            : std::shared_ptr<renderer::IRenderSceneSource>{};

    renderer::RendererOptions renderer_options = options;
    if (renderer_options.resource_bridge == nullptr)
    {
        renderer_options.resource_bridge = resource_bridge.get();
    }
    if (renderer_options.scene_source == nullptr)
    {
        renderer_options.scene_source = scene_source.get();
    }

    const auto services = renderer::CreateRendererServices(renderer_options);
    if (!services)
    {
        return foundation::Result<void>::Failure(services.GetError());
    }

    renderer::RendererServices renderer_services = services.Value();
    if (resource_bridge)
    {
        renderer_services.resource_bridge = resource_bridge;
    }
    if (scene_source)
    {
        renderer_services.scene_source = scene_source;
    }
    return RegisterShared(app, std::make_shared<renderer::RendererServices>(std::move(renderer_services)), "Renderer");
}

foundation::Result<EngineRuntimeServices> RegisterDefaultEngineRuntime(core::Application& app, const EngineRuntimeOptions& options)
{
    const auto preflight = PreflightDefaultRegistration(app, options);
    if (!preflight)
    {
        return foundation::Result<EngineRuntimeServices>::Failure(preflight.GetError());
    }

#define EPIDEMIC_RUNTIME_TRY(call) \
    if (const auto result = (call); !result) \
    { \
        return foundation::Result<EngineRuntimeServices>::Failure(result.GetError()); \
    }

    EPIDEMIC_RUNTIME_TRY(RegisterRuntimeFoundation(app));
    if (options.enable_assets) { EPIDEMIC_RUNTIME_TRY(RegisterAssets(app, options.assets)); }
    if (options.enable_serialization) { EPIDEMIC_RUNTIME_TRY(RegisterSerialization(app, options.serialization)); }
    if (options.enable_resources) { EPIDEMIC_RUNTIME_TRY(RegisterResources(app, options.resources)); }
    if (options.enable_persistence) { EPIDEMIC_RUNTIME_TRY(RegisterPersistence(app, options.persistence)); }
    if (options.enable_time) { EPIDEMIC_RUNTIME_TRY(RegisterTime(app, options.time)); }
    if (options.enable_environment) { EPIDEMIC_RUNTIME_TRY(RegisterEnvironment(app, options.environment)); }
    if (options.enable_scene) { EPIDEMIC_RUNTIME_TRY(RegisterScene(app, options.scene)); }
    if (options.enable_world) { EPIDEMIC_RUNTIME_TRY(RegisterWorld(app, options.world)); }
    if (options.enable_streaming) { EPIDEMIC_RUNTIME_TRY(RegisterStreaming(app)); }
    if (options.enable_simulation) { EPIDEMIC_RUNTIME_TRY(RegisterSimulation(app, options.simulation)); }
    if (options.enable_navigation) { EPIDEMIC_RUNTIME_TRY(RegisterNavigation(app, options.navigation)); }
    if (options.enable_animation) { EPIDEMIC_RUNTIME_TRY(RegisterAnimation(app, options.animation)); }
    if (options.enable_physics) { EPIDEMIC_RUNTIME_TRY(RegisterPhysics(app)); }
    if (options.enable_audio) { EPIDEMIC_RUNTIME_TRY(RegisterAudio(app, options.audio)); }
    if (options.enable_renderer) { EPIDEMIC_RUNTIME_TRY(RegisterRenderer(app, options.renderer)); }

#undef EPIDEMIC_RUNTIME_TRY

    EngineRuntimeServices services{};
    AppendIfPresent<RuntimeFoundationRegistration>(app, services, "RuntimeFoundation");
    AppendIfPresent<AssetServices>(app, services, "Assets");
    AppendIfPresent<SerializationServices>(app, services, "Serialization");
    AppendIfPresent<ResourceServices>(app, services, "Resources");
    AppendIfPresent<PersistenceServices>(app, services, "Persistence");
    AppendIfPresent<TimeServices>(app, services, "Time");
    AppendIfPresent<EnvironmentServices>(app, services, "Environment");
    AppendIfPresent<SceneServices>(app, services, "Scene");
    AppendIfPresent<WorldServices>(app, services, "World");
    AppendIfPresent<streaming::StreamingServices>(app, services, "Streaming");
    AppendIfPresent<simulation::SimulationServices>(app, services, "Simulation");
    AppendIfPresent<navigation::NavigationServices>(app, services, "Navigation");
    AppendIfPresent<animation::AnimationServices>(app, services, "Animation");
    AppendIfPresent<physics::PhysicsServices>(app, services, "Physics");
    AppendIfPresent<audio::AudioServices>(app, services, "Audio");
    AppendIfPresent<renderer::RendererServices>(app, services, "Renderer");
    return foundation::Result<EngineRuntimeServices>::Success(std::move(services));
}

std::vector<RuntimeAdapterKind> GetAllowedRuntimeAdapters()
{
    return {
        RuntimeAdapterKind::SceneToRenderer,
        RuntimeAdapterKind::ResourcesToRenderer,
        RuntimeAdapterKind::SceneToPhysics,
        RuntimeAdapterKind::ResourcesToAnimation,
        RuntimeAdapterKind::AnimationToRenderer,
        RuntimeAdapterKind::ResourcesToAudio,
        RuntimeAdapterKind::SceneToAudio,
        RuntimeAdapterKind::WorldResourcesPersistenceToStreaming,
        RuntimeAdapterKind::TimeToSimulation,
        RuntimeAdapterKind::EnvironmentToAudio,
        RuntimeAdapterKind::EnvironmentToNavigation,
    };
}

std::vector<RuntimeUpdateStep> GetRuntimeUpdateOrder()
{
    return {
        RuntimeUpdateStep::Time,
        RuntimeUpdateStep::MainThreadCommits,
        RuntimeUpdateStep::Resources,
        RuntimeUpdateStep::Streaming,
        RuntimeUpdateStep::Simulation,
        RuntimeUpdateStep::Navigation,
        RuntimeUpdateStep::Animation,
        RuntimeUpdateStep::Physics,
        RuntimeUpdateStep::SceneProjectionCommit,
        RuntimeUpdateStep::Audio,
        RuntimeUpdateStep::Renderer,
        RuntimeUpdateStep::DiagnosticsEvents,
    };
}

std::vector<RuntimeShutdownStep> GetRuntimeShutdownOrder()
{
    return {
        RuntimeShutdownStep::StopNewWork,
        RuntimeShutdownStep::CancelWaitBackgroundJobs,
        RuntimeShutdownStep::FlushDiscardProposals,
        RuntimeShutdownStep::StopAudio,
        RuntimeShutdownStep::StopPhysics,
        RuntimeShutdownStep::ReleaseRenderer,
        RuntimeShutdownStep::UnloadStreaming,
        RuntimeShutdownStep::ClosePersistenceTransactions,
        RuntimeShutdownStep::DestroyServices,
    };
}
} // namespace epidemic::runtime
