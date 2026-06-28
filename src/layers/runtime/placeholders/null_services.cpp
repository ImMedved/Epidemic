#include "layers/runtime/placeholders/null_services.h"

namespace epidemic::layers::runtime
{
bool NullVirtualFileSystem::Mount(const epidemic::foundation::Path &root)
{
    static_cast<void>(root);
    return false;
}

bool NullVirtualFileSystem::Exists(const epidemic::foundation::Path &path) const
{
    static_cast<void>(path);
    return false;
}

bool NullResourceManager::HasResource(std::string_view resource_id) const
{
    static_cast<void>(resource_id);
    return false;
}

void NullRenderer::RequestFrame()
{
}

bool NullScriptHost::IsReady() const
{
    return false;
}
} // namespace epidemic::layers::runtime
