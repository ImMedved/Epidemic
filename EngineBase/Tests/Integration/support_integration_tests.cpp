// This file exercises EngineBase Support composition contracts independently from Runtime/Framework.

#include "../test_assert.h"

#include <Epidemic/Core/configuration.h>
#include <Epidemic/Core/event_bus.h>
#include <Epidemic/Core/main_thread_dispatcher.h>
#include <Epidemic/Core/task_scheduler.h>
#include <Epidemic/Diagnostics/logger.h>
#include <Epidemic/EngineBase/engine_base_support.h>
#include <Epidemic/Input/input_system.h>
#include <Epidemic/Memory/imemory_tracker.h>
#include <Epidemic/Memory/memory_tracker.h>
#include <Epidemic/Platform/windows_platform_runtime.h>
#include <Epidemic/RHI/null_rhi_device.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using epidemic::tests::Assert;

class StubWindow final : public epidemic::platform::IWindow
{
  public:
    explicit StubWindow(void *native_handle = reinterpret_cast<void *>(static_cast<std::uintptr_t>(1)))
        : native_handle_(native_handle)
    {
    }

    [[nodiscard]] epidemic::platform::WindowId Id() const noexcept override
    {
        return epidemic::platform::WindowId(1);
    }

    [[nodiscard]] std::string_view Title() const noexcept override
    {
        return "SupportStubWindow";
    }

    [[nodiscard]] epidemic::platform::NativeWindowHandle GetNativeHandle() const noexcept override
    {
        return epidemic::platform::NativeWindowHandle(native_handle_);
    }

    [[nodiscard]] std::uint32_t ClientWidth() const noexcept override { return 320; }
    [[nodiscard]] std::uint32_t ClientHeight() const noexcept override { return 240; }
    [[nodiscard]] std::uint32_t Dpi() const noexcept override { return 96; }
    [[nodiscard]] bool HasFocus() const noexcept override { return true; }
    [[nodiscard]] bool IsMinimized() const noexcept override { return false; }
    [[nodiscard]] bool IsCloseRequested() const noexcept override { return false; }
    void Show() override {}
    void Close() override {}

  private:
    void *native_handle_{};
};

class StubWindowSystem final : public epidemic::platform::IWindowSystem
{
  public:
    [[nodiscard]] epidemic::foundation::Result<epidemic::platform::WindowPtr>
    CreateWindow(const epidemic::platform::WindowCreateInfo &) override
    {
        return epidemic::foundation::Result<epidemic::platform::WindowPtr>::Success(std::make_shared<StubWindow>());
    }

    [[nodiscard]] std::vector<epidemic::platform::PlatformEvent> DrainEvents() override { return {}; }
    [[nodiscard]] bool HasPendingEvents() const noexcept override { return false; }
    [[nodiscard]] std::size_t WindowCount() const noexcept override { return 0; }
};

class TracePlatform final : public epidemic::platform::IPlatformRuntime, public epidemic::platform::IWindowSystem
{
  public:
    explicit TracePlatform(std::vector<std::string> &trace) : trace_(trace) {}

    [[nodiscard]] std::string_view Name() const override { return "TracePlatform"; }
    [[nodiscard]] const epidemic::platform::ProcessInfo &GetProcessInfo() const override { return process_info_; }
    [[nodiscard]] epidemic::foundation::TimePoint Now() const override { return epidemic::foundation::Clock::now(); }

    [[nodiscard]] epidemic::foundation::Result<epidemic::platform::DynamicLibraryPtr>
    LoadDynamicLibrary(const epidemic::foundation::Path &) override
    {
        return epidemic::foundation::Result<epidemic::platform::DynamicLibraryPtr>::Failure(
            epidemic::foundation::Error::Create("support.test.unsupported", "Dynamic loading is not used by this test"));
    }

    void PumpEvents() override { trace_.push_back("platform.pump"); }
    [[nodiscard]] bool IsExitRequested() const override { return false; }

    [[nodiscard]] epidemic::foundation::Result<epidemic::platform::WindowPtr>
    CreateWindow(const epidemic::platform::WindowCreateInfo &) override
    {
        return epidemic::foundation::Result<epidemic::platform::WindowPtr>::Success(std::make_shared<StubWindow>());
    }

