#include "../test_assert.h"

#include <Epidemic/EngineBase/engine_base_support.h>
#include <Epidemic/RHI/pixel_format.h>

namespace
{
using epidemic::tests::Assert;

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
}

int main()
{
    return epidemic::tests::RunNamedTests({
        {"NullGraphicsRuntimeRegistration", &TestNullGraphicsRuntimeRegistration},
        {"D3D11GraphicsRuntimeFailurePath", &TestD3D11GraphicsRuntimeFailurePath},
    });
}