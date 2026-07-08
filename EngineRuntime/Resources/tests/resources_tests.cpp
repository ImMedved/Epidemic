#include "Epidemic/Runtime/Resources/resource_dependency_graph.h"
#include "Epidemic/Runtime/Resources/resource_handle.h"
#include "Epidemic/Runtime/Resources/resource_loader.h"
#include "Epidemic/Runtime/Resources/resource_loader_registry.h"
#include "Epidemic/Runtime/Resources/resource_manager.h"
#include "Epidemic/Runtime/Resources/resource_request.h"
#include "Epidemic/Runtime/Resources/resource_result.h"
#include "Epidemic/Runtime/Resources/resource_state.h"
#include "Epidemic/Runtime/Resources/resource_type.h"

#include "resource_dependency_graph.h"
#include "resource_loader_registry.h"
#include "resource_manager.h"

#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
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
using epidemic::runtime::ResourceRequest;
using epidemic::runtime::ResourceResult;
using epidemic::runtime::ResourceState;
using epidemic::runtime::ResourceType;

class CountingLoader final : public IResourceLoader
{
  public:
    explicit CountingLoader(ResourceType type, bool should_fail = false,
        std::vector<ResourceDependency> dependencies = {})
        : type_(type), should_fail_(should_fail), dependencies_(std::move(dependencies))
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

        return epidemic::foundation::Result<ResourceLoadArtifact>::Success(
            ResourceLoadArtifact{request.resource_id, request.type, dependencies_});
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
    bool should_fail_ = false;
    int load_count_ = 0;
    ResourceRequest last_request_{};
    std::vector<ResourceDependency> dependencies_;
};

ResourceRequest MakeRequest(const char* resource_id, const char* type)
{
    return ResourceRequest{ResourceId::FromString(resource_id), ResourceType{epidemic::foundation::StringId::FromString(type)}, {}};
}

ResourceDependency MakeDependency(const char* resource_id, const char* type, bool required = true)
{
    return ResourceDependency{ResourceId::FromString(resource_id),
        ResourceType{epidemic::foundation::StringId::FromString(type)}, required};
}

bool TestDefaultResourceHandleIsInvalid()
{
    const ResourceHandle handle{};
    return !handle.IsValid() && !handle.id.IsValid() && handle.generation == 0;
}

bool TestDefaultResourceRequestIsEmpty()
{
    const ResourceRequest request{};
    return !request.resource_id.IsValid() && !request.type.IsValid() && request.budget_hint.IsEmpty();
}

bool TestResourceTypeCanBeNamed()
{
    const ResourceType type{epidemic::foundation::StringId::FromString("mesh")};
    return type.IsValid();
}

bool TestResourceResultAliasCompiles()
{
    const auto result = ResourceResult<ResourceHandle>::Success(
        ResourceHandle{epidemic::runtime::ResourceId::FromString("resources/potato.mesh"), 3u});
    return result && result.Value().IsValid() && result.Value().generation == 3u;
}

bool TestDependencyGraphStoresDependencies()
{
    ResourceDependencyGraph graph;
    const auto root = ResourceId::FromString("resources/tree.mesh");
    std::vector<ResourceDependency> dependencies;
    dependencies.push_back(MakeDependency("resources/tree_albedo.tex", "texture"));
    dependencies.push_back(MakeDependency("resources/tree_normals.tex", "texture", false));

    graph.SetDependencies(root, dependencies);
    const auto found = graph.FindDependencies(root);
    return graph.HasDependencies(root) && found.has_value() && found->root == root && found->dependencies.size() == 2 &&
           found->dependencies[0].required && !found->dependencies[1].required;
}

bool TestLoaderRegistryRegistersAndFindsLoader()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")});

    const auto register_result = registry.RegisterLoader(loader);
    return register_result && registry.HasLoader(loader.GetResourceType()) && registry.FindLoader(loader.GetResourceType()) == &loader;
}

bool TestLoaderRegistryRejectsDuplicateType()
{
    ResourceLoaderRegistry registry;
    CountingLoader first(ResourceType{epidemic::foundation::StringId::FromString("mesh")});
    CountingLoader second(ResourceType{epidemic::foundation::StringId::FromString("mesh")});

    const auto first_result = registry.RegisterLoader(first);
    const auto second_result = registry.RegisterLoader(second);
    return first_result && !second_result && second_result.GetError().HasCode("resource_loader.duplicate_type");
}

bool TestRequestRequiresLoaderRegistry()
{
    ResourceManager manager;
    const auto result = manager.Request(MakeRequest("resources/tree.mesh", "mesh"));
    return !result && result.GetError().HasCode("resource.loader_registry_missing");
}

bool TestRequestRequiresRegisteredLoader()
{
    ResourceLoaderRegistry registry;
    ResourceManager manager(&registry);
    const auto result = manager.Request(MakeRequest("resources/tree.mesh", "mesh"));
    return !result && result.GetError().HasCode("resource.loader_not_found");
}

bool TestRequestLoadsResourceThroughRegistry()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")});
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto result = manager.Request(MakeRequest("resources/tree.mesh", "mesh"));
    if (!result)
    {
        return false;
    }

    const auto handle = result.Value();
    return handle.IsValid() && handle.generation == 1u && manager.IsReady(handle) &&
           manager.GetState(handle) == ResourceState::Ready && manager.GetResourceId(handle) == handle.id &&
           loader.load_count() == 1 && loader.last_request().resource_id == handle.id;
}

