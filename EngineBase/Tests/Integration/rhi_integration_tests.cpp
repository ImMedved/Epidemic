// This file exercises integration between EngineBase support composition and available graphics backends.

#include "../test_assert.h"

#include <Epidemic/EngineBase/engine_base_support.h>
#include <Epidemic/RHI/descriptors.h>
#include <Epidemic/RHI/irhi_command_context.h>
#include <Epidemic/RHI/irhi_swap_chain.h>
#include <Epidemic/RHI/pixel_format.h>
#include <Epidemic/RHI_D3D11/d3d11_rhi_device.h>

#if defined(EPIDEMIC_RHI_D3D11_ENABLE_TEST_HOOKS)
#include "d3d11_test_hooks.h"
#endif

#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
using epidemic::tests::Assert;

// Verifies that the null graphics runtime composes successfully and registers expected services.
void TestNullGraphicsRuntimeRegistration()
{
    epidemic::core::Application application({"RhiIntegration"});
    static_cast<void>(epidemic::enginebase::RegisterEngineBase(
        application, {.runtime_name = "RhiIntegration", .log_module = "RhiIntegration"}));

    const auto graphics_runtime_result = epidemic::enginebase::RegisterGraphicsRuntime(
        application, {.backend = epidemic::enginebase::GraphicsBackend::Null, .debug_name = "NullGraphicsRuntime"});
    Assert(graphics_runtime_result.HasValue(), "RegisterGraphicsRuntime(Null) must succeed");
    const auto graphics_runtime = graphics_runtime_result.Value();
    Assert(graphics_runtime.device != nullptr, "RegisterGraphicsRuntime(Null) must return a device");
    Assert(graphics_runtime.command_context != nullptr, "RegisterGraphicsRuntime(Null) must return a command context");
    Assert(application.Services().Contains<epidemic::rhi::IRhiDevice>(), "Null graphics runtime must register IRhiDevice");
    Assert(application.Services().Contains<epidemic::rhi::IRhiCommandContext>(),
           "Null graphics runtime must register IRhiCommandContext");
}

