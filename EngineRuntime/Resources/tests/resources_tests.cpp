#include "Epidemic/Runtime/Resources/resource_dependency_graph.h"
#include "Epidemic/Runtime/Resources/resource_handle.h"
#include "Epidemic/Runtime/Resources/resource_loader.h"
#include "Epidemic/Runtime/Resources/resource_loader_registry.h"
#include "Epidemic/Runtime/Resources/resource_manager.h"
#include "Epidemic/Runtime/Resources/resource_request.h"
#include "Epidemic/Runtime/Resources/resource_result.h"
#include "Epidemic/Runtime/Resources/resource_state.h"
#include "Epidemic/Runtime/Resources/resource_type.h"

// File note:
// Focused module-level tests for the surrounding runtime component. Each helper builds
// a narrow fixture, and each Test* function verifies one public contract or regression.
#include "resource_dependency_graph.h"
#include "resource_loader_registry.h"
#include "resource_manager.h"

#include <cstddef>
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
using epidemic::runtime::ResourceSlot;
using epidemic::runtime::ResourceState;
using epidemic::runtime::ResourceType;

class CountingLoader final : public IResourceLoader
{
  public:
    explicit CountingLoader(ResourceType type, bool should_fail = false, std::vector<ResourceDependency> dependencies = {})
        // Function note: Handles type .
        // Inputs/outputs: see the signature; the method consumes caller-provided values and
        // returns either a value, status flag or Result according to the surrounding API.
        // Relations: this member is part of the local runtime workflow and pairs with
        // neighboring query/update helpers defined in the same class or file.
        : type_(type), should_fail_(should_fail), dependencies_(std::move(dependencies))
    {
    }

    // Function note: Gets resource type.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] ResourceType GetResourceType() const override
    {
        return type_;
    }

    // Function note: Loads the associated runtime state.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] epidemic::foundation::Result<ResourceLoadArtifact> Load(ResourceRequest request) override
    {
        ++load_count_;
        last_request_ = request;

        if (should_fail_)
        {
            return epidemic::foundation::Result<ResourceLoadArtifact>::Failure(
                // Function note: Creates the associated runtime state.
                // Inputs/outputs: see the signature; the method consumes caller-provided values and
                // returns either a value, status flag or Result according to the surrounding API.
                // Relations: this member is part of the local runtime workflow and pairs with
                // neighboring query/update helpers defined in the same class or file.
                epidemic::foundation::Error::Create("resource.load_failed", "test loader failed on purpose"));
        }

        return epidemic::foundation::Result<ResourceLoadArtifact>::Success(
            ResourceLoadArtifact{request.resource_id, request.type, dependencies_});
    }

    // Function note: Handles load count.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] int load_count() const noexcept
    {
        return load_count_;
    }

