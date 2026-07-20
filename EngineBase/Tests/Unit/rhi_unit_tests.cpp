// This file exercises the RHI baseline contracts and descriptor validation paths.

#include "../test_assert.h"

#include <Epidemic/RHI/descriptors.h>
#include <Epidemic/RHI/null_rhi_device.h>

#include <memory>

namespace
{
using epidemic::tests::Assert;

// Verifies null-RHI device, command-context, swap-chain, and validation behavior.
void TestRhiContracts()
{
    const epidemic::rhi::RhiDeviceDesc invalid_device_desc{false, {}};
    const auto invalid_device_result = epidemic::rhi::CreateNullRhiDevice(invalid_device_desc);
    Assert(!invalid_device_result.HasValue(), "RHI device creation must reject empty device names");

    const auto device_result = epidemic::rhi::CreateNullRhiDevice(epidemic::rhi::RhiDeviceDesc{true, "UnitTestNullRHI"});
    Assert(device_result.HasValue(), "Null RHI device creation must succeed for valid descriptors");
    const auto device = device_result.Value();

    const auto command_context_result = device->CreateCommandContext();
    Assert(command_context_result.HasValue(), "RHI device must create a command context");
    const auto command_context = command_context_result.Value();
    Assert(!command_context->Clear(epidemic::rhi::RhiClearDesc{}).HasValue(),
           "RHI command context must reject Clear outside an active frame");
    Assert(command_context->BeginFrame().HasValue(), "RHI command context must begin a frame");

    epidemic::rhi::RhiClearDesc clear_desc;
    clear_desc.color = epidemic::rhi::RhiColor{0.1f, 0.2f, 0.3f, 1.0f};
    Assert(command_context->Clear(clear_desc).HasValue(), "RHI command context must accept valid clear operations");
    Assert(command_context->EndFrame().HasValue(), "RHI command context must end an active frame");

    epidemic::rhi::RhiSwapChainDesc invalid_swap_chain_desc;
    invalid_swap_chain_desc.width = 1280;
    invalid_swap_chain_desc.height = 720;
    invalid_swap_chain_desc.buffer_count = 2;
    invalid_swap_chain_desc.color_format = epidemic::rhi::RhiPixelFormat::B8G8R8A8_UNorm;
    const auto invalid_swap_chain_result = device->CreateSwapChain(invalid_swap_chain_desc);
    Assert(!invalid_swap_chain_result.HasValue(), "RHI swap chain creation must reject missing surface handles");

    epidemic::rhi::RhiSwapChainDesc swap_chain_desc;
    swap_chain_desc.surface_handle = epidemic::rhi::PresentationSurfaceHandle(reinterpret_cast<void *>(1));
    swap_chain_desc.width = 1280;
    swap_chain_desc.height = 720;
    swap_chain_desc.buffer_count = 2;
    swap_chain_desc.color_format = epidemic::rhi::RhiPixelFormat::B8G8R8A8_UNorm;
    const auto swap_chain_result = device->CreateSwapChain(swap_chain_desc);
    Assert(swap_chain_result.HasValue(), "RHI device must create a swap chain for valid descriptors");
    const auto swap_chain = swap_chain_result.Value();
    Assert(!swap_chain->Resize(0, 1080).HasValue(), "Resize(0, h) must fail");
    Assert(!swap_chain->Resize(1920, 0).HasValue(), "Resize(w, 0) must fail");
    Assert(!swap_chain->Resize(0, 0).HasValue(), "Resize(0, 0) must fail");
    Assert(swap_chain->Resize(1920, 1080).HasValue(), "Valid swap chain resize must succeed");
}
}

// Runs the RHI unit-test group.
int main()
{
    return epidemic::tests::RunNamedTests({{"RhiContracts", &TestRhiContracts}});
}