// Verifies that D3D11 setup failures surface as Result errors rather than hidden exceptions.
void TestD3D11GraphicsRuntimeFailurePath()
{
    epidemic::core::Application application({"RhiD3D11Failure"});
    static_cast<void>(epidemic::enginebase::RegisterEngineBase(
        application, {.runtime_name = "RhiD3D11Failure", .log_module = "RhiD3D11Failure"}));

    const auto result = epidemic::enginebase::RegisterGraphicsRuntime(
        application, {.backend = epidemic::enginebase::GraphicsBackend::D3D11, .debug_name = std::string{}});
    Assert(!result.HasValue(), "RegisterGraphicsRuntime(D3D11) must report expected runtime failure via Result");
    Assert(result.GetError().message.find("debug name") != std::string::npos,
           "RegisterGraphicsRuntime(D3D11) must surface a meaningful failure message for invalid setup");
}
// Exercises the real D3D11 device/swap-chain path with an actual hidden Win32 window.
void TestD3D11PositiveSmoke()
{
    epidemic::core::Application application({"RhiD3D11Positive"});
    static_cast<void>(epidemic::enginebase::RegisterEngineBase(
        application, {.runtime_name = "RhiD3D11Positive", .log_module = "RhiD3D11Positive"}));
    static_cast<void>(epidemic::enginebase::RegisterWindowsRuntime(application));

    const auto graphics = epidemic::enginebase::RegisterGraphicsRuntime(
        application,
        {.backend = epidemic::enginebase::GraphicsBackend::D3D11,
         .enable_debug_validation = false,
         .debug_name = "RhiD3D11PositiveDevice"});
    Assert(graphics.HasValue(), "D3D11 graphics runtime must initialize for the positive smoke test");

    const auto window_result = epidemic::enginebase::CreateMainWindow(
        application, epidemic::platform::WindowCreateInfo{"Hidden D3D11 Test", 320, 240, false});
    Assert(window_result.HasValue(), "D3D11 smoke test window creation must succeed");
    if (!graphics.HasValue() || !window_result.HasValue())
    {
        return;
    }

    epidemic::rhi::RhiSwapChainDesc swap_desc{};
    swap_desc.width = 320;
    swap_desc.height = 240;
    swap_desc.buffer_count = 2;
    swap_desc.color_format = epidemic::rhi::RhiPixelFormat::B8G8R8A8_UNorm;
    swap_desc.vsync = false;
    swap_desc.debug_name = "RhiD3D11PositiveSwapChain";
    const auto swap_chain = epidemic::enginebase::RegisterMainSwapChain(application, window_result.Value(), swap_desc);
    Assert(swap_chain.HasValue(), "D3D11 swap chain creation must succeed");
    if (!swap_chain.HasValue())
    {
        window_result.Value()->Close();
        return;
    }

    epidemic::rhi::RhiClearDesc clear{};
    clear.color = epidemic::rhi::RhiColor{0.05f, 0.10f, 0.15f, 1.0f};
    Assert(graphics.Value().command_context->BeginFrame().HasValue(), "D3D11 BeginFrame must succeed");
    Assert(graphics.Value().command_context->Clear(clear).HasValue(), "D3D11 Clear must succeed");
    Assert(graphics.Value().command_context->EndFrame().HasValue(), "D3D11 EndFrame must succeed");
    Assert(swap_chain.Value()->Present().HasValue(), "D3D11 Present must succeed");

    Assert(swap_chain.Value()->Resize(400, 300).HasValue(), "D3D11 Resize must succeed");
    Assert(swap_chain.Value()->Width() == 400 && swap_chain.Value()->Height() == 300,
           "D3D11 swap chain dimensions must update after resize");
    Assert(graphics.Value().command_context->BeginFrame().HasValue(), "D3D11 BeginFrame after resize must succeed");
    Assert(graphics.Value().command_context->Clear(clear).HasValue(), "D3D11 Clear after resize must succeed");
    Assert(graphics.Value().command_context->EndFrame().HasValue(), "D3D11 EndFrame after resize must succeed");
    Assert(swap_chain.Value()->Present().HasValue(), "D3D11 Present after resize must succeed");

    const auto failed_resize = swap_chain.Value()->Resize(std::numeric_limits<std::uint32_t>::max(),
                                                          std::numeric_limits<std::uint32_t>::max());
    Assert(!failed_resize.HasValue(), "D3D11 must report an impossible resize as a failure");
    Assert(swap_chain.Value()->Width() == 400 && swap_chain.Value()->Height() == 300,
           "Failed D3D11 resize must preserve the last usable logical dimensions");
    const auto present_after_failed_resize = swap_chain.Value()->Present();
    Assert(present_after_failed_resize.HasValue() ||
               present_after_failed_resize.GetError().code == "rhi.d3d11.recreate_required",
           "D3D11 failed resize must either recover the old target or expose recreate-required state");

    window_result.Value()->Close();
}