    // Function note: Handles last request.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
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

// Function note: Builds request.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
ResourceRequest MakeRequest(const char* resource_id, const char* type)
{
    return ResourceRequest{ResourceId::FromString(resource_id), ResourceType{epidemic::foundation::StringId::FromString(type)}, {}};
}

// Function note: Builds dependency.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
ResourceDependency MakeDependency(const char* resource_id, const char* type, bool required = true)
{
    return ResourceDependency{ResourceId::FromString(resource_id),
        // Function note: Handles from string.
        // Inputs/outputs: see the signature; the method consumes caller-provided values and
        // returns either a value, status flag or Result according to the surrounding API.
        // Relations: this member is part of the local runtime workflow and pairs with
        // neighboring query/update helpers defined in the same class or file.
        ResourceType{epidemic::foundation::StringId::FromString(type)}, required};
}

// Helper used by the tests to evaluate slot.
const ResourceSlot* Slot(const ResourceManager& manager, const char* resource_id)
{
    return manager.InspectSlot(ResourceId::FromString(resource_id));
}

// Verifies default resource handle is invalid.
bool TestDefaultResourceHandleIsInvalid()
{
    const ResourceHandle handle{};
    return !handle.IsValid() && !handle.id.IsValid() && handle.generation == 0;
}

// Verifies default resource request is empty.
bool TestDefaultResourceRequestIsEmpty()
{
    const ResourceRequest request{};
    return !request.resource_id.IsValid() && !request.type.IsValid() && request.budget_hint.IsEmpty();
}

// Verifies resource type can be named.
bool TestResourceTypeCanBeNamed()
{
    // Function note: Handles from string.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const ResourceType type{epidemic::foundation::StringId::FromString("mesh")};
    return type.IsValid();
}

// Verifies resource result alias compiles.
bool TestResourceResultAliasCompiles()
{
    const auto result = ResourceResult<ResourceHandle>::Success(
        // Function note: Handles from string.
        // Inputs/outputs: see the signature; the method consumes caller-provided values and
        // returns either a value, status flag or Result according to the surrounding API.
        // Relations: this member is part of the local runtime workflow and pairs with
        // neighboring query/update helpers defined in the same class or file.
        ResourceHandle{epidemic::runtime::ResourceId::FromString("resources/potato.mesh"), 3u});
    return result && result.Value().IsValid() && result.Value().generation == 3u;
}

// Verifies dependency graph stores dependencies.
bool TestDependencyGraphStoresDependencies()
{
    ResourceDependencyGraph graph;
    // Function note: Handles from string.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const auto root = ResourceId::FromString("resources/tree.mesh");
    std::vector<ResourceDependency> dependencies;
    dependencies.push_back(MakeDependency("resources/tree_albedo.tex", "texture"));
    dependencies.push_back(MakeDependency("resources/tree_normals.tex", "texture", false));

    graph.SetDependencies(root, dependencies);
    const auto found = graph.FindDependencies(root);
    return graph.HasDependencies(root) && found.has_value() && found->root == root && found->dependencies.size() == 2 &&
           found->dependencies[0].required && !found->dependencies[1].required;
}

// Verifies loader registry registers and finds loader.
bool TestLoaderRegistryRegistersAndFindsLoader()
{
    ResourceLoaderRegistry registry;
    // Function note: Handles loader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    CountingLoader loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")});

    const auto register_result = registry.RegisterLoader(loader);
    return register_result && registry.HasLoader(loader.GetResourceType()) && registry.FindLoader(loader.GetResourceType()) == &loader;
}

// Verifies loader registry rejects duplicate type.
bool TestLoaderRegistryRejectsDuplicateType()
{
    ResourceLoaderRegistry registry;
    // Function note: Handles first.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    CountingLoader first(ResourceType{epidemic::foundation::StringId::FromString("mesh")});
    // Function note: Handles second.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    CountingLoader second(ResourceType{epidemic::foundation::StringId::FromString("mesh")});

    const auto first_result = registry.RegisterLoader(first);
    const auto second_result = registry.RegisterLoader(second);
    return first_result && !second_result && second_result.GetError().HasCode("resource_loader.duplicate_type");
}

// Verifies request requires loader registry.
bool TestRequestRequiresLoaderRegistry()
{
    ResourceManager manager;
    const auto result = manager.Request(MakeRequest("resources/tree.mesh", "mesh"));
    return !result && result.GetError().HasCode("resource.loader_registry_missing");
}

// Verifies request requires registered loader.
bool TestRequestRequiresRegisteredLoader()
{
    ResourceLoaderRegistry registry;
    // Function note: Handles manager.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ResourceManager manager(&registry);
    const auto result = manager.Request(MakeRequest("resources/tree.mesh", "mesh"));
    return !result && result.GetError().HasCode("resource.loader_not_found");
}

// Verifies request loads resource through registry.
bool TestRequestLoadsResourceThroughRegistry()
{
    ResourceLoaderRegistry registry;
    // Function note: Handles loader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    CountingLoader loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")});
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    // Function note: Handles manager.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
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

// Verifies type mismatch rejects conflicting request.
bool TestTypeMismatchRejectsConflictingRequest()
{
    ResourceLoaderRegistry registry;
    // Function note: Handles mesh loader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    CountingLoader mesh_loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")});
    // Function note: Handles texture loader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    CountingLoader texture_loader(ResourceType{epidemic::foundation::StringId::FromString("texture")});
    if (!registry.RegisterLoader(mesh_loader) || !registry.RegisterLoader(texture_loader))
    {
        return false;
    }

    // Function note: Handles manager.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ResourceManager manager(&registry);
    const auto mesh_request = manager.Request(MakeRequest("resources/shared.asset", "mesh"));
    const auto texture_request = manager.Request(MakeRequest("resources/shared.asset", "texture"));
    if (!mesh_request)
    {
        return false;
    }

    return !texture_request && texture_request.GetError().HasCode("resource.type_mismatch") &&
           manager.GetState(mesh_request.Value()) == ResourceState::Ready;
}

// Verifies dependencies load before root becomes ready.
bool TestDependenciesLoadBeforeRootBecomesReady()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")}, false,
        {MakeDependency("resources/tree_albedo.tex", "texture")});
    // Function note: Handles texture loader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    CountingLoader texture_loader(ResourceType{epidemic::foundation::StringId::FromString("texture")});
    if (!registry.RegisterLoader(mesh_loader) || !registry.RegisterLoader(texture_loader))
    {
        return false;
    }

