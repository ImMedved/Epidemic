#pragma once

#include "layers/runtime/interfaces/irenderer.h"
#include "layers/runtime/interfaces/iresource_manager.h"
#include "layers/runtime/interfaces/iscript_host.h"
#include "layers/runtime/interfaces/ivirtual_file_system.h"

namespace epidemic::layers::runtime
{
class NullVirtualFileSystem final : public IVirtualFileSystem
{
  public:
    bool Mount(const epidemic::foundation::Path &root) override;
    [[nodiscard]] bool Exists(const epidemic::foundation::Path &path) const override;
};

class NullResourceManager final : public IResourceManager
{
  public:
    [[nodiscard]] bool HasResource(std::string_view resource_id) const override;
};

class NullRenderer final : public IRenderer
{
  public:
    void RequestFrame() override;
};

class NullScriptHost final : public IScriptHost
{
  public:
    [[nodiscard]] bool IsReady() const override;
};
} // namespace epidemic::layers::runtime
