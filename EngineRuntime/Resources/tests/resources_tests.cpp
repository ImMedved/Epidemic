#include "Epidemic/Runtime/Resources/resource_dependency_graph.h"
#include "Epidemic/Runtime/Resources/resource_handle.h"
#include "Epidemic/Runtime/Resources/resource_loader.h"
#include "Epidemic/Runtime/Resources/resource_loader_registry.h"
#include "Epidemic/Runtime/Resources/resource_manager.h"
#include "Epidemic/Runtime/Resources/resource_payload.h"
#include "Epidemic/Runtime/Resources/resource_request.h"
#include "Epidemic/Runtime/Resources/resource_result.h"
#include "Epidemic/Runtime/Resources/resource_services.h"
#include "Epidemic/Runtime/Resources/resource_state.h"
#include "Epidemic/Runtime/Resources/resource_type.h"
#include "resource_dependency_graph.h"
#include "resource_loader_registry.h"
#include "resource_manager.h"

#include <cstddef>
#include <iostream>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using epidemic::foundation::StringId;
using epidemic::runtime::ByteResourcePayload;
using epidemic::runtime::CanTransition;
using epidemic::runtime::CreateResourceServices;
using epidemic::runtime::IResourceLoader;
using epidemic::runtime::IResourceLoaderRegistry;
using epidemic::runtime::IResourceManager;
using epidemic::runtime::ResourceDependency;
using epidemic::runtime::ResourceDependencyGraph;
using epidemic::runtime::ResourceHandle;
using epidemic::runtime::ResourceId;
using epidemic::runtime::ResourceLoadArtifact;
using epidemic::runtime::ResourceLoaderRegistry;
using epidemic::runtime::ResourceManager;
using epidemic::runtime::ResourcePayloadPtr;
using epidemic::runtime::ResourceRequest;
using epidemic::runtime::ResourceResult;
using epidemic::runtime::ResourceSlot;
using epidemic::runtime::ResourceState;
using epidemic::runtime::ResourceType;
using epidemic::runtime::RuntimeBudget;

[[nodiscard]] ResourceType Type(std::string_view value)
{
    return ResourceType{StringId::FromString(value)};
}

[[nodiscard]] ResourceRequest MakeRequest(std::string_view resource_id, std::string_view type)
{
    return ResourceRequest{ResourceId::FromString(resource_id), Type(type), {}};
}

[[nodiscard]] ResourceDependency MakeDependency(std::string_view resource_id, std::string_view type, bool required = true)
{
    return ResourceDependency{ResourceId::FromString(resource_id), Type(type), required};
}

class CountingLoader final : public IResourceLoader
{
  public:
    explicit CountingLoader(ResourceType type, std::size_t payload_size = 8, bool should_fail = false,
                            std::vector<ResourceDependency> dependencies = {})
        : type_(type), payload_size_(payload_size), should_fail_(should_fail), dependencies_(std::move(dependencies))
    {
    }

    [[nodiscard]] ResourceType GetResourceType() const override
    {
        return type_;
    }

    [[nodiscard]] epidemic::foundation::Result<ResourceLoadArtifact> Load(ResourceRequest request) override
    {
        ++load_count_;
        last_request_ = request;
        if (should_fail_)
        {
            return epidemic::foundation::Result<ResourceLoadArtifact>::Failure(
                epidemic::foundation::Error::Create("resource.load_failed", "test loader failed on purpose"));
        }

        std::vector<std::byte> bytes(payload_size_, std::byte{0x2a});
        return epidemic::foundation::Result<ResourceLoadArtifact>::Success(
            ResourceLoadArtifact{request.resource_id, request.type, std::make_shared<ByteResourcePayload>(std::move(bytes)), dependencies_});
    }

    [[nodiscard]] int load_count() const noexcept
    {
        return load_count_;
    }

    [[nodiscard]] ResourceRequest last_request() const noexcept
    {
        return last_request_;
    }

