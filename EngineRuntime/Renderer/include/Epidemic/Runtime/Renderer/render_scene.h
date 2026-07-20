#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Renderer/render_types.h"

namespace epidemic::runtime::renderer
{
// Threading: mutation must occur on the runtime thread.
// Concurrent reads/writes are not supported unless explicitly documented.
class IRenderScene
{
  public:
    virtual ~IRenderScene() = default;

    [[nodiscard]] virtual foundation::Result<RenderProxyId> RegisterProxy(const RenderProxyDesc& desc) = 0;
    [[nodiscard]] virtual foundation::Result<void> DestroyProxy(RenderProxyId id) = 0;
    [[nodiscard]] virtual foundation::Result<void> FlushDeferredDestroys() = 0;
    [[nodiscard]] virtual foundation::Result<void> MarkTransformDirty(RenderProxyId id) = 0;
    [[nodiscard]] virtual foundation::Result<void> MarkMaterialDirty(RenderProxyId id) = 0;
    [[nodiscard]] virtual foundation::Result<void> SetProxyVisibility(RenderProxyId id, RenderProxyVisibility visibility) = 0;
    [[nodiscard]] virtual RenderProxyLifecycle GetProxyLifecycle(RenderProxyId id) const = 0;
    [[nodiscard]] virtual RenderProxyReadiness GetProxyReadiness(RenderProxyId id) const = 0;
    [[nodiscard]] virtual RenderProxyVisibility GetProxyVisibility(RenderProxyId id) const = 0;
    [[nodiscard]] virtual RenderProxyDirtyMask GetProxyDirtyFlags(RenderProxyId id) const = 0;
};
} // namespace epidemic::runtime::renderer