    // Function note: Handles manager.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ResourceManager manager(&registry);
    const auto result = manager.Request(MakeRequest("resources/tree.mesh", "mesh"));
    if (!result)
    {
        return false;
    }

    // Function note: Handles slot.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const ResourceSlot* dependency_slot = Slot(manager, "resources/tree_albedo.tex");
    return manager.GetState(result.Value()) == ResourceState::Ready && mesh_loader.load_count() == 1 &&
           texture_loader.load_count() == 1 && dependency_slot != nullptr && dependency_slot->reference_count == 1;
}

// Verifies releasing root releases dependency.
bool TestReleasingRootReleasesDependency()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")}, false,
        {MakeDependency("resources/tree_albedo.tex", "texture")});
    // Function note: Handles texture loader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    CountingLoader texture_loader(ResourceType{epidemic::foundation::StringId::FromString("texture")});
    if (!registry.RegisterLoader(mesh_loader) || !registry.RegisterLoader(texture_loader))
    {
        return false;
    }

    // Function note: Handles manager.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ResourceManager manager(&registry);
    const auto root = manager.Request(MakeRequest("resources/tree.mesh", "mesh"));
    if (!root)
    {
        return false;
    }

    // Function note: Handles slot.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const ResourceSlot* dependency_before = Slot(manager, "resources/tree_albedo.tex");
    if (dependency_before == nullptr || dependency_before->reference_count != 1 || dependency_before->state != ResourceState::Ready)
    {
        return false;
    }

    manager.Release(root.Value());
    // Function note: Handles slot.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const ResourceSlot* dependency_after = Slot(manager, "resources/tree_albedo.tex");
    return manager.GetState(root.Value()) == ResourceState::Unknown && dependency_after != nullptr &&
           dependency_after->reference_count == 0 && dependency_after->state == ResourceState::Unloaded &&
           dependency_after->dependency_handles.empty();
}

// Verifies shared dependency remains alive until both roots release.
bool TestSharedDependencyRemainsAliveUntilBothRootsRelease()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")}, false,
        {MakeDependency("resources/shared.tex", "texture")});
    CountingLoader material_loader(ResourceType{epidemic::foundation::StringId::FromString("material")}, false,
        {MakeDependency("resources/shared.tex", "texture")});
    // Function note: Handles texture loader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    CountingLoader texture_loader(ResourceType{epidemic::foundation::StringId::FromString("texture")});
    if (!registry.RegisterLoader(mesh_loader) || !registry.RegisterLoader(material_loader) || !registry.RegisterLoader(texture_loader))
    {
        return false;
    }

    // Function note: Handles manager.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ResourceManager manager(&registry);
    const auto first_root = manager.Request(MakeRequest("resources/tree.mesh", "mesh"));
    const auto second_root = manager.Request(MakeRequest("resources/tree.mat", "material"));
    if (!first_root || !second_root)
    {
        return false;
    }

    // Function note: Handles slot.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const ResourceSlot* shared_before = Slot(manager, "resources/shared.tex");
    if (shared_before == nullptr || shared_before->reference_count != 2 || shared_before->state != ResourceState::Ready)
    {
        return false;
    }

    manager.Release(first_root.Value());
    // Function note: Handles slot.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const ResourceSlot* shared_mid = Slot(manager, "resources/shared.tex");
    if (shared_mid == nullptr || shared_mid->reference_count != 1 || shared_mid->state != ResourceState::Ready)
    {
        return false;
    }

    manager.Release(second_root.Value());
    // Function note: Handles slot.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const ResourceSlot* shared_after = Slot(manager, "resources/shared.tex");
    return shared_after != nullptr && shared_after->reference_count == 0 && shared_after->state == ResourceState::Unloaded;
}

