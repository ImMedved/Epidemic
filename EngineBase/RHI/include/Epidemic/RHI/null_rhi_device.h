#pragma once

#include <Epidemic/Foundation/result.h>
#include <Epidemic/RHI/irhi_device.h>

#include <memory>

namespace epidemic::rhi
{
[[nodiscard]] epidemic::foundation::Result<std::shared_ptr<IRhiDevice>>
CreateNullRhiDevice(const RhiDeviceDesc &device_desc = {});
} // namespace epidemic::rhi