  private:
    ResourceType type_{};
    std::size_t payload_size_ = 0;
    bool should_fail_ = false;
    int load_count_ = 0;
    ResourceRequest last_request_{};
    std::vector<ResourceDependency> dependencies_;
};

[[nodiscard]] const ResourceSlot* Slot(const ResourceManager& manager, std::string_view resource_id)
{
    return manager.InspectSlot(ResourceId::FromString(resource_id));
}

[[nodiscard]] bool TestBasicTypesAndStateTransitions()
{
    const ResourceHandle handle{};
    const ResourceRequest request{};
    const auto result = ResourceResult<ResourceHandle>::Success(ResourceHandle{ResourceId::FromString("resources/a.mesh"), 2u});
    return !handle.IsValid() && !request.resource_id.IsValid() && request.budget_hint.IsUnlimited() && result &&
           result.Value().generation == 2u && CanTransition(ResourceState::Unloaded, ResourceState::Queued) &&
           !CanTransition(ResourceState::Ready, ResourceState::Queued);
}

[[nodiscard]] bool TestDependencyGraphStoresDependencies()
{
    ResourceDependencyGraph graph;
    const auto root = ResourceId::FromString("resources/tree.mesh");
    graph.SetDependencies(root, {MakeDependency("resources/tree_albedo.tex", "texture"),
                                 MakeDependency("resources/tree_normals.tex", "texture", false)});
    const auto found = graph.FindDependencies(root);
    return graph.HasDependencies(root) && found && found->dependencies.size() == 2u && found->dependencies[0].required &&
           !found->dependencies[1].required;
}

[[nodiscard]] bool TestLoaderRegistryNonOwningContract()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(Type("mesh"));
    CountingLoader duplicate(Type("mesh"));
    const auto first = registry.RegisterLoader(loader);
    const auto second = registry.RegisterLoader(duplicate);
    return first && registry.FindLoader(loader.GetResourceType()) == &loader && registry.HasLoader(loader.GetResourceType()) && !second &&
           second.GetError().HasCode("resource_loader.duplicate_type");
}

[[nodiscard]] bool TestRequestQueuesAndProcessLoadsPayload()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(Type("mesh"), 16);
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto handle = manager.Request(MakeRequest("resources/tree.mesh", "mesh"));
    if (!handle || loader.load_count() != 0 || manager.GetState(handle.Value()) != ResourceState::Queued)
    {
        return false;
    }

    const auto processed = manager.ProcessPendingLoads();
    const auto payload = manager.GetPayload(handle.Value());
    const auto stats = manager.GetMemoryStats();
    return processed && processed.Value().processed_jobs == 1u && processed.Value().loaded_resources == 1u && loader.load_count() == 1 &&
           manager.IsReady(handle.Value()) && payload && payload->GetSizeBytes() == 16u && stats.resident_bytes == 16u && stats.ready_count == 1u;
}

[[nodiscard]] bool TestProcessBudgetLimitsJobs()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(Type("mesh"));
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto first = manager.Request(MakeRequest("resources/a.mesh", "mesh"));
    const auto second = manager.Request(MakeRequest("resources/b.mesh", "mesh"));
    RuntimeBudget budget{};
    budget.max_items = 1;
    const auto processed = manager.ProcessPendingLoads(budget);
    return first && second && processed && processed.Value().processed_jobs == 1u && loader.load_count() == 1 &&
           manager.GetState(first.Value()) == ResourceState::Ready && manager.GetState(second.Value()) == ResourceState::Queued;
}

[[nodiscard]] bool TestRepeatedReadyRequestDoesNotReload()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(Type("mesh"));
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto first = manager.Request(MakeRequest("resources/rock.mesh", "mesh"));
    if (!first || !manager.ProcessPendingLoads())
    {
        return false;
    }
    const auto second = manager.Request(MakeRequest("resources/rock.mesh", "mesh"));
    const ResourceSlot* slot = Slot(manager, "resources/rock.mesh");
    return second && first.Value() == second.Value() && loader.load_count() == 1 && slot != nullptr && slot->reference_count == 2u;
}

