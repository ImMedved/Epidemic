#include "Epidemic/Runtime/Resources/resource_handle.h"
#include "Epidemic/Runtime/Resources/resource_request.h"
#include "Epidemic/Runtime/Resources/resource_result.h"
#include "Epidemic/Runtime/Resources/resource_state.h"
#include "Epidemic/Runtime/Resources/resource_type.h"

#include <cstdint>
#include <type_traits>

namespace
{
using epidemic::runtime::ResourceHandle;
using epidemic::runtime::ResourceRequest;
using epidemic::runtime::ResourceResult;
using epidemic::runtime::ResourceState;
using epidemic::runtime::ResourceType;

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
} // namespace

int main()
{
    static_assert(std::is_same_v<decltype(ResourceHandle{}.generation), std::uint32_t>);
    static_assert(std::is_same_v<decltype(ResourceRequest{}.budget_hint), epidemic::runtime::RuntimeBudget>);
    static_assert(static_cast<int>(ResourceState::Unloaded) != static_cast<int>(ResourceState::Ready));

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

    return 0;
}
