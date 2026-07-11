#pragma once

#include <Epidemic/Foundation/result.h>
#include <Epidemic/RHI/irhi_device.h>

#include <memory>

namespace epidemic::rhi::d3d11
{
// This file exposes the D3D11 RHI backend factory used by EngineBase support wiring.
// Backend creation returns Result because device creation and swap-chain setup are expected runtime failures.

// Creates a D3D11 RHI device or returns an Error describing why backend initialization failed.
[[nodiscard]] epidemic::foundation::Result<std::shared_ptr<IRhiDevice>>
CreateD3D11RhiDevice(const RhiDeviceDesc &device_desc = {});
} // namespace epidemic::rhi::d3d11