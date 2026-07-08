#include "Epidemic/Runtime/Resources/resource_handle.h"
#include "Epidemic/Runtime/Resources/resource_manager.h"
#include "Epidemic/Runtime/Resources/resource_request.h"
#include "Epidemic/Runtime/Resources/resource_result.h"
#include "Epidemic/Runtime/Resources/resource_state.h"
#include "Epidemic/Runtime/Resources/resource_type.h"

#include "resource_manager.h"

#include <cstdint>
#include <type_traits>

namespace
{
using epidemic::runtime::IResourceManager;
using epidemic::runtime::ResourceHandle;
using epidemic::runtime::ResourceId;
using epidemic::runtime::ResourceManager;
using epidemic::runtime::ResourceRequest;
using epidemic::runtime::ResourceResult;
using epidemic::runtime::ResourceState;
using epidemic::runtime::ResourceType;

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

bool TestRequestReturnsReadyHandle()
{
    ResourceManager manager;
    const auto result = manager.Request(MakeRequest("resources/tree.mesh", "mesh"));
    if (!result)
    {
        return false;
    }

    const auto handle = result.Value();
    return handle.IsValid() && handle.generation == 1u && manager.IsReady(handle) &&
           manager.GetState(handle) == ResourceState::Ready && manager.GetResourceId(handle) == handle.id;
}

bool TestRequestRejectsInvalidRequest()
{
    ResourceManager manager;

    const auto invalid_id = manager.Request(ResourceRequest{});
    if (invalid_id || !invalid_id.GetError().HasCode("resource.invalid_id"))
    {
        return false;
    }

    const auto invalid_type = manager.Request(ResourceRequest{ResourceId::FromString("resources/tree.mesh"), {}, {}});
    return !invalid_type && invalid_type.GetError().HasCode("resource.invalid_type");
}

bool TestRepeatedRequestSharesGenerationWhileResident()
{
    ResourceManager manager;
    const auto first = manager.Request(MakeRequest("resources/rock.mesh", "mesh"));
    const auto second = manager.Request(MakeRequest("resources/rock.mesh", "mesh"));
    if (!first || !second)
    {
        return false;
    }

    return first.Value() == second.Value() && manager.GetState(first.Value()) == ResourceState::Ready;
}

bool TestReleaseUnknownHandleIsSafe()
{
    ResourceManager manager;
    manager.Release(ResourceHandle{});
    manager.Release(ResourceHandle{ResourceId::FromString("resources/missing.mesh"), 99u});
    return manager.GetState(ResourceHandle{ResourceId::FromString("resources/missing.mesh"), 99u}) == ResourceState::Evicted;
}

bool TestHandleBecomesStaleAfterEvictionAndReload()
{
    ResourceManager manager;
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
           manager.GetState(second.Value()) == ResourceState::Ready;
}
} // namespace

int main()
{
    static_assert(std::is_same_v<decltype(ResourceHandle{}.generation), std::uint32_t>);
    static_assert(std::is_same_v<decltype(ResourceRequest{}.budget_hint), epidemic::runtime::RuntimeBudget>);
    static_assert(static_cast<int>(ResourceState::Unloaded) != static_cast<int>(ResourceState::Ready));
    static_assert(std::is_abstract_v<IResourceManager>);

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

    if (!TestRequestReturnsReadyHandle())
    {
        return 5;
    }

    if (!TestRequestRejectsInvalidRequest())
    {
        return 6;
    }

    if (!TestRepeatedRequestSharesGenerationWhileResident())
    {
        return 7;
    }

    if (!TestReleaseUnknownHandleIsSafe())
    {
        return 8;
    }

    if (!TestHandleBecomesStaleAfterEvictionAndReload())
    {
        return 9;
    }

    return 0;
}