    [[nodiscard]] std::vector<epidemic::platform::PlatformEvent> DrainEvents() override
    {
        trace_.push_back("platform.drain");
        return {};
    }

    [[nodiscard]] bool HasPendingEvents() const noexcept override { return false; }
    [[nodiscard]] std::size_t WindowCount() const noexcept override { return 0; }

  private:
    std::vector<std::string> &trace_;
    epidemic::platform::ProcessInfo process_info_{};
};

class TraceInput final : public epidemic::input::IInputSystem
{
  public:
    explicit TraceInput(std::vector<std::string> &trace) : trace_(trace) {}

    void QueuePlatformEvent(const epidemic::platform::PlatformEvent &) override { trace_.push_back("input.queue_one"); }

    void QueuePlatformEvents(std::span<const epidemic::platform::PlatformEvent>) override
    {
        trace_.push_back("input.queue");
    }

    void PublishSnapshot() override { trace_.push_back("input.publish"); }
    void Reset() noexcept override {}
    [[nodiscard]] const epidemic::input::InputSnapshot &CurrentSnapshot() const noexcept override { return snapshot_; }
    [[nodiscard]] std::span<const epidemic::input::InputEvent> CurrentEvents() const noexcept override { return events_; }

  private:
    std::vector<std::string> &trace_;
    epidemic::input::InputSnapshot snapshot_{};
    std::vector<epidemic::input::InputEvent> events_{};
};

class TraceCommandContext final : public epidemic::rhi::IRhiCommandContext
{
  public:
    explicit TraceCommandContext(std::vector<std::string> &trace) : trace_(trace) {}

    [[nodiscard]] epidemic::foundation::Result<void> BeginFrame() override
    {
        trace_.push_back("rhi.begin");
        active_ = true;
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] epidemic::foundation::Result<void> Clear(const epidemic::rhi::RhiClearDesc &) override
    {
        trace_.push_back("rhi.clear");
        if (fail_clear)
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("support.test.clear_failure", "Injected clear failure"));
        }
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] epidemic::foundation::Result<void> EndFrame() override
    {
        trace_.push_back("rhi.end");
        active_ = false;
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] bool IsFrameActive() const noexcept override { return active_; }

    bool fail_clear{false};

  private:
    std::vector<std::string> &trace_;
    bool active_{false};
};

class TraceSwapChain final : public epidemic::rhi::IRhiSwapChain
{
  public:
    explicit TraceSwapChain(std::vector<std::string> &trace) : trace_(trace) {}

    [[nodiscard]] epidemic::foundation::Result<void> Present() override
    {
        trace_.push_back("rhi.present");
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] epidemic::foundation::Result<void> Resize(std::uint32_t, std::uint32_t) override
    {
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] std::uint32_t Width() const noexcept override { return 320; }
    [[nodiscard]] std::uint32_t Height() const noexcept override { return 240; }
    [[nodiscard]] std::uint32_t BufferCount() const noexcept override { return 2; }
    [[nodiscard]] epidemic::rhi::RhiPixelFormat ColorFormat() const noexcept override
    {
        return epidemic::rhi::RhiPixelFormat::B8G8R8A8_UNorm;
    }

  private:
    std::vector<std::string> &trace_;
};

class CountingDevice final : public epidemic::rhi::IRhiDevice
{
  public:
    [[nodiscard]] std::string_view BackendName() const noexcept override { return "CountingDevice"; }
    [[nodiscard]] const epidemic::rhi::RhiDeviceDesc &Descriptor() const noexcept override { return descriptor_; }

    [[nodiscard]] epidemic::foundation::Result<std::shared_ptr<epidemic::rhi::IRhiCommandContext>>
    CreateCommandContext() override
    {
        return epidemic::foundation::Result<std::shared_ptr<epidemic::rhi::IRhiCommandContext>>::Failure(
            epidemic::foundation::Error::Create("support.test.unused", "Command context creation is unused"));
    }