[[nodiscard]] bool TestTypeMismatchRejectsConflictingRequest()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(Type("mesh"));
    CountingLoader texture_loader(Type("texture"));
    if (!registry.RegisterLoader(mesh_loader) || !registry.RegisterLoader(texture_loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto mesh = manager.Request(MakeRequest("resources/shared.asset", "mesh"));
    const auto texture = manager.Request(MakeRequest("resources/shared.asset", "texture"));
    return mesh && !texture && texture.GetError().HasCode("resource.type_mismatch") && manager.GetState(mesh.Value()) == ResourceState::Queued;
}

[[nodiscard]] bool TestDependenciesLoadAndReleaseOnlyOnEvict()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(Type("mesh"), 10, false, {MakeDependency("resources/tree_albedo.tex", "texture")});
    CountingLoader texture_loader(Type("texture"), 6);
    if (!registry.RegisterLoader(mesh_loader) || !registry.RegisterLoader(texture_loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto root = manager.Request(MakeRequest("resources/tree.mesh", "mesh"));
    if (!root)
    {
        return false;
    }
    const auto processed = manager.ProcessPendingLoads();
    const ResourceSlot* root_slot = Slot(manager, "resources/tree.mesh");
    const ResourceSlot* dependency_slot = Slot(manager, "resources/tree_albedo.tex");
    if (!processed || !root_slot || !dependency_slot || root_slot->state != ResourceState::Ready || dependency_slot->state != ResourceState::Ready ||
        dependency_slot->reference_count != 1u)
    {
        return false;
    }

    manager.Release(root.Value());
    if (manager.GetState(root.Value()) != ResourceState::Ready || root_slot->reference_count != 0u || dependency_slot->reference_count != 1u)
    {
        return false;
    }

    const auto evicted = manager.Evict(ResourceId::FromString("resources/tree.mesh"));
    return evicted && manager.GetState(root.Value()) == ResourceState::Unknown && dependency_slot->reference_count == 0u &&
           dependency_slot->state == ResourceState::Ready;
}

[[nodiscard]] bool TestSharedDependencySurvivesUntilBothRootsEvict()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(Type("mesh"), 4, false, {MakeDependency("resources/shared.tex", "texture")});
    CountingLoader material_loader(Type("material"), 4, false, {MakeDependency("resources/shared.tex", "texture")});
    CountingLoader texture_loader(Type("texture"), 4);
    if (!registry.RegisterLoader(mesh_loader) || !registry.RegisterLoader(material_loader) || !registry.RegisterLoader(texture_loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto first = manager.Request(MakeRequest("resources/tree.mesh", "mesh"));
    const auto second = manager.Request(MakeRequest("resources/tree.mat", "material"));
    if (!first || !second || !manager.ProcessPendingLoads())
    {
        return false;
    }

    const ResourceSlot* shared = Slot(manager, "resources/shared.tex");
    if (!shared || shared->reference_count != 2u)
    {
        return false;
    }

    manager.Release(first.Value());
    const auto first_evict = manager.Evict(ResourceId::FromString("resources/tree.mesh"));
    if (!first_evict || shared->reference_count != 1u)
    {
        return false;
    }

    manager.Release(second.Value());
    const auto second_evict = manager.Evict(ResourceId::FromString("resources/tree.mat"));
    return second_evict && shared->reference_count == 0u && shared->state == ResourceState::Ready;
}

[[nodiscard]] bool TestMemoryBudgetFailureRollsBackPayload()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(Type("mesh"), 64);
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    manager.SetMemoryBudgetBytes(8);
    const auto handle = manager.Request(MakeRequest("resources/heavy.mesh", "mesh"));
    const auto processed = manager.ProcessPendingLoads();
    const ResourceSlot* slot = Slot(manager, "resources/heavy.mesh");
    const auto stats = manager.GetMemoryStats();
    return handle && !processed && processed.GetError().HasCode("resource.memory_budget_exceeded") && slot &&
           slot->state == ResourceState::Failed && !slot->payload && stats.resident_bytes == 0u;
}

[[nodiscard]] bool TestUnknownAndStaleHandles()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(Type("mesh"));
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto handle = manager.Request(MakeRequest("resources/stale.mesh", "mesh"));
    if (!handle || !manager.ProcessPendingLoads())
    {
        return false;
    }
    manager.Release(handle.Value());
    const auto evicted = manager.Evict(ResourceId::FromString("resources/stale.mesh"));
    const auto invalid = manager.ValidateHandle(ResourceHandle{});
    const auto stale = manager.ValidateHandle(handle.Value());
    return evicted && manager.GetState(ResourceHandle{}) == ResourceState::Unknown && manager.GetState(handle.Value()) == ResourceState::Unknown &&
           !invalid && invalid.GetError().HasCode("resource.invalid_handle") && !stale && stale.GetError().HasCode("resource.handle_stale");
}

[[nodiscard]] bool TestFactoryCreatesUsableServices()
{
    const auto services = CreateResourceServices();
    if (!services || !services.Value().loaders || !services.Value().manager)
    {
        return false;
    }

    CountingLoader loader(Type("mesh"));
    if (!services.Value().loaders->RegisterLoader(loader))
    {
        return false;
    }
    const auto handle = services.Value().manager->Request(MakeRequest("resources/factory.mesh", "mesh"));
    const auto processed = services.Value().manager->ProcessPendingLoads();
    return handle && processed && services.Value().manager->IsReady(handle.Value());
}
} // namespace

