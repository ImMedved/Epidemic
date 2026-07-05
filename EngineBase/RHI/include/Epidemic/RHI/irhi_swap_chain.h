#pragma once

#include <Epidemic/Foundation/result.h>
#include <Epidemic/RHI/descriptors.h>

namespace epidemic::rhi
{
class IRhiSwapChain
{
  public:
    virtual ~IRhiSwapChain() = default;

    [[nodiscard]] virtual epidemic::foundation::Result<void> Present() = 0;
    [[nodiscard]] virtual epidemic::foundation::Result<void> Resize(std::uint32_t width, std::uint32_t height) = 0;
    [[nodiscard]] virtual std::uint32_t Width() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t Height() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t BufferCount() const noexcept = 0;
    [[nodiscard]] virtual RhiPixelFormat ColorFormat() const noexcept = 0;
};
} // namespace epidemic::rhi