    [[nodiscard]] epidemic::foundation::Result<std::shared_ptr<epidemic::rhi::IRhiSwapChain>>
    CreateSwapChain(const epidemic::rhi::RhiSwapChainDesc &) override
    {
        ++swap_chain_create_count;
        if (fail_swap_chain_creation)
        {
            return epidemic::foundation::Result<std::shared_ptr<epidemic::rhi::IRhiSwapChain>>::Failure(
                epidemic::foundation::Error::Create("support.test.swap_chain_failure", "Injected swap-chain failure"));
        }

        static std::vector<std::string> unused_trace;
        return epidemic::foundation::Result<std::shared_ptr<epidemic::rhi::IRhiSwapChain>>::Success(
            std::make_shared<TraceSwapChain>(unused_trace));
    }

    std::size_t swap_chain_create_count{};
    bool fail_swap_chain_creation{false};

  private:
    epidemic::rhi::RhiDeviceDesc descriptor_{};
};

void TestEngineBaseBundleRejectsLateConflictWithoutPartialRegistration()
{
    epidemic::core::Application application({.application_name = "SupportAtomicBaseline", .frame_limit = std::nullopt});
    auto existing_memory = std::make_shared<epidemic::memory::MemoryTracker>(true);
    application.Services().RegisterInstance<epidemic::memory::IMemoryTracker>(existing_memory);

    bool rejected = false;
    try
    {
        static_cast<void>(epidemic::enginebase::RegisterEngineBase(application, {}));
    }
    catch (const std::runtime_error &)
    {
        rejected = true;
    }

    Assert(rejected, "RegisterEngineBase must reject a conflict anywhere in its service bundle");
    Assert(!application.Services().Contains<epidemic::diagnostics::ILogger>(),
           "Failed EngineBase bundle registration must not publish ILogger");
    Assert(!application.Services().Contains<epidemic::core::config::IConfiguration>(),
           "Failed EngineBase bundle registration must not publish IConfiguration");
    Assert(!application.Services().Contains<epidemic::core::events::IEventBus>(),
           "Failed EngineBase bundle registration must not publish IEventBus");
    Assert(!application.Services().Contains<epidemic::core::tasks::ITaskScheduler>(),
           "Failed EngineBase bundle registration must not publish ITaskScheduler");
    Assert(!application.Services().Contains<epidemic::core::IMainThreadDispatcher>(),
           "Failed EngineBase bundle registration must not publish IMainThreadDispatcher");
    Assert(application.Services().Get<epidemic::memory::IMemoryTracker>() == existing_memory,
           "Failed later bundle registration must preserve the previously valid service");
}

void TestEngineBaseOptionsNormalizeAndRejectBeforePublication()
{
    epidemic::core::Application normalized({.application_name = "SupportWorkerNormalization", .frame_limit = std::nullopt});
    static_cast<void>(epidemic::enginebase::RegisterEngineBase(
        normalized, {.runtime_name = "SupportWorkerNormalization", .worker_count = 0}));
    const auto configuration = normalized.Services().Get<epidemic::core::config::IConfiguration>();
    const auto scheduler = normalized.Services().Get<epidemic::core::tasks::ITaskScheduler>();
    Assert(configuration->GetWorkerCount().value_or(0) == 1,
           "worker_count=0 must be normalized consistently in configuration");
    Assert(scheduler->WorkerCount() == 1, "worker_count=0 must create exactly one scheduler worker");

    epidemic::core::Application invalid({.application_name = "SupportInvalidOptions", .frame_limit = std::nullopt});
    bool rejected = false;
    try
    {
        static_cast<void>(epidemic::enginebase::RegisterEngineBase(
            invalid, {.default_window_width = 0, .default_window_height = 720}));
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }
    Assert(rejected, "Invalid default window dimensions must be rejected");
    Assert(!invalid.Services().Contains<epidemic::diagnostics::ILogger>(),
           "Invalid EngineBase options must be rejected before service publication");
}

