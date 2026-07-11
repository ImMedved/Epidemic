#pragma once

#include <Epidemic/Foundation/result.h>
#include <Epidemic/RHI/pixel_format.h>
#include <Epidemic/RHI/presentation_surface_handle.h>

#include <cstdint>
#include <string>

namespace epidemic::rhi
{
// This file defines the value descriptors used by the baseline RHI interfaces.
// Validation helpers keep expected runtime setup failures in Result instead of exceptions.

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

// Validates device creation parameters.
[[nodiscard]] epidemic::foundation::Result<void> Validate(const RhiDeviceDesc &device_desc);

// Validates swap-chain creation parameters.
[[nodiscard]] epidemic::foundation::Result<void> Validate(const RhiSwapChainDesc &swap_chain_desc);

// Validates a clear operation descriptor.
[[nodiscard]] epidemic::foundation::Result<void> Validate(const RhiClearDesc &clear_desc);
} // namespace epidemic::rhi