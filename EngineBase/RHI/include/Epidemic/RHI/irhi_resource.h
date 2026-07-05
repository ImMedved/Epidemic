#pragma once

#include <Epidemic/RHI/pixel_format.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace epidemic::rhi
{
class IRhiResource
{
  public:
    virtual ~IRhiResource() = default;

    [[nodiscard]] virtual std::string_view DebugName() const noexcept = 0;
};

class IRhiBuffer : public IRhiResource
{
  public:
    ~IRhiBuffer() override = default;

    [[nodiscard]] virtual std::size_t SizeBytes() const noexcept = 0;
};

class IRhiTexture : public IRhiResource
{
  public:
    ~IRhiTexture() override = default;

    [[nodiscard]] virtual std::uint32_t Width() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t Height() const noexcept = 0;
    [[nodiscard]] virtual RhiPixelFormat Format() const noexcept = 0;
};
} // namespace epidemic::rhi