void TestWindowsAndInputDuplicateRegistrationAreControlled()
{
    epidemic::core::Application windows_conflict({.application_name = "SupportWindowsConflict", .frame_limit = std::nullopt});
    auto existing_window_system = std::make_shared<StubWindowSystem>();
    windows_conflict.Services().RegisterInstance<epidemic::platform::IWindowSystem>(existing_window_system);

    bool windows_rejected = false;
    try
    {
        static_cast<void>(epidemic::enginebase::RegisterWindowsRuntime(windows_conflict));
    }
    catch (const std::runtime_error &)
    {
        windows_rejected = true;
    }
    Assert(windows_rejected, "Windows bundle must reject a pre-existing service role");
    Assert(!windows_conflict.Services().Contains<epidemic::platform::IPlatformRuntime>(),
           "Rejected Windows bundle must not publish its other service role");
    Assert(windows_conflict.Services().Get<epidemic::platform::IWindowSystem>() == existing_window_system,
           "Rejected Windows bundle must preserve the existing window-system owner");

    epidemic::core::Application input_conflict({.application_name = "SupportInputConflict", .frame_limit = std::nullopt});
    auto existing_input = std::make_shared<epidemic::input::InputSystem>();
    input_conflict.Services().RegisterInstance<epidemic::input::IInputSystem>(existing_input);
    bool input_rejected = false;
    try
    {
        static_cast<void>(epidemic::enginebase::RegisterInputRuntime(input_conflict));
    }
    catch (const std::runtime_error &)
    {
        input_rejected = true;
    }
    Assert(input_rejected, "Input duplicate registration must be rejected");
    Assert(input_conflict.Services().Get<epidemic::input::IInputSystem>() == existing_input,
           "Input duplicate rejection must preserve the existing service");
}

void TestGraphicsRegistrationConflictsAndInvalidBackendAreResultFailures()
{
    epidemic::core::Application conflict({.application_name = "SupportGraphicsConflict", .frame_limit = std::nullopt});
    const auto null_device_result = epidemic::rhi::CreateNullRhiDevice({.debug_name = "SupportPreexistingNull"});
    Assert(null_device_result.HasValue(), "Null device setup for support regression must succeed");
    const auto context_result = null_device_result.Value()->CreateCommandContext();
    Assert(context_result.HasValue(), "Null command-context setup for support regression must succeed");
    auto existing_context = context_result.Value();
    conflict.Services().RegisterInstance<epidemic::rhi::IRhiCommandContext>(existing_context);

    const auto conflict_result = epidemic::enginebase::RegisterGraphicsRuntime(
        conflict, {.backend = epidemic::enginebase::GraphicsBackend::Null, .debug_name = "ConflictDevice"});
    Assert(!conflict_result.HasValue(), "Graphics registration conflict must return a controlled Result failure");
    Assert(conflict_result.GetError().HasCode("engine_base.support.service_registration_conflict"),
           "Graphics registration conflict must expose a stable machine-readable code");
    Assert(!conflict.Services().Contains<epidemic::rhi::IRhiDevice>(),
           "Graphics bundle conflict must not publish IRhiDevice partially");
    Assert(conflict.Services().Get<epidemic::rhi::IRhiCommandContext>() == existing_context,
           "Graphics bundle conflict must preserve the pre-existing command context");

    epidemic::core::Application invalid_backend({.application_name = "SupportInvalidBackend", .frame_limit = std::nullopt});
    const auto invalid_result = epidemic::enginebase::RegisterGraphicsRuntime(
        invalid_backend,
        {.backend = static_cast<epidemic::enginebase::GraphicsBackend>(255), .debug_name = "InvalidBackend"});
    Assert(!invalid_result.HasValue(), "Unknown GraphicsBackend values must be rejected");
    Assert(invalid_result.GetError().HasCode("engine_base.support.invalid_graphics_backend"),
           "Unknown GraphicsBackend must expose a stable machine-readable error");
    Assert(!invalid_backend.Services().Contains<epidemic::rhi::IRhiDevice>() &&
               !invalid_backend.Services().Contains<epidemic::rhi::IRhiCommandContext>(),
           "Unknown GraphicsBackend must not mutate the composition root");
}

