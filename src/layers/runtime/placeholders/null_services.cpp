#include "layers/runtime/placeholders/null_services.h"

namespace epidemic::layers::runtime
{
bool NullVirtualFileSystem::Mount(const epidemic::foundation::Path &root)
{
    return !root.Empty();
}

bool NullVirtualFileSystem::Exists(const epidemic::foundation::Path &path) const
{
    return !path.Empty();
}

bool NullResourceManager::HasResource(std::string_view resource_id) const
{
    return !resource_id.empty();
}

void NullRenderer::RequestFrame()
{
}

bool NullScriptHost::IsReady() const
{
    return false;
}
} // namespace epidemic::layers::runtime