// Verifies D3D11 factory cleanup after native acquisition and the optional debug-layer contract.
void TestD3D11FactoryCleanupAndDebugLayer()
{
#if defined(EPIDEMIC_RHI_D3D11_ENABLE_TEST_HOOKS)
    epidemic::rhi::d3d11::testing::SetFaultPoint(
        epidemic::rhi::d3d11::testing::FaultPoint::AfterDeviceCreate);
    const auto injected_failure = epidemic::rhi::d3d11::CreateD3D11RhiDevice(
        epidemic::rhi::RhiDeviceDesc{false, "RhiD3D11InjectedFactoryFailure"});
    Assert(!injected_failure.HasValue(), "Injected post-create D3D11 factory failure must return Result failure");
    Assert(injected_failure.GetError().code == "rhi.d3d11.injected_failure",
           "Injected post-create D3D11 factory failure must preserve its stable error code");
    epidemic::rhi::d3d11::testing::ClearFaultPoint();
#endif

    const auto retry = epidemic::rhi::d3d11::CreateD3D11RhiDevice(
        epidemic::rhi::RhiDeviceDesc{false, "RhiD3D11FactoryRetry"});
    Assert(retry.HasValue(), "D3D11 factory must remain usable after a post-acquisition failure");

#if defined(EPIDEMIC_RHI_D3D11_ENABLE_TEST_HOOKS)
    epidemic::rhi::d3d11::testing::SetFaultPoint(
        epidemic::rhi::d3d11::testing::FaultPoint::DebugLayerUnavailable);
    const auto injected_debug_failure = epidemic::rhi::d3d11::CreateD3D11RhiDevice(
        epidemic::rhi::RhiDeviceDesc{true, "RhiD3D11InjectedMissingDebugLayer"});
    Assert(!injected_debug_failure.HasValue() &&
               injected_debug_failure.GetError().code == "rhi.d3d11.debug_layer_unavailable",
           "Injected missing D3D11 debug layer must return its dedicated controlled error");
    epidemic::rhi::d3d11::testing::ClearFaultPoint();
#endif

    const auto debug_device = epidemic::rhi::d3d11::CreateD3D11RhiDevice(
        epidemic::rhi::RhiDeviceDesc{true, "RhiD3D11DebugLayer"});
    if (debug_device.HasValue())
    {
        Assert(debug_device.Value()->Descriptor().enable_debug_validation,
               "D3D11 debug-layer success must preserve the debug-validation descriptor");
    }
    else
    {
        Assert(debug_device.GetError().code == "rhi.d3d11.debug_layer_unavailable",
               "Unavailable D3D11 debug layer must return its dedicated controlled error");
    }
}