void TestValidWindowsInputAndNullGraphicsComposition()
{
    epidemic::core::Application application({.application_name = "SupportValidComposition", .frame_limit = std::nullopt});
    static_cast<void>(epidemic::enginebase::RegisterEngineBase(
        application, {.runtime_name = "SupportValidComposition", .worker_count = 1}));
    const auto platform = epidemic::enginebase::RegisterWindowsRuntime(application);
    const auto input = epidemic::enginebase::RegisterInputRuntime(application);
    const auto graphics = epidemic::enginebase::RegisterGraphicsRuntime(
        application, {.backend = epidemic::enginebase::GraphicsBackend::Null, .debug_name = "SupportNullGraphics"});

    Assert(platform != nullptr && input != nullptr && graphics.HasValue(),
           "EngineBase + Windows + Input + Null graphics must compose successfully");
    const auto windows_runtime = std::dynamic_pointer_cast<epidemic::platform::WindowsPlatformRuntime>(platform);
    const auto windows_system = std::dynamic_pointer_cast<epidemic::platform::WindowsPlatformRuntime>(
        application.Services().Get<epidemic::platform::IWindowSystem>());
    Assert(windows_runtime != nullptr && windows_runtime == windows_system,
           "Windows runtime and window-system roles must share one concrete owner");
    Assert(application.Services().Get<epidemic::rhi::IRhiDevice>() == graphics.Value().device,
           "Null graphics registration must publish the returned device role");
    Assert(application.Services().Get<epidemic::rhi::IRhiCommandContext>() == graphics.Value().command_context,
           "Null graphics registration must publish the returned command-context role");
}

void TestMainSwapChainDuplicateOwnerIsRejectedBeforeBackendWork()
{
    epidemic::core::Application application({.application_name = "SupportSwapChainOwnership", .frame_limit = std::nullopt});
    auto device = std::make_shared<CountingDevice>();
    application.Services().RegisterInstance<epidemic::rhi::IRhiDevice>(device);

    std::vector<std::string> trace;
    auto existing_swap_chain = std::make_shared<TraceSwapChain>(trace);
    application.Services().RegisterInstance<epidemic::rhi::IRhiSwapChain>(existing_swap_chain);

    auto window = std::make_shared<StubWindow>();
    epidemic::rhi::RhiSwapChainDesc desc{};
    desc.width = 320;
    desc.height = 240;
    const auto duplicate = epidemic::enginebase::RegisterMainSwapChain(application, window, desc);
    Assert(!duplicate.HasValue(), "A second main swap-chain owner must be rejected");
    Assert(duplicate.GetError().HasCode("engine_base.support.service_registration_conflict"),
           "Duplicate main swap-chain owner must expose a stable conflict code");
    Assert(device->swap_chain_create_count == 0,
           "Duplicate main swap-chain ownership must be rejected before backend creation");
    Assert(application.Services().Get<epidemic::rhi::IRhiSwapChain>() == existing_swap_chain,
           "Duplicate main swap-chain rejection must preserve the existing owner");

    epidemic::core::Application null_window({.application_name = "SupportNullWindow", .frame_limit = std::nullopt});
    null_window.Services().RegisterInstance<epidemic::rhi::IRhiDevice>(device);
    const auto missing_window = epidemic::enginebase::RegisterMainSwapChain(null_window, {}, desc);
    Assert(!missing_window.HasValue() && missing_window.GetError().HasCode("engine_base.support.window_required"),
           "Null main-window wiring must return a controlled failure");

    epidemic::core::Application missing_device({.application_name = "SupportMissingDevice", .frame_limit = std::nullopt});
    const auto no_device = epidemic::enginebase::RegisterMainSwapChain(missing_device, window, desc);
    Assert(!no_device.HasValue() && no_device.GetError().HasCode("engine_base.support.rhi_device_required"),
           "Missing RHI device must return a controlled Result failure");

    epidemic::core::Application missing_window_system(
        {.application_name = "SupportMissingWindowSystem", .frame_limit = std::nullopt});
    const auto no_window_system = epidemic::enginebase::CreateMainWindow(
        missing_window_system, epidemic::platform::WindowCreateInfo{"Missing", 320, 240, false});
    Assert(!no_window_system.HasValue() &&
               no_window_system.GetError().HasCode("engine_base.support.window_system_required"),
           "Missing window system must return a controlled Result failure");

    epidemic::core::Application backend_failure(
        {.application_name = "SupportSwapBackendFailure", .frame_limit = std::nullopt});
    auto failing_device = std::make_shared<CountingDevice>();
    failing_device->fail_swap_chain_creation = true;
    backend_failure.Services().RegisterInstance<epidemic::rhi::IRhiDevice>(failing_device);
    const auto failed_backend = epidemic::enginebase::RegisterMainSwapChain(backend_failure, window, desc);
    Assert(!failed_backend.HasValue() && failed_backend.GetError().HasCode("support.test.swap_chain_failure"),
           "Swap-chain backend failure must propagate as the original controlled Result failure");
    Assert(!backend_failure.Services().Contains<epidemic::rhi::IRhiSwapChain>(),
           "Swap-chain backend failure must not publish a partial main swap-chain owner");
}

