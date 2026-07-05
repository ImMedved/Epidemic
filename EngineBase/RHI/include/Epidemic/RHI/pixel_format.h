#pragma once

#include <cstdint>
#include <string_view>

namespace epidemic::rhi
{
enum class RhiPixelFormat : std::uint8_t
{
    Unknown,
    R8G8B8A8_UNorm,
    B8G8R8A8_UNorm,
};

[[nodiscard]] inline std::string_view ToString(RhiPixelFormat pixel_format) noexcept
{
    switch (pixel_format)
    {
    case RhiPixelFormat::Unknown:
        return "Unknown";
    case RhiPixelFormat::R8G8B8A8_UNorm:
        return "R8G8B8A8_UNorm";
    case RhiPixelFormat::B8G8R8A8_UNorm:
        return "B8G8R8A8_UNorm";
    }

    return "Unknown";
}
} // namespace epidemic::rhi
