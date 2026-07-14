#include "Epidemic/Runtime/Foundation/runtime_foundation.h"
#include "Epidemic/Runtime/Resources/resource_handle.h"

#include <type_traits>

int main()
{
    epidemic::runtime::ResourceHandle handle{};
    static_assert(!std::is_same_v<epidemic::runtime::ResourceId, epidemic::runtime::ResourceHandle>);
    return handle.IsValid() ? 1 : 0;
}