void TestFrameLoopDependencyFailureDoesNotPublishEventBuffer()
{
    epidemic::core::Application platform_missing({.application_name = "SupportPlatformLoopMissing", .frame_limit = std::nullopt});
    static_cast<void>(epidemic::enginebase::RegisterEngineBase(platform_missing, {}));
    bool platform_rejected = false;
    try
    {
        epidemic::enginebase::RegisterPlatformFrameLoop(platform_missing);
    }
    catch (const std::runtime_error &)
    {
        platform_rejected = true;
    }
    Assert(platform_rejected, "Platform frame-loop registration must reject missing dependencies");
    Assert(!platform_missing.Services().Contains<epidemic::enginebase::FramePlatformEvents>(),
           "Missing platform dependencies must not leave a partial FramePlatformEvents service");

    epidemic::core::Application input_missing({.application_name = "SupportInputLoopMissing", .frame_limit = std::nullopt});
    static_cast<void>(epidemic::enginebase::RegisterEngineBase(input_missing, {}));
    bool input_rejected = false;
    try
    {
        epidemic::enginebase::RegisterInputFrameLoop(input_missing);
    }
    catch (const std::runtime_error &)
    {
        input_rejected = true;
    }
    Assert(input_rejected, "Input frame-loop registration must reject missing dependencies");
    Assert(!input_missing.Services().Contains<epidemic::enginebase::FramePlatformEvents>(),
           "Missing input dependency must not leave a partial FramePlatformEvents service");
}

void TestFrameHandlerOrderIsPlatformInputThenPresentation()
{
    epidemic::core::Application application({.application_name = "SupportFrameOrder", .frame_limit = std::nullopt});
    static_cast<void>(epidemic::enginebase::RegisterEngineBase(
        application, {.runtime_name = "SupportFrameOrder", .worker_count = 1}));

    std::vector<std::string> trace;
    auto platform = std::make_shared<TracePlatform>(trace);
    auto input = std::make_shared<TraceInput>(trace);
    application.Services().RegisterInstancesAtomic<epidemic::platform::IPlatformRuntime, epidemic::platform::IWindowSystem>(
        platform, platform);
    application.Services().RegisterInstance<epidemic::input::IInputSystem>(input);

    epidemic::enginebase::RegisterPlatformFrameLoop(application);
    epidemic::enginebase::RegisterInputFrameLoop(application);

    auto command_context = std::make_shared<TraceCommandContext>(trace);
    auto swap_chain = std::make_shared<TraceSwapChain>(trace);
    auto render_paused = std::make_shared<bool>(false);
    auto clear_desc = std::make_shared<epidemic::rhi::RhiClearDesc>();
    epidemic::enginebase::RegisterRhiFrameLoop(application, command_context, swap_chain, render_paused, clear_desc);

    application.SetFrameLimit(1);
    Assert(application.Bootstrap() == 0, "Support frame-order bootstrap must succeed");
    Assert(application.Initialize() == 0, "Support frame-order initialize must succeed");
    Assert(application.Run() == 0, "Support frame-order run must succeed");
    Assert(application.Shutdown() == 0, "Support frame-order shutdown must succeed");

    const std::vector<std::string> expected{
        "platform.pump", "platform.drain", "input.queue", "input.publish", "rhi.begin", "rhi.clear", "rhi.end", "rhi.present"};
    Assert(trace == expected,
           "Support frame handlers must execute in platform -> input -> begin/clear -> end -> present phase order");
}

