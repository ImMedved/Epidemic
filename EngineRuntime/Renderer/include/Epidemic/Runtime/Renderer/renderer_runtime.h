#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Renderer/render_types.h"

namespace epidemic::runtime::renderer
{
// Threading: mutation must occur on the runtime thread.
// Concurrent reads/writes are not supported unless explicitly documented.
class IRendererRuntime
{
  public:
    virtual ~IRendererRuntime() = default;

    [[nodiscard]] virtual foundation::Result<void> PrepareFrame() = 0;
    [[nodiscard]] virtual foundation::Result<void> RenderFrame() = 0;
    [[nodiscard]] virtual RenderFrameState GetFrameState() const = 0;
};
} // namespace epidemic::runtime::renderer
