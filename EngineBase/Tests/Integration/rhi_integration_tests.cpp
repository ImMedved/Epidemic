// This file exercises integration between EngineBase support composition and available graphics backends.

#include "../test_assert.h"

#include <Epidemic/EngineBase/engine_base_support.h>
#include <Epidemic/RHI/descriptors.h>
#include <Epidemic/RHI/irhi_command_context.h>
#include <Epidemic/RHI/irhi_swap_chain.h>
#include <Epidemic/RHI/pixel_format.h>

#include <stdexcept>
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

    window_result.Value()->Close();
}

}

// Runs the RHI integration-test group.
int main()
{
    return epidemic::tests::RunNamedTests({
        {"NullGraphicsRuntimeRegistration", &TestNullGraphicsRuntimeRegistration},
        {"D3D11GraphicsRuntimeFailurePath", &TestD3D11GraphicsRuntimeFailurePath},
        {"D3D11PositiveSmoke", &TestD3D11PositiveSmoke},
    });
}