void TestRhiFrameLoopRejectsInvalidWiringBeforeRegistration()
{
    epidemic::core::Application application({.application_name = "SupportInvalidRhiLoop", .frame_limit = std::nullopt});
    static_cast<void>(epidemic::enginebase::RegisterEngineBase(application, {}));

    std::vector<std::string> trace;
    auto context = std::make_shared<TraceCommandContext>(trace);
    auto swap_chain = std::make_shared<TraceSwapChain>(trace);
    auto paused = std::make_shared<bool>(false);
    auto clear = std::make_shared<epidemic::rhi::RhiClearDesc>();

    bool rejected = false;
    try
    {
        epidemic::enginebase::RegisterRhiFrameLoop(application, {}, swap_chain, paused, clear);
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }
    Assert(rejected, "RHI frame-loop registration must reject null dependencies before adding handlers");

    epidemic::enginebase::RegisterRhiFrameLoop(application, context, swap_chain, paused, clear);
    application.SetFrameLimit(1);
    Assert(application.Bootstrap() == 0 && application.Initialize() == 0 && application.Run() == 0 &&
               application.Shutdown() == 0,
           "Valid RHI frame-loop wiring after a rejected call must still run");
    Assert(trace == std::vector<std::string>({"rhi.begin", "rhi.clear", "rhi.end", "rhi.present"}),
           "Rejected RHI frame-loop wiring must not leave duplicate partial handlers");
}

void TestRhiClearFailureClosesActiveFrame()
{
    epidemic::core::Application application({.application_name = "SupportRhiClearFailure", .frame_limit = std::nullopt});
    static_cast<void>(epidemic::enginebase::RegisterEngineBase(application, {}));

    std::vector<std::string> trace;
    auto context = std::make_shared<TraceCommandContext>(trace);
    context->fail_clear = true;
    auto swap_chain = std::make_shared<TraceSwapChain>(trace);
    auto paused = std::make_shared<bool>(false);
    auto clear = std::make_shared<epidemic::rhi::RhiClearDesc>();
    epidemic::enginebase::RegisterRhiFrameLoop(application, context, swap_chain, paused, clear);

    Assert(application.Bootstrap() == 0 && application.Initialize() == 0,
           "Clear-failure application must reach initialized state");
    bool clear_failed = false;
    try
    {
        static_cast<void>(application.Tick());
    }
    catch (const std::runtime_error &exception)
    {
        clear_failed = std::string(exception.what()) == "Injected clear failure";
    }
    Assert(clear_failed, "Injected Clear failure must remain the reported frame error");
    Assert(!context->IsFrameActive(),
           "Clear failure after BeginFrame must close the active command frame before unwinding");
    Assert(trace == std::vector<std::string>({"rhi.begin", "rhi.clear", "rhi.end"}),
           "Clear failure cleanup must end the frame without presenting it");
}

void TestFramePlatformEventsAndThrottleContracts()
{
    epidemic::core::Application application(
        {.application_name = "SupportFrameHelpers", .frame_limit = std::nullopt});
    static_cast<void>(epidemic::enginebase::RegisterEngineBase(application, {}));

    const auto first = epidemic::enginebase::EnsureFramePlatformEvents(application);
    const auto second = epidemic::enginebase::EnsureFramePlatformEvents(application);
    Assert(first == second, "EnsureFramePlatformEvents must be idempotent and preserve one shared frame buffer");
    Assert(application.Services().Get<epidemic::enginebase::FramePlatformEvents>() == first,
           "EnsureFramePlatformEvents must publish exactly the returned buffer instance");

    epidemic::enginebase::RegisterFrameThrottle(application, std::chrono::milliseconds(0));
    application.SetFrameLimit(1);
    Assert(application.Bootstrap() == 0 && application.Initialize() == 0 && application.Run() == 0 &&
               application.Shutdown() == 0,
           "Zero-duration frame throttle must integrate into the EndFrame phase without changing lifecycle success");
}