// Verifies missing required dependency fails root.
bool TestMissingRequiredDependencyFailsRoot()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")}, false,
        {MakeDependency("resources/tree_albedo.tex", "texture")});
    if (!registry.RegisterLoader(mesh_loader))
    {
        return false;
    }

    // Function note: Handles manager.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ResourceManager manager(&registry);
    const auto result = manager.Request(MakeRequest("resources/tree.mesh", "mesh"));
    return !result && result.GetError().HasCode("resource.loader_not_found");
}

// Verifies optional missing dependency does not fail root.
bool TestOptionalMissingDependencyDoesNotFailRoot()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")}, false,
        {MakeDependency("resources/tree_fx.tex", "texture", false)});
    if (!registry.RegisterLoader(mesh_loader))
    {
        return false;
    }

    // Function note: Handles manager.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ResourceManager manager(&registry);
    const auto result = manager.Request(MakeRequest("resources/tree.mesh", "mesh"));
    return result && manager.GetState(result.Value()) == ResourceState::Ready;
}

// Verifies repeated request shares resident resource.
bool TestRepeatedRequestSharesResidentResource()
{
    ResourceLoaderRegistry registry;
    // Function note: Handles loader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    CountingLoader loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")});
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    // Function note: Handles manager.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ResourceManager manager(&registry);
    const auto first = manager.Request(MakeRequest("resources/rock.mesh", "mesh"));
    const auto second = manager.Request(MakeRequest("resources/rock.mesh", "mesh"));
    if (!first || !second)
    {
        return false;
    }

    // Function note: Handles slot.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const ResourceSlot* slot = Slot(manager, "resources/rock.mesh");
    return first.Value() == second.Value() && loader.load_count() == 1 && manager.GetState(first.Value()) == ResourceState::Ready &&
           slot != nullptr && slot->reference_count == 2;
}

// Verifies unknown handle state is reported as unknown.
bool TestUnknownHandleStateIsReportedAsUnknown()
{
    ResourceLoaderRegistry registry;
    // Function note: Handles manager.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ResourceManager manager(&registry);
    manager.Release(ResourceHandle{});
    // Function note: Handles from string.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const ResourceHandle missing_handle{ResourceId::FromString("resources/missing.mesh"), 99u};
    manager.Release(missing_handle);
    return manager.GetState(ResourceHandle{}) == ResourceState::Unknown && manager.GetState(missing_handle) == ResourceState::Unknown;
}

// Verifies release makes handle stale and reload uses next generation.
bool TestReleaseMakesHandleStaleAndReloadUsesNextGeneration()
{
    ResourceLoaderRegistry registry;
    // Function note: Handles loader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    CountingLoader loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")});
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    // Function note: Handles manager.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ResourceManager manager(&registry);
    const auto first = manager.Request(MakeRequest("resources/house.mesh", "mesh"));
    if (!first)
    {
        return false;
    }

    manager.Release(first.Value());
    if (manager.GetState(first.Value()) != ResourceState::Unknown || manager.IsReady(first.Value()))
    {
        return false;
    }

    // Function note: Handles slot.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const ResourceSlot* unloaded_slot = Slot(manager, "resources/house.mesh");
    if (unloaded_slot == nullptr || unloaded_slot->state != ResourceState::Unloaded || unloaded_slot->reference_count != 0)
    {
        return false;
    }

    const auto second = manager.Request(MakeRequest("resources/house.mesh", "mesh"));
    return second && second.Value().generation == first.Value().generation + 1u && manager.GetState(second.Value()) == ResourceState::Ready;
}

// Verifies explicit eviction changes state.
bool TestExplicitEvictionChangesState()
{
    ResourceLoaderRegistry registry;
    // Function note: Handles loader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    CountingLoader loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")});
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    // Function note: Handles manager.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ResourceManager manager(&registry);
    const auto handle = manager.Request(MakeRequest("resources/evict.mesh", "mesh"));
    if (!handle)
    {
        return false;
    }

    manager.Release(handle.Value());
    const auto evict_result = manager.Evict(ResourceId::FromString("resources/evict.mesh"));
    // Function note: Handles slot.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const ResourceSlot* slot = Slot(manager, "resources/evict.mesh");
    return evict_result && slot != nullptr && slot->state == ResourceState::Evicted;
}

