#pragma once

#if defined(EPIDEMIC_RHI_D3D11_ENABLE_TEST_HOOKS)
namespace epidemic::rhi::d3d11::testing
{
enum class FaultPoint
{
    None,
    AfterDeviceCreate,
    DebugLayerUnavailable,
    GetBackBuffer,
    CreateRenderTargetView,
    PresentDeviceRemoved,
};

void SetFaultPoint(FaultPoint fault_point) noexcept;
void ClearFaultPoint() noexcept;
} // namespace epidemic::rhi::d3d11::testing
#endif
