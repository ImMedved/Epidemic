#include "layers/runtime/placeholders/null_services.h"

namespace epidemic::layers::runtime
{
bool NullVirtualFileSystem::Mount(const std::filesystem::path &root)
{
    return !root.empty();
}

bool NullVirtualFileSystem::Exists(const std::filesystem::path &path) const
{
    return !path.empty();
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