bool TestDependenciesLoadBeforeRootBecomesReady()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")}, false,
        {MakeDependency("resources/tree_albedo.tex", "texture")});
    CountingLoader texture_loader(ResourceType{epidemic::foundation::StringId::FromString("texture")});
    if (!registry.RegisterLoader(mesh_loader) || !registry.RegisterLoader(texture_loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto result = manager.Request(MakeRequest("resources/tree.mesh", "mesh"));
    if (!result)
    {
        return false;
    }

    return manager.GetState(result.Value()) == ResourceState::Ready && mesh_loader.load_count() == 1 && texture_loader.load_count() == 1;
}

bool TestMissingRequiredDependencyFailsRoot()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")}, false,
        {MakeDependency("resources/tree_albedo.tex", "texture")});
    if (!registry.RegisterLoader(mesh_loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto result = manager.Request(MakeRequest("resources/tree.mesh", "mesh"));
    return !result && result.GetError().HasCode("resource.loader_not_found");
}

bool TestOptionalMissingDependencyDoesNotFailRoot()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")}, false,
        {MakeDependency("resources/tree_fx.tex", "texture", false)});
    if (!registry.RegisterLoader(mesh_loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto result = manager.Request(MakeRequest("resources/tree.mesh", "mesh"));
    return result && manager.GetState(result.Value()) == ResourceState::Ready;
}

bool TestRepeatedRequestSharesResidentResource()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")});
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto first = manager.Request(MakeRequest("resources/rock.mesh", "mesh"));
    const auto second = manager.Request(MakeRequest("resources/rock.mesh", "mesh"));
    if (!first || !second)
    {
        return false;
    }

    return first.Value() == second.Value() && loader.load_count() == 1 && manager.GetState(first.Value()) == ResourceState::Ready;
}

bool TestReleaseUnknownHandleIsSafe()
{
    ResourceLoaderRegistry registry;
    ResourceManager manager(&registry);
    manager.Release(ResourceHandle{});
    manager.Release(ResourceHandle{ResourceId::FromString("resources/missing.mesh"), 99u});
    return manager.GetState(ResourceHandle{ResourceId::FromString("resources/missing.mesh"), 99u}) == ResourceState::Evicted;
}

bool TestHandleBecomesStaleAfterEvictionAndReload()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")});
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto first = manager.Request(MakeRequest("resources/house.mesh", "mesh"));
    if (!first)
    {
        return false;
    }

    manager.Release(first.Value());
    if (manager.GetState(first.Value()) != ResourceState::Evicted || manager.IsReady(first.Value()))
    {
        return false;
    }

    const auto second = manager.Request(MakeRequest("resources/house.mesh", "mesh"));
    if (!second)
    {
        return false;
    }

    return second.Value().generation == first.Value().generation + 1u &&
           manager.GetState(first.Value()) == ResourceState::Evicted &&
           !manager.GetResourceId(first.Value()).has_value() &&
           manager.GetState(second.Value()) == ResourceState::Ready &&
           loader.load_count() == 2;
}

bool TestLoaderFailurePropagatesError()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")}, true);
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto result = manager.Request(MakeRequest("resources/bad.mesh", "mesh"));
    return !result && result.GetError().HasCode("resource.load_failed") && loader.load_count() == 1;
}

bool TestDependencyCycleFailsLoad()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")}, false,
        {MakeDependency("resources/self.mesh", "mesh")});
    if (!registry.RegisterLoader(mesh_loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto result = manager.Request(MakeRequest("resources/self.mesh", "mesh"));
    return !result && result.GetError().HasCode("resource.dependency_cycle");
}
} // namespace

int main()
{
    static_assert(std::is_same_v<decltype(ResourceHandle{}.generation), std::uint32_t>);
    static_assert(std::is_same_v<decltype(ResourceRequest{}.budget_hint), epidemic::runtime::RuntimeBudget>);
    static_assert(static_cast<int>(ResourceState::Unloaded) != static_cast<int>(ResourceState::Ready));
    static_assert(std::is_abstract_v<IResourceManager>);
    static_assert(std::is_abstract_v<IResourceLoader>);
    static_assert(std::is_abstract_v<IResourceLoaderRegistry>);

    if (!TestDefaultResourceHandleIsInvalid())
    {
        return 1;
    }

    if (!TestDefaultResourceRequestIsEmpty())
    {
        return 2;
    }

    if (!TestResourceTypeCanBeNamed())
    {
        return 3;
    }

    if (!TestResourceResultAliasCompiles())
    {
        return 4;
    }

    if (!TestDependencyGraphStoresDependencies())
    {
        return 5;
    }

    if (!TestLoaderRegistryRegistersAndFindsLoader())
    {
        return 6;
    }

    if (!TestLoaderRegistryRejectsDuplicateType())
    {
        return 7;
    }

    if (!TestRequestRequiresLoaderRegistry())
    {
        return 8;
    }

    if (!TestRequestRequiresRegisteredLoader())
    {
        return 9;
    }

    if (!TestRequestLoadsResourceThroughRegistry())
    {
        return 10;
    }

    if (!TestDependenciesLoadBeforeRootBecomesReady())
    {
        return 11;
    }

    if (!TestMissingRequiredDependencyFailsRoot())
    {
        return 12;
    }

    if (!TestOptionalMissingDependencyDoesNotFailRoot())
    {
        return 13;
    }

    if (!TestRepeatedRequestSharesResidentResource())
    {
        return 14;
    }

    if (!TestReleaseUnknownHandleIsSafe())
    {
        return 15;
    }

    if (!TestHandleBecomesStaleAfterEvictionAndReload())
    {
        return 16;
    }

    if (!TestLoaderFailurePropagatesError())
    {
        return 17;
    }

    if (!TestDependencyCycleFailsLoad())
    {
        return 18;
    }

    return 0;
}
