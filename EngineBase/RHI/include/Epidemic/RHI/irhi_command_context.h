#pragma once

#include <Epidemic/Foundation/result.h>
#include <Epidemic/RHI/descriptors.h>

namespace epidemic::rhi
{
// This file defines the command-context abstraction used by the baseline RHI.
// The current API is intentionally minimal: begin frame, clear, end frame.

class IRhiCommandContext
{
  public:
    virtual ~IRhiCommandContext() = default;

    // Begins recording or issuing commands for one frame.
    [[nodiscard]] virtual epidemic::foundation::Result<void> BeginFrame() = 0;

    // Executes a clear operation described by clear_desc.
    [[nodiscard]] virtual epidemic::foundation::Result<void> Clear(const RhiClearDesc &clear_desc) = 0;

    // Ends the active frame command scope.
    [[nodiscard]] virtual epidemic::foundation::Result<void> EndFrame() = 0;

    // Returns whether a frame is currently active on this context.
    [[nodiscard]] virtual bool IsFrameActive() const noexcept = 0;
};
} // namespace epidemic::rhi