// Verifies evict unreferenced evicts only unreferenced slots.
bool TestEvictUnreferencedEvictsOnlyUnreferencedSlots()
{
    ResourceLoaderRegistry registry;
    // Function note: Handles mesh loader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    CountingLoader mesh_loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")});
    // Function note: Handles texture loader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    CountingLoader texture_loader(ResourceType{epidemic::foundation::StringId::FromString("texture")});
    if (!registry.RegisterLoader(mesh_loader) || !registry.RegisterLoader(texture_loader))
    {
        return false;
    }

    // Function note: Handles manager.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ResourceManager manager(&registry);
    const auto kept = manager.Request(MakeRequest("resources/kept.mesh", "mesh"));
    const auto dropped = manager.Request(MakeRequest("resources/dropped.tex", "texture"));
    if (!kept || !dropped)
    {
        return false;
    }

    manager.Release(dropped.Value());
    const std::size_t evicted = manager.EvictUnreferenced();
    // Function note: Handles slot.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const ResourceSlot* kept_slot = Slot(manager, "resources/kept.mesh");
    // Function note: Handles slot.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const ResourceSlot* dropped_slot = Slot(manager, "resources/dropped.tex");
    return evicted == 1u && kept_slot != nullptr && kept_slot->state == ResourceState::Ready && dropped_slot != nullptr &&
           dropped_slot->state == ResourceState::Evicted;
}

// Verifies loader failure propagates error.
bool TestLoaderFailurePropagatesError()
{
    ResourceLoaderRegistry registry;
    // Function note: Handles loader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    CountingLoader loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")}, true);
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    // Function note: Handles manager.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ResourceManager manager(&registry);
    const auto result = manager.Request(MakeRequest("resources/bad.mesh", "mesh"));
    return !result && result.GetError().HasCode("resource.load_failed") && loader.load_count() == 1;
}

// Verifies dependency cycle fails load.
bool TestDependencyCycleFailsLoad()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(ResourceType{epidemic::foundation::StringId::FromString("mesh")}, false,
        {MakeDependency("resources/self.mesh", "mesh")});
    if (!registry.RegisterLoader(mesh_loader))
    {
        return false;
    }

    // Function note: Handles manager.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ResourceManager manager(&registry);
    const auto result = manager.Request(MakeRequest("resources/self.mesh", "mesh"));
    return !result && result.GetError().HasCode("resource.dependency_cycle");
}
} // namespace

// Runs the local test suite and maps failures to stable exit codes.
int main()
{
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::is_same_v<decltype(ResourceHandle{}.generation), std::uint32_t>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::is_same_v<decltype(ResourceRequest{}.budget_hint), epidemic::runtime::RuntimeBudget>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::is_same_v<decltype(ResourceManager{}.EvictUnreferenced()), std::size_t>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::is_abstract_v<IResourceManager>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::is_abstract_v<IResourceLoader>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
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

    if (!TestTypeMismatchRejectsConflictingRequest())
    {
        return 11;
    }

    if (!TestDependenciesLoadBeforeRootBecomesReady())
    {
        return 12;
    }

    if (!TestReleasingRootReleasesDependency())
    {
        return 13;
    }

    if (!TestSharedDependencyRemainsAliveUntilBothRootsRelease())
    {
        return 14;
    }

    if (!TestMissingRequiredDependencyFailsRoot())
    {
        return 15;
    }

    if (!TestOptionalMissingDependencyDoesNotFailRoot())
    {
        return 16;
    }

    if (!TestRepeatedRequestSharesResidentResource())
    {
        return 17;
    }

    if (!TestUnknownHandleStateIsReportedAsUnknown())
    {
        return 18;
    }

    if (!TestReleaseMakesHandleStaleAndReloadUsesNextGeneration())
    {
        return 19;
    }

    if (!TestExplicitEvictionChangesState())
    {
        return 20;
    }

    if (!TestEvictUnreferencedEvictsOnlyUnreferencedSlots())
    {
        return 21;
    }

    if (!TestLoaderFailurePropagatesError())
    {
        return 22;
    }

    if (!TestDependencyCycleFailsLoad())
    {
        return 23;
    }

    return 0;
}
