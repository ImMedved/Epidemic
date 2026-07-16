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
    AppendIfPresent<renderer::RendererServices>(app, services, "Renderer");
    return foundation::Result<EngineRuntimeServices>::Success(std::move(services));
}
} // namespace epidemic::runtime
