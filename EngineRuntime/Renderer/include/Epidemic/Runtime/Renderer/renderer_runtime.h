#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Renderer/render_types.h"

namespace epidemic::runtime::renderer
{
// File note:
// Minimal renderer runtime contract for the early frame lifecycle. At this stage it only
// validates inputs and produces deterministic state transitions for tests and integration.
class IRendererRuntime
{
  public:
    virtual ~IRendererRuntime() = default;

    [[nodiscard]] virtual foundation::Result<void> PrepareFrame() = 0;
    [[nodiscard]] virtual foundation::Result<void> RenderFrame() = 0;
    [[nodiscard]] virtual RenderFrameState GetFrameState() const = 0;
};
} // namespace epidemic::runtime::renderer