// Exercises partial swap-chain creation cleanup, render-target recreation, zero-area rejection and child lifetimes.
void TestD3D11SwapChainRecoveryAndLifetime()
{
    epidemic::core::Application application({"RhiD3D11Recovery"});
    static_cast<void>(epidemic::enginebase::RegisterEngineBase(
        application, {.runtime_name = "RhiD3D11Recovery", .log_module = "RhiD3D11Recovery"}));
    static_cast<void>(epidemic::enginebase::RegisterWindowsRuntime(application));

    auto device_result = epidemic::rhi::d3d11::CreateD3D11RhiDevice(
        epidemic::rhi::RhiDeviceDesc{false, "RhiD3D11RecoveryDevice"});
    Assert(device_result.HasValue(), "D3D11 recovery test device creation must succeed");
    if (!device_result.HasValue())
    {
        return;
    }
    auto device = std::move(device_result).Value();

    auto context_result = device->CreateCommandContext();
    Assert(context_result.HasValue(), "D3D11 recovery test command-context creation must succeed");
    if (!context_result.HasValue())
    {
        return;
    }
    auto context = std::move(context_result).Value();

    epidemic::rhi::RhiClearDesc clear{};
    clear.color = epidemic::rhi::RhiColor{0.12f, 0.18f, 0.24f, 1.0f};
    Assert(!context->EndFrame().HasValue(), "D3D11 EndFrame without BeginFrame must fail");
    Assert(!context->Clear(clear).HasValue(), "D3D11 Clear outside a frame must fail");
    Assert(context->BeginFrame().HasValue(), "D3D11 BeginFrame must enter active state");
    Assert(!context->BeginFrame().HasValue(), "D3D11 nested BeginFrame must fail");
    Assert(context->EndFrame().HasValue(), "D3D11 EndFrame must leave active state");

    const auto window_result = epidemic::enginebase::CreateMainWindow(
        application, epidemic::platform::WindowCreateInfo{"Hidden D3D11 Recovery Test", 320, 240, false});
    Assert(window_result.HasValue(), "D3D11 recovery test window creation must succeed");
    if (!window_result.HasValue())
    {
        return;
    }
    const auto window = window_result.Value();

    epidemic::rhi::RhiSwapChainDesc swap_desc{};
    swap_desc.surface_handle = epidemic::rhi::PresentationSurfaceHandle(window->GetNativeHandle().Value());
    swap_desc.width = 320;
    swap_desc.height = 240;
    swap_desc.buffer_count = 2;
    swap_desc.color_format = epidemic::rhi::RhiPixelFormat::B8G8R8A8_UNorm;
    swap_desc.vsync = false;
    swap_desc.debug_name = "RhiD3D11RecoverySwapChain";

#if defined(EPIDEMIC_RHI_D3D11_ENABLE_TEST_HOOKS)
    epidemic::rhi::d3d11::testing::SetFaultPoint(
        epidemic::rhi::d3d11::testing::FaultPoint::GetBackBuffer);
    const auto partial_create = device->CreateSwapChain(swap_desc);
    Assert(!partial_create.HasValue(), "Injected back-buffer acquisition failure must fail swap-chain creation");
    Assert(partial_create.GetError().code == "rhi.d3d11.get_back_buffer_failed",
           "Injected swap-chain creation failure must expose the back-buffer operation");
    epidemic::rhi::d3d11::testing::ClearFaultPoint();
#endif

    auto swap_chain_result = device->CreateSwapChain(swap_desc);
    Assert(swap_chain_result.HasValue(), "D3D11 swap chain must be creatable after partial creation cleanup");
    if (!swap_chain_result.HasValue())
    {
        window->Close();
        return;
    }
    auto swap_chain = std::move(swap_chain_result).Value();

    auto invalid_desc = swap_desc;
    invalid_desc.surface_handle = epidemic::rhi::PresentationSurfaceHandle(reinterpret_cast<void *>(1));
    invalid_desc.debug_name = "RhiD3D11InvalidWindowSwapChain";
    const auto invalid_replacement = device->CreateSwapChain(invalid_desc);
    Assert(!invalid_replacement.HasValue(), "D3D11 swap chain must reject a non-window Win32 handle");
    Assert(invalid_replacement.GetError().code == "rhi.invalid_surface_handle",
           "Non-window native handle must be rejected before a DXGI swap-chain call");

    Assert(context->BeginFrame().HasValue(), "D3D11 BeginFrame must remain valid after failed replacement creation");
    Assert(context->Clear(clear).HasValue(), "Failed replacement creation must preserve the previous active target");
    Assert(context->EndFrame().HasValue(), "D3D11 EndFrame must close the preserved frame");
    Assert(swap_chain->Present().HasValue(), "Preserved D3D11 swap chain must remain presentable");

    const auto width_before_minimize = swap_chain->Width();
    const auto height_before_minimize = swap_chain->Height();
    Assert(swap_chain->Resize(width_before_minimize, height_before_minimize).HasValue(),
           "D3D11 same-size resize must be an explicit successful no-op");
    Assert(swap_chain->Present().HasValue(), "D3D11 same-size resize must preserve presentation state");
    const auto zero_resize = swap_chain->Resize(0, 0);
    Assert(!zero_resize.HasValue(), "D3D11 zero-area resize must be rejected before DXGI ResizeBuffers");
    Assert(zero_resize.GetError().code == "rhi.invalid_swap_chain_size",
           "D3D11 zero-area resize must use the baseline invalid-size error");
    Assert(swap_chain->Width() == width_before_minimize && swap_chain->Height() == height_before_minimize,
           "Rejected minimized resize must preserve usable logical dimensions");
    Assert(swap_chain->Present().HasValue(), "Rejected minimized resize must leave the old target usable");

#if defined(EPIDEMIC_RHI_D3D11_ENABLE_TEST_HOOKS)
    epidemic::rhi::d3d11::testing::SetFaultPoint(
        epidemic::rhi::d3d11::testing::FaultPoint::CreateRenderTargetView);
    const auto failed_recreate = swap_chain->Resize(400, 300);
    Assert(!failed_recreate.HasValue(), "Injected RTV recreation failure must fail D3D11 resize");
    Assert(failed_recreate.GetError().code == "rhi.d3d11.create_render_target_failed",
           "Injected RTV recreation failure must preserve its operation error");
    epidemic::rhi::d3d11::testing::ClearFaultPoint();

    const auto present_without_rtv = swap_chain->Present();
    Assert(!present_without_rtv.HasValue() && present_without_rtv.GetError().code == "rhi.d3d11.recreate_required",
           "D3D11 Present must reject a swap chain whose RTV recreation failed");
    Assert(context->BeginFrame().HasValue(), "D3D11 frame may begin while presentation target awaits recreation");
    const auto clear_without_rtv = context->Clear(clear);
    Assert(!clear_without_rtv.HasValue() && clear_without_rtv.GetError().code == "rhi.d3d11.recreate_required",
           "D3D11 Clear must not silently succeed with a missing/stale render target");
    Assert(context->EndFrame().HasValue(), "Failed D3D11 clear must still allow logical frame closure");

    Assert(swap_chain->Resize(400, 300).HasValue(), "Retrying D3D11 resize must recreate back-buffer resources");
    Assert(context->BeginFrame().HasValue(), "D3D11 BeginFrame after RTV recreation must succeed");
    Assert(context->Clear(clear).HasValue(), "D3D11 Clear after RTV recreation must use the new target");
    Assert(context->EndFrame().HasValue(), "D3D11 EndFrame after RTV recreation must succeed");
    Assert(swap_chain->Present().HasValue(), "D3D11 Present after RTV recreation must succeed");
#endif

    swap_chain.reset();
    Assert(context->BeginFrame().HasValue(), "D3D11 context remains usable after swap-chain destruction");
    const auto clear_after_swap_chain_destroy = context->Clear(clear);
    Assert(!clear_after_swap_chain_destroy.HasValue() &&
               clear_after_swap_chain_destroy.GetError().code == "rhi.d3d11.no_swap_chain",
           "Destroyed active D3D11 swap chain must not leave a stale presentation target");
    Assert(context->EndFrame().HasValue(), "D3D11 frame must close after stale-target rejection");

    auto replacement_result = device->CreateSwapChain(swap_desc);
    Assert(replacement_result.HasValue(), "D3D11 swap chain must be recreatable after active-target destruction");
    if (!replacement_result.HasValue())
    {
        window->Close();
        return;
    }
    swap_chain = std::move(replacement_result).Value();

    device.reset();
    Assert(context->BeginFrame().HasValue(), "D3D11 child objects must retain shared native device/context lifetime");
    Assert(context->Clear(clear).HasValue(), "D3D11 command context must remain valid after device wrapper destruction");
    Assert(context->EndFrame().HasValue(), "D3D11 retained command context must close its frame");
    Assert(swap_chain->Present().HasValue(), "D3D11 swap chain must retain device state after wrapper destruction");

#if defined(EPIDEMIC_RHI_D3D11_ENABLE_TEST_HOOKS)
    epidemic::rhi::d3d11::testing::SetFaultPoint(
        epidemic::rhi::d3d11::testing::FaultPoint::PresentDeviceRemoved);
    const auto lost_present = swap_chain->Present();
    Assert(!lost_present.HasValue() && lost_present.GetError().code == "rhi.d3d11.device_lost",
           "D3D11 device-loss HRESULT must become stable rhi.d3d11.device_lost");
    epidemic::rhi::d3d11::testing::ClearFaultPoint();
    const auto begin_after_device_lost = context->BeginFrame();
    Assert(!begin_after_device_lost.HasValue() && begin_after_device_lost.GetError().code == "rhi.d3d11.device_lost",
           "D3D11 context must reject new native work after device loss");
#endif

    window->Close();
}

}

// Runs the RHI integration-test group.
int main()
{
    return epidemic::tests::RunNamedTests({
        {"NullGraphicsRuntimeRegistration", &TestNullGraphicsRuntimeRegistration},
        {"D3D11GraphicsRuntimeFailurePath", &TestD3D11GraphicsRuntimeFailurePath},
        {"D3D11PositiveSmoke", &TestD3D11PositiveSmoke},
        {"D3D11FactoryCleanupAndDebugLayer", &TestD3D11FactoryCleanupAndDebugLayer},
        {"D3D11SwapChainRecoveryAndLifetime", &TestD3D11SwapChainRecoveryAndLifetime},
    });
}
