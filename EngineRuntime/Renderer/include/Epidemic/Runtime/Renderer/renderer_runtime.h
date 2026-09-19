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

    // PrepareFrame resolves/stages fallible scene/resource/pose observations without opening a backend frame.
    // Calling it again before RenderFrame replaces the staged frame.
    [[nodiscard]] virtual foundation::Result<void> PrepareFrame() = 0;
    // RenderFrame consumes a successfully prepared frame. If AbortFrame could not confirm closure after
    // Submit/End failure, the next PrepareFrame/RenderFrame/Shutdown first retries reconciliation.
    [[nodiscard]] virtual foundation::Result<void> RenderFrame() = 0;
    [[nodiscard]] virtual foundation::Result<void> Shutdown() = 0;
    [[nodiscard]] virtual RenderFrameState GetFrameState() const = 0;
};
} // namespace epidemic::runtime::renderer
