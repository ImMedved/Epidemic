#pragma once

#include <Epidemic/Foundation/result.h>
#include <Epidemic/RHI/irhi_device.h>

#include <memory>

namespace epidemic::rhi
{
// This file exposes the baseline software/null RHI backend used by tests and headless scenarios.
// The null device validates descriptors and simulates clear/present flow without touching a GPU.

// Creates a null RHI device or returns a validation error.
[[nodiscard]] epidemic::foundation::Result<std::shared_ptr<IRhiDevice>>
CreateNullRhiDevice(const RhiDeviceDesc &device_desc = {});
} // namespace epidemic::rhi