int main()
{
    static_assert(std::is_same_v<decltype(ResourceHandle{}.generation), std::uint32_t>);
    static_assert(std::is_same_v<decltype(ResourceRequest{}.budget_hint), RuntimeBudget>);
    static_assert(std::is_abstract_v<IResourceManager>);
    static_assert(std::is_abstract_v<IResourceLoader>);
    static_assert(std::is_abstract_v<IResourceLoaderRegistry>);

    struct NamedTest
    {
        const char* name;
        bool (*run)();
    };

    const NamedTest tests[] = {
        {"BasicTypesAndStateTransitions", TestBasicTypesAndStateTransitions},
        {"DependencyGraphStoresDependencies", TestDependencyGraphStoresDependencies},
        {"LoaderRegistryNonOwningContract", TestLoaderRegistryNonOwningContract},
        {"RequestQueuesAndProcessLoadsPayload", TestRequestQueuesAndProcessLoadsPayload},
        {"ProcessBudgetLimitsJobs", TestProcessBudgetLimitsJobs},
        {"RepeatedReadyRequestDoesNotReload", TestRepeatedReadyRequestDoesNotReload},
        {"TypeMismatchRejectsConflictingRequest", TestTypeMismatchRejectsConflictingRequest},
        {"DependenciesLoadAndReleaseOnlyOnEvict", TestDependenciesLoadAndReleaseOnlyOnEvict},
        {"SharedDependencySurvivesUntilBothRootsEvict", TestSharedDependencySurvivesUntilBothRootsEvict},
        {"MemoryBudgetFailureRollsBackPayload", TestMemoryBudgetFailureRollsBackPayload},
        {"UnknownAndStaleHandles", TestUnknownAndStaleHandles},
        {"FactoryCreatesUsableServices", TestFactoryCreatesUsableServices},
    };

    for (const NamedTest& test : tests)
    {
        if (!test.run())
        {
            std::cerr << "Resources test failed: " << test.name << "\n";
            return 1;
        }
    }

    return 0;
}

