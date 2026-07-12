#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Renderer/render_types.h"

namespace epidemic::runtime::renderer
{
// File note:
// Render scene contract for registering visual proxies and tracking their readiness
// and dirtiness without owning gameplay or world logic.
class IRenderScene
{
  public:
    virtual ~IRenderScene() = default;

    [[nodiscard]] virtual foundation::Result<RenderProxyId> RegisterProxy(const RenderProxyDesc& desc) = 0;
    [[nodiscard]] virtual foundation::Result<void> UnregisterProxy(RenderProxyId id) = 0;
    [[nodiscard]] virtual foundation::Result<void> MarkTransformDirty(RenderProxyId id) = 0;
    [[nodiscard]] virtual foundation::Result<void> MarkMaterialDirty(RenderProxyId id) = 0;
    [[nodiscard]] virtual RenderProxyState GetProxyState(RenderProxyId id) const = 0;
};
} // namespace epidemic::runtime::renderer
