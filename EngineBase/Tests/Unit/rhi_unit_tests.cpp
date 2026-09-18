// This file exercises the RHI baseline contracts and descriptor validation paths.

#include "../test_assert.h"

#include <Epidemic/RHI/descriptors.h>
#include <Epidemic/RHI/null_rhi_device.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>

namespace
{
std::atomic<std::ptrdiff_t> g_fail_allocation_index{0};

[[nodiscard]] void *AllocateTestMemory(std::size_t size)
{
    const auto countdown = g_fail_allocation_index.load(std::memory_order_relaxed);
    if (countdown > 0 && g_fail_allocation_index.fetch_sub(1, std::memory_order_relaxed) == 1)
    {
        throw std::bad_alloc{};
    }

    if (void *memory = std::malloc(size == 0 ? 1 : size))
    {
        return memory;
    }
    throw std::bad_alloc{};
}
}

void *operator new(std::size_t size)
{
    return AllocateTestMemory(size);
}

void *operator new[](std::size_t size)
{
    return AllocateTestMemory(size);
}

void operator delete(void *memory) noexcept
{
    std::free(memory);
}

void operator delete[](void *memory) noexcept
{
    std::free(memory);
}

void operator delete(void *memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void *memory, std::size_t) noexcept
{
    std::free(memory);
}

