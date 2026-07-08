#include "Epidemic/Runtime/Resources/resource_handle.h"
#include "Epidemic/Runtime/Resources/resource_loader.h"
#include "Epidemic/Runtime/Resources/resource_loader_registry.h"
#include "Epidemic/Runtime/Resources/resource_manager.h"
#include "Epidemic/Runtime/Resources/resource_request.h"
#include "Epidemic/Runtime/Resources/resource_result.h"
#include "Epidemic/Runtime/Resources/resource_state.h"
#include "Epidemic/Runtime/Resources/resource_type.h"

#include "resource_loader_registry.h"
#include "resource_manager.h"

#include <cstdint>
#include <type_traits>

namespace
{
using epidemic::runtime::IResourceLoader;
using epidemic::runtime::IResourceLoaderRegistry;
using epidemic::runtime::IResourceManager;
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
    explicit CountingLoader(ResourceType type, bool should_fail = false) : type_(type), should_fail_(should_fail)
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

        return epidemic::foundation::Result<ResourceLoadArtifact>::Success(ResourceLoadArtifact{request.resource_id, request.type});
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
};

ResourceRequest MakeRequest(const char* resource_id, const char* type)
{
    return ResourceRequest{ResourceId::FromString(resource_id), ResourceType{epidemic::foundation::StringId::FromString(type)}, {}};
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

    if (!TestLoaderRegistryRegistersAndFindsLoader())
    {
        return 5;
    }

    if (!TestLoaderRegistryRejectsDuplicateType())
    {
        return 6;
    }

    if (!TestRequestRequiresLoaderRegistry())
    {
        return 7;
    }

    if (!TestRequestRequiresRegisteredLoader())
    {
        return 8;
    }

    if (!TestRequestLoadsResourceThroughRegistry())
    {
        return 9;
    }

    if (!TestRepeatedRequestSharesResidentResource())
    {
        return 10;
    }

    if (!TestReleaseUnknownHandleIsSafe())
    {
        return 11;
    }

    if (!TestHandleBecomesStaleAfterEvictionAndReload())
    {
        return 12;
    }

    if (!TestLoaderFailurePropagatesError())
    {
        return 13;
    }

    return 0;
}
