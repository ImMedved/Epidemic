#pragma once

#include <Epidemic/Foundation/result.h>
#include <Epidemic/RHI/irhi_device.h>

#include <memory>

namespace epidemic::rhi::d3d11
{
[[nodiscard]] epidemic::foundation::Result<std::shared_ptr<IRhiDevice>>
CreateD3D11RhiDevice(const RhiDeviceDesc &device_desc = {});
} // namespace epidemic::rhi::d3d11