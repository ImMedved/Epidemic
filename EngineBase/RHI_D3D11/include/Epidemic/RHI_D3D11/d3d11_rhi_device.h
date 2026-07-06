#pragma once

#include <Epidemic/Foundation/result.h>
#include <Epidemic/Platform/native_window_handle.h>
#include <Epidemic/RHI/irhi_device.h>
#include <Epidemic/RHI/presentation_surface_handle.h>

#include <memory>

namespace epidemic::rhi::d3d11
{
[[nodiscard]] constexpr PresentationSurfaceHandle
CreatePresentationSurfaceHandle(epidemic::platform::NativeWindowHandle native_window_handle) noexcept
{
    return PresentationSurfaceHandle(native_window_handle.Value());
}

[[nodiscard]] epidemic::foundation::Result<std::shared_ptr<IRhiDevice>>
CreateD3D11RhiDevice(const RhiDeviceDesc &device_desc = {});
} // namespace epidemic::rhi::d3d11