void TestRegistrationAfterSealIsRejectedWithoutNewServices()
{
    epidemic::core::Application application(
        {.application_name = "SupportSealedComposition", .frame_limit = std::nullopt});
    static_cast<void>(epidemic::enginebase::RegisterEngineBase(application, {}));
    Assert(application.Bootstrap() == 0 && application.Initialize() == 0,
           "Application must reach the sealed state for the support lifecycle test");

    bool input_rejected = false;
    try
    {
        static_cast<void>(epidemic::enginebase::RegisterInputRuntime(application));
    }
    catch (const std::runtime_error &)
    {
        input_rejected = true;
    }
    Assert(input_rejected && !application.Services().Contains<epidemic::input::IInputSystem>(),
           "Input registration after seal must fail without publishing a service");

    const auto graphics = epidemic::enginebase::RegisterGraphicsRuntime(
        application, {.backend = epidemic::enginebase::GraphicsBackend::Null, .debug_name = "SealedGraphics"});
    Assert(!graphics.HasValue() && graphics.GetError().HasCode("engine_base.support.service_container_sealed"),
           "Graphics registration after seal must return the controlled sealed-container failure");
    Assert(!application.Services().Contains<epidemic::rhi::IRhiDevice>() &&
               !application.Services().Contains<epidemic::rhi::IRhiCommandContext>(),
           "Sealed graphics registration must not mutate the composition root");
    Assert(application.Shutdown() == 0, "Sealed support lifecycle test must shut down cleanly");
}

void TestCompositionServicesAreApplicationScoped()
{
    epidemic::core::Application first({.application_name = "SupportScopeA", .frame_limit = std::nullopt});
    epidemic::core::Application second({.application_name = "SupportScopeB", .frame_limit = std::nullopt});
    static_cast<void>(epidemic::enginebase::RegisterEngineBase(first, {.runtime_name = "ScopeA"}));
    static_cast<void>(epidemic::enginebase::RegisterEngineBase(second, {.runtime_name = "ScopeB"}));

    Assert(first.Services().Get<epidemic::diagnostics::ILogger>() != second.Services().Get<epidemic::diagnostics::ILogger>(),
           "Support must not keep ILogger in a hidden global service locator");
    Assert(first.Services().Get<epidemic::core::events::IEventBus>() != second.Services().Get<epidemic::core::events::IEventBus>(),
           "Support must keep EventBus ownership scoped to each Application");
    Assert(first.Services().Get<epidemic::core::config::IConfiguration>()->GetRuntimeName().value_or("") == "ScopeA" &&
               second.Services().Get<epidemic::core::config::IConfiguration>()->GetRuntimeName().value_or("") == "ScopeB",
           "Independent composition roots must retain independent configuration state");
}

} // namespace

int main()
{
    return epidemic::tests::RunNamedTests({
        {"EngineBaseBundleRejectsLateConflictWithoutPartialRegistration", &TestEngineBaseBundleRejectsLateConflictWithoutPartialRegistration},
        {"EngineBaseOptionsNormalizeAndRejectBeforePublication", &TestEngineBaseOptionsNormalizeAndRejectBeforePublication},
        {"WindowsAndInputDuplicateRegistrationAreControlled", &TestWindowsAndInputDuplicateRegistrationAreControlled},
        {"GraphicsRegistrationConflictsAndInvalidBackendAreResultFailures", &TestGraphicsRegistrationConflictsAndInvalidBackendAreResultFailures},
        {"ValidWindowsInputAndNullGraphicsComposition", &TestValidWindowsInputAndNullGraphicsComposition},
        {"MainSwapChainDuplicateOwnerIsRejectedBeforeBackendWork", &TestMainSwapChainDuplicateOwnerIsRejectedBeforeBackendWork},
        {"FrameLoopDependencyFailureDoesNotPublishEventBuffer", &TestFrameLoopDependencyFailureDoesNotPublishEventBuffer},
        {"FrameHandlerOrderIsPlatformInputThenPresentation", &TestFrameHandlerOrderIsPlatformInputThenPresentation},
        {"RhiFrameLoopRejectsInvalidWiringBeforeRegistration", &TestRhiFrameLoopRejectsInvalidWiringBeforeRegistration},
        {"RhiClearFailureClosesActiveFrame", &TestRhiClearFailureClosesActiveFrame},
        {"FramePlatformEventsAndThrottleContracts", &TestFramePlatformEventsAndThrottleContracts},
        {"RegistrationAfterSealIsRejectedWithoutNewServices", &TestRegistrationAfterSealIsRejectedWithoutNewServices},
        {"CompositionServicesAreApplicationScoped", &TestCompositionServicesAreApplicationScoped},
    });
}
