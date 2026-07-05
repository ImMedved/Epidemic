#pragma once

#include <Epidemic/Foundation/result.h>
#include <Epidemic/RHI/pixel_format.h>
#include <Epidemic/RHI/presentation_surface_handle.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace epidemic::rhi
{
struct RhiColor
{
    float red{};
    float green{};
    float blue{};
    float alpha{1.0f};
};

struct RhiClearDesc
{
    bool clear_color{true};
    RhiColor color{};
};

struct RhiDeviceDesc
{
    bool enable_debug_validation{};
    std::string debug_name{"EpidemicNullRHI"};
};

struct RhiSwapChainDesc
{
    PresentationSurfaceHandle surface_handle{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t buffer_count{2};
    RhiPixelFormat color_format{RhiPixelFormat::B8G8R8A8_UNorm};
    bool vsync{true};
    std::string debug_name{"MainSwapChain"};
};

struct RhiBufferDesc
{
    std::size_t size_bytes{};
    std::string debug_name{"Buffer"};
};

struct RhiTextureDesc
{
    std::uint32_t width{};
    std::uint32_t height{};
    RhiPixelFormat format{RhiPixelFormat::Unknown};
    std::string debug_name{"Texture"};
};

[[nodiscard]] epidemic::foundation::Result<void> Validate(const RhiDeviceDesc &device_desc);
[[nodiscard]] epidemic::foundation::Result<void> Validate(const RhiSwapChainDesc &swap_chain_desc);
[[nodiscard]] epidemic::foundation::Result<void> Validate(const RhiBufferDesc &buffer_desc);
[[nodiscard]] epidemic::foundation::Result<void> Validate(const RhiTextureDesc &texture_desc);
[[nodiscard]] epidemic::foundation::Result<void> Validate(const RhiClearDesc &clear_desc);
} // namespace epidemic::rhi