namespace
{
using epidemic::tests::Assert;

void FailAllocationAt(std::ptrdiff_t index) noexcept
{
    g_fail_allocation_index.store(index, std::memory_order_relaxed);
}

void DisableAllocationFailure() noexcept
{
    g_fail_allocation_index.store(0, std::memory_order_relaxed);
}

[[nodiscard]] epidemic::rhi::RhiSwapChainDesc MakeValidSwapChainDesc()
{
    epidemic::rhi::RhiSwapChainDesc descriptor;
    descriptor.surface_handle = epidemic::rhi::PresentationSurfaceHandle(reinterpret_cast<void *>(1));
    descriptor.width = 1280;
    descriptor.height = 720;
    descriptor.buffer_count = 2;
    descriptor.color_format = epidemic::rhi::RhiPixelFormat::B8G8R8A8_UNorm;
    descriptor.debug_name = "UnitTestSwapChain";
    return descriptor;
}

[[nodiscard]] std::shared_ptr<epidemic::rhi::IRhiDevice> MakeDevice()
{
    auto result = epidemic::rhi::CreateNullRhiDevice(epidemic::rhi::RhiDeviceDesc{true, "UnitTestNullRHI"});
    Assert(result.HasValue(), "Null RHI device creation must succeed for valid descriptors");
    return result.Value();
}

// Verifies the small backend-neutral value/query contracts used by the RHI interfaces.
void TestValueAndQueryContracts()
{
    const epidemic::rhi::PresentationSurfaceHandle empty_surface;
    Assert(!empty_surface.IsValid(), "Default presentation surface handle must be invalid");
    Assert(empty_surface.Value() == nullptr, "Invalid presentation surface handle must expose nullptr");

    auto *const raw_surface = reinterpret_cast<void *>(0x1234);
    const epidemic::rhi::PresentationSurfaceHandle surface(raw_surface);
    Assert(surface.IsValid(), "Non-null presentation surface handle must be valid");
    Assert(surface.Value() == raw_surface, "Presentation surface handle must preserve the wrapped pointer");
    Assert(surface.As<void *>() == raw_surface, "Typed presentation surface conversion must preserve the wrapped pointer");
    Assert(surface == epidemic::rhi::PresentationSurfaceHandle(raw_surface),
           "Presentation surface equality must compare wrapped pointer identity");

    Assert(epidemic::rhi::ToString(epidemic::rhi::RhiPixelFormat::Unknown) == "Unknown",
           "Unknown pixel format must have a stable diagnostic name");
    Assert(epidemic::rhi::ToString(epidemic::rhi::RhiPixelFormat::R8G8B8A8_UNorm) == "R8G8B8A8_UNorm",
           "R8G8B8A8 format must have a stable diagnostic name");
    Assert(epidemic::rhi::ToString(epidemic::rhi::RhiPixelFormat::B8G8R8A8_UNorm) == "B8G8R8A8_UNorm",
           "B8G8R8A8 format must have a stable diagnostic name");
    Assert(epidemic::rhi::ToString(static_cast<epidemic::rhi::RhiPixelFormat>(0xffu)) == "Unknown",
           "Out-of-range pixel format must stringify as Unknown without indexing invalid storage");
}

void AssertFailureCode(const epidemic::foundation::Result<void> &result, std::string_view expected_code,
                       std::string_view message)
{
    Assert(!result.HasValue(), message);
    Assert(result.GetError().code == expected_code, "RHI failure must expose the expected machine-readable code");
}

// Verifies descriptor rejection and that failed creation does not poison later valid creation.
void TestDescriptorValidationAndCreationAtomicity()
{
    const epidemic::rhi::RhiDeviceDesc invalid_device_desc{false, {}};
    const auto invalid_device_result = epidemic::rhi::CreateNullRhiDevice(invalid_device_desc);
    Assert(!invalid_device_result.HasValue(), "RHI device creation must reject empty device names");
    Assert(invalid_device_result.GetError().code == "rhi.empty_device_name",
           "Invalid device creation must expose a stable error code");

    const auto device = MakeDevice();
    Assert(device->BackendName() == "NullRHI", "Null RHI must expose its stable backend name");
    Assert(device->Descriptor().debug_name == "UnitTestNullRHI", "Device creation must preserve its descriptor");

    const auto command_context_result = device->CreateCommandContext();
    Assert(command_context_result.HasValue(), "RHI device must create a command context after an earlier factory failure");
    Assert(!command_context_result.Value()->IsFrameActive(), "New command context must start outside a frame");

    auto descriptor = MakeValidSwapChainDesc();
    descriptor.surface_handle = {};
    auto result = device->CreateSwapChain(descriptor);
    Assert(!result.HasValue(), "RHI swap chain creation must reject missing surface handles");
    Assert(result.GetError().code == "rhi.invalid_surface_handle", "Missing surface must have a stable error code");

    descriptor = MakeValidSwapChainDesc();
    descriptor.width = 0;
    result = device->CreateSwapChain(descriptor);
    Assert(!result.HasValue(), "RHI swap chain creation must reject zero width");

    descriptor = MakeValidSwapChainDesc();
    descriptor.buffer_count = 0;
    result = device->CreateSwapChain(descriptor);
    Assert(!result.HasValue(), "RHI swap chain creation must reject zero buffers");

    descriptor = MakeValidSwapChainDesc();
    descriptor.color_format = epidemic::rhi::RhiPixelFormat::Unknown;
    result = device->CreateSwapChain(descriptor);
    Assert(!result.HasValue(), "RHI swap chain creation must reject Unknown pixel format");

    descriptor = MakeValidSwapChainDesc();
    descriptor.color_format = static_cast<epidemic::rhi::RhiPixelFormat>(0xffu);
    result = device->CreateSwapChain(descriptor);
    Assert(!result.HasValue(), "RHI swap chain creation must reject out-of-range pixel formats");
    Assert(result.GetError().code == "rhi.invalid_color_format", "Invalid enum must use the color-format error code");

    result = device->CreateSwapChain(MakeValidSwapChainDesc());
    Assert(result.HasValue(), "Failed swap-chain creation must not poison a later valid creation");
    const auto active_swap_chain = result.Value();
    Assert(active_swap_chain->Width() == 1280 && active_swap_chain->Height() == 720,
           "Successful creation after failures must publish only the valid dimensions");
    Assert(active_swap_chain->BufferCount() == 2, "Swap chain must preserve the validated buffer count");
    Assert(active_swap_chain->ColorFormat() == epidemic::rhi::RhiPixelFormat::B8G8R8A8_UNorm,
           "Swap chain must preserve the validated color format");

    const auto active_context_result = device->CreateCommandContext();
    Assert(active_context_result.HasValue(), "Atomicity fixture must create a command context");
    Assert(active_context_result.Value()->BeginFrame().HasValue(), "Atomicity fixture must begin a frame");
    descriptor = MakeValidSwapChainDesc();
    descriptor.height = 0;
    const auto rejected_replacement = device->CreateSwapChain(descriptor);
    Assert(!rejected_replacement.HasValue(), "Invalid replacement swap chain must fail");
    Assert(active_context_result.Value()->Clear(epidemic::rhi::RhiClearDesc{}).HasValue(),
           "Failed replacement creation must preserve the previously active presentation target");
    Assert(active_context_result.Value()->EndFrame().HasValue(), "Atomicity fixture must close its frame");
}

// Sweeps allocation failures across all null-RHI creation operations and verifies pre-state preservation.
void TestCreationAllocationFailureAtomicity()
{
#if defined(_MSC_VER)
    // MSVC's debug STL may allocate while propagating std::bad_alloc from a replacement global
    // operator new, which makes process-wide allocation injection re-enter the debug heap. The
    // deterministic sweep remains enabled on the GCC/Clang configurations used by this test;
    // Windows validates creation recovery through controlled descriptor failures below.
    const auto device = MakeDevice();
    const auto context = device->CreateCommandContext();
    Assert(context.HasValue(), "Null RHI creation must remain usable after controlled factory failures");
    const auto swap_chain = device->CreateSwapChain(MakeValidSwapChainDesc());
    Assert(swap_chain.HasValue(), "Null RHI swap-chain creation must remain usable after controlled failures");
#else
    bool factory_failure_observed = false;
    const epidemic::rhi::RhiDeviceDesc factory_descriptor{true, "AllocationSweepDevice"};
    for (std::ptrdiff_t fault_index = 1; fault_index <= 8; ++fault_index)
    {
        FailAllocationAt(fault_index);
        bool threw = false;
        try
        {
            static_cast<void>(epidemic::rhi::CreateNullRhiDevice(factory_descriptor));
        }
        catch (const std::bad_alloc &)
        {
            threw = true;
        }
        DisableAllocationFailure();
        if (!threw)
        {
            break;
        }
        factory_failure_observed = true;
        Assert(MakeDevice() != nullptr, "Device factory allocation failure must not poison subsequent creation");
    }
    Assert(factory_failure_observed, "Device factory allocation sweep must observe at least one allocation boundary");

    bool context_failure_observed = false;
    for (std::ptrdiff_t fault_index = 1; fault_index <= 8; ++fault_index)
    {
        const auto device = MakeDevice();
        FailAllocationAt(fault_index);
        bool threw = false;
        try
        {
            static_cast<void>(device->CreateCommandContext());
        }
        catch (const std::bad_alloc &)
        {
            threw = true;
        }
        DisableAllocationFailure();
        if (!threw)
        {
            break;
        }
        context_failure_observed = true;
        const auto recovery_context = device->CreateCommandContext();
        Assert(recovery_context.HasValue(), "Command-context allocation failure must not poison device state");
        Assert(!recovery_context.Value()->IsFrameActive(), "Recovered command context must start inactive");
    }
    Assert(context_failure_observed, "Command-context allocation sweep must observe at least one allocation boundary");

    bool swap_chain_failure_observed = false;
    for (std::ptrdiff_t fault_index = 1; fault_index <= 12; ++fault_index)
    {
        const auto device = MakeDevice();
        std::shared_ptr<epidemic::rhi::IRhiSwapChain> original_swap_chain;
        {
            const auto original_result = device->CreateSwapChain(MakeValidSwapChainDesc());
            Assert(original_result.HasValue(), "Allocation-sweep fixture must create the original swap chain");
            original_swap_chain = original_result.Value();
        }
        const auto context_result = device->CreateCommandContext();
        Assert(context_result.HasValue(), "Allocation-sweep fixture must create a command context");
        const auto context = context_result.Value();
        Assert(context->BeginFrame().HasValue(), "Allocation-sweep fixture must begin a frame");

        auto replacement_descriptor = MakeValidSwapChainDesc();
        replacement_descriptor.width = 1600;
        replacement_descriptor.debug_name = "AllocationSweepReplacement";
        FailAllocationAt(fault_index);
        bool threw = false;
        try
        {
            static_cast<void>(device->CreateSwapChain(replacement_descriptor));
        }
        catch (const std::bad_alloc &)
        {
            threw = true;
        }
        DisableAllocationFailure();
        if (!threw)
        {
            Assert(context->EndFrame().HasValue(), "Successful sweep terminal iteration must close its frame");
            break;
        }

        swap_chain_failure_observed = true;
        Assert(original_swap_chain->Width() == 1280 && original_swap_chain->Height() == 720,
               "Failed replacement allocation must not mutate the original swap-chain descriptor");
        Assert(context->Clear(epidemic::rhi::RhiClearDesc{}).HasValue(),
               "Failed replacement allocation must preserve the original active presentation target");
        Assert(context->EndFrame().HasValue(), "Allocation-failure fixture must remain closable after failure");
    }
    Assert(swap_chain_failure_observed, "Swap-chain allocation sweep must observe at least one allocation boundary");
#endif
}

// Verifies the full Begin/Clear/End state machine and failure-state preservation.
void TestCommandContextLifecycle()
{
    const auto device = MakeDevice();
    const auto swap_chain_result = device->CreateSwapChain(MakeValidSwapChainDesc());
    Assert(swap_chain_result.HasValue(), "Command lifecycle fixture requires an active swap chain");
    const auto context_result = device->CreateCommandContext();
    Assert(context_result.HasValue(), "Command context creation must succeed");
    const auto context = context_result.Value();

    Assert(!context->IsFrameActive(), "Fresh context must not have an active frame");
    AssertFailureCode(context->EndFrame(), "rhi.no_active_frame", "EndFrame without BeginFrame must fail");
    Assert(!context->IsFrameActive(), "Rejected EndFrame must preserve inactive state");
    AssertFailureCode(context->Clear(epidemic::rhi::RhiClearDesc{}), "rhi.clear_outside_frame",
                      "Clear outside a frame must fail");

    Assert(context->BeginFrame().HasValue(), "BeginFrame must activate a fresh frame");
    Assert(context->IsFrameActive(), "Successful BeginFrame must publish active state");
    AssertFailureCode(context->BeginFrame(), "rhi.frame_already_active", "Nested BeginFrame must fail");
    Assert(context->IsFrameActive(), "Rejected nested BeginFrame must preserve the original active frame");

    epidemic::rhi::RhiClearDesc nothing_to_clear{};
    nothing_to_clear.clear_color = false;
    AssertFailureCode(context->Clear(nothing_to_clear), "rhi.nothing_to_clear",
                      "Clear with no requested operation must fail");
    Assert(context->IsFrameActive(), "Rejected Clear must not end the active frame");

    epidemic::rhi::RhiClearDesc non_finite{};
    non_finite.color.red = std::nanf("");
    AssertFailureCode(context->Clear(non_finite), "rhi.invalid_clear_color", "Non-finite clear color must fail");
    Assert(context->IsFrameActive(), "Invalid clear payload must preserve active-frame state");

    epidemic::rhi::RhiClearDesc clear_desc;
    clear_desc.color = epidemic::rhi::RhiColor{0.1f, 0.2f, 0.3f, 1.0f};
    Assert(context->Clear(clear_desc).HasValue(), "Clear must succeed inside an active frame with an active swap chain");
    Assert(context->EndFrame().HasValue(), "EndFrame must close an active frame");
    Assert(!context->IsFrameActive(), "Successful EndFrame must clear active state");
    AssertFailureCode(context->EndFrame(), "rhi.no_active_frame", "Repeated EndFrame must fail");
}

// Verifies present/resize behavior including minimized zero-area input and failed-resize atomicity.
void TestSwapChainPresentResizeLifecycle()
{
    const auto device = MakeDevice();
    const auto result = device->CreateSwapChain(MakeValidSwapChainDesc());
    Assert(result.HasValue(), "Swap-chain lifecycle fixture creation must succeed");
    const auto swap_chain = result.Value();

    Assert(swap_chain->Present().HasValue(), "Present must succeed before the first resize");
    Assert(swap_chain->Resize(1920, 1080).HasValue(), "Valid swap-chain resize must succeed");
    Assert(swap_chain->Width() == 1920 && swap_chain->Height() == 1080,
           "Successful resize must atomically publish both dimensions");
    Assert(swap_chain->Present().HasValue(), "Present must succeed after a successful resize");

    Assert(swap_chain->Resize(1920, 1080).HasValue(), "Resize to current dimensions is an allowed semantic no-op");
    Assert(swap_chain->Width() == 1920 && swap_chain->Height() == 1080,
           "No-op resize must preserve dimensions");

    AssertFailureCode(swap_chain->Resize(0, 1080), "rhi.invalid_swap_chain_size",
                      "Zero-width resize from a minimized/zero-area surface must be rejected");
    AssertFailureCode(swap_chain->Resize(1920, 0), "rhi.invalid_swap_chain_size",
                      "Zero-height resize from a minimized/zero-area surface must be rejected");
    AssertFailureCode(swap_chain->Resize(0, 0), "rhi.invalid_swap_chain_size",
                      "Zero-area resize must be rejected");
    Assert(swap_chain->Width() == 1920 && swap_chain->Height() == 1080,
           "Rejected zero-area resize must preserve the last usable dimensions");
    Assert(swap_chain->Present().HasValue(), "Rejected minimized resize must leave the previous presentation state usable");
}

// Verifies that Null RHI enforces presentation-target lifetime instead of hiding missing/stale targets.
void TestNullPresentationTargetLifetime()
{
    auto device = MakeDevice();
    const auto context_result = device->CreateCommandContext();
    Assert(context_result.HasValue(), "Presentation-lifetime fixture must create a command context");
    auto context = context_result.Value();

    Assert(context->BeginFrame().HasValue(), "BeginFrame does not require a presentation target by itself");
    AssertFailureCode(context->Clear(epidemic::rhi::RhiClearDesc{}), "rhi.no_swap_chain",
                      "Null RHI must reject Clear when no active swap chain exists");

    std::shared_ptr<epidemic::rhi::IRhiSwapChain> swap_chain;
    {
        const auto swap_chain_result = device->CreateSwapChain(MakeValidSwapChainDesc());
        Assert(swap_chain_result.HasValue(), "Creating an active swap chain must succeed");
        swap_chain = swap_chain_result.Value();
    }
    Assert(context->Clear(epidemic::rhi::RhiClearDesc{}).HasValue(),
           "The same active frame must become renderable after a swap chain is created");

    swap_chain.reset();
    AssertFailureCode(context->Clear(epidemic::rhi::RhiClearDesc{}), "rhi.no_swap_chain",
                      "Destroying the active swap chain must invalidate presentation instead of retaining a stale target");
    Assert(context->IsFrameActive(), "Presentation loss must not silently complete the caller-owned command frame");
    Assert(context->EndFrame().HasValue(), "Caller must still be able to close a frame after presentation loss");

    auto second_swap_chain_result = device->CreateSwapChain(MakeValidSwapChainDesc());
    Assert(second_swap_chain_result.HasValue(), "A fresh swap chain must be creatable after the previous target is destroyed");
    Assert(context->BeginFrame().HasValue(), "Context must remain reusable after presentation-target replacement");
    Assert(context->Clear(epidemic::rhi::RhiClearDesc{}).HasValue(), "Fresh target must replace stale presentation state");
    Assert(context->EndFrame().HasValue(), "Frame must close after target replacement");

    Assert(context->BeginFrame().HasValue(), "Fixture must create an active frame before context destruction");
    context.reset();
    const auto replacement_context = device->CreateCommandContext();
    Assert(replacement_context.HasValue(), "Context destruction must not poison subsequent context creation");
    Assert(!replacement_context.Value()->IsFrameActive(), "Destroying an active context must not leak frame-active state");

    const auto survivor_context = replacement_context.Value();
    const auto survivor_swap_chain = second_swap_chain_result.Value();
    device.reset();
    Assert(survivor_context->BeginFrame().HasValue(), "Child RHI objects retain the state they require after device wrapper destruction");
    Assert(survivor_context->Clear(epidemic::rhi::RhiClearDesc{}).HasValue(),
           "Device wrapper destruction must not leave child objects with dangling presentation state");
    Assert(survivor_context->EndFrame().HasValue(), "Surviving context must close its frame cleanly");
    Assert(survivor_swap_chain->Present().HasValue(), "Surviving swap chain must remain usable after device wrapper destruction");
}
}

// Runs the RHI unit-test group.
int main()
{
    return epidemic::tests::RunNamedTests({
        {"ValueAndQueryContracts", &TestValueAndQueryContracts},
        {"DescriptorValidationAndCreationAtomicity", &TestDescriptorValidationAndCreationAtomicity},
        {"CreationAllocationFailureAtomicity", &TestCreationAllocationFailureAtomicity},
        {"CommandContextLifecycle", &TestCommandContextLifecycle},
        {"SwapChainPresentResizeLifecycle", &TestSwapChainPresentResizeLifecycle},
        {"NullPresentationTargetLifetime", &TestNullPresentationTargetLifetime},
    });
}
