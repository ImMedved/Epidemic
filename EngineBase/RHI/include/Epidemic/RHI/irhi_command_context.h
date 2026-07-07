#pragma once

#include <Epidemic/Foundation/result.h>
#include <Epidemic/RHI/descriptors.h>

namespace epidemic::rhi
{
class IRhiCommandContext
{
  public:
    virtual ~IRhiCommandContext() = default;

    [[nodiscard]] virtual epidemic::foundation::Result<void> BeginFrame() = 0;
    [[nodiscard]] virtual epidemic::foundation::Result<void> Clear(const RhiClearDesc &clear_desc) = 0;
    [[nodiscard]] virtual epidemic::foundation::Result<void> EndFrame() = 0;
    [[nodiscard]] virtual bool IsFrameActive() const noexcept = 0;
};
} // namespace epidemic::rhi
