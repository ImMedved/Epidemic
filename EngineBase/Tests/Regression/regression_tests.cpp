#include "../core_test_support.h"
#include "../test_assert.h"

#include <Epidemic/EngineBase/engine_base_support.h>
#include <Epidemic/Diagnostics/counters.h>
#include <Epidemic/Diagnostics/profiling.h>
#include <Epidemic/RHI/null_rhi_device.h>

#include <stdexcept>

namespace
{
using epidemic::tests::Assert;
using epidemic::tests::ProbeModule;
using epidemic::tests::RegisterApplicationCoreServices;
using epidemic::tests::RegisterCoreServices;

void TestRegressionContracts()
{
    epidemic::core::Application app({"RegressionApp"});
    static_cast<void>(epidemic::enginebase::RegisterEngineBase(
        app, {.runtime_name = "RegressionApp", .log_module = "RegressionApp"}));
    Assert(app.Services().Contains<epidemic::memory::IMemoryTracker>(), "RegisterEngineBase must register IMemoryTracker");
    static_cast<void>(epidemic::enginebase::RegisterWindowsRuntime(app));
    Assert(app.Services().Contains<epidemic::platform::IPlatformRuntime>(), "RegisterWindowsRuntime must register platform runtime");
    static_cast<void>(epidemic::enginebase::RegisterInputRuntime(app));
    Assert(app.Services().Contains<epidemic::input::IInputSystem>(), "RegisterInputRuntime must register input system");
    const auto graphics_runtime = epidemic::enginebase::RegisterGraphicsRuntime(
        app, {.backend = epidemic::enginebase::GraphicsBackend::Null, .debug_name = "RegressionNullRhi"});
    Assert(graphics_runtime.HasValue(), "RegisterGraphicsRuntime must return Result for expected runtime setup");
    Assert(app.Services().Contains<epidemic::rhi::IRhiDevice>(), "RegisterGraphicsRuntime must register IRhiDevice");
    Assert(app.Services().Contains<epidemic::rhi::IRhiCommandContext>(),
           "RegisterGraphicsRuntime must register IRhiCommandContext");

    epidemic::core::ServiceContainer services;
    auto logger = RegisterCoreServices(services);

    bool duplicate_service_failed = false;
    try
    {
        services.RegisterInstance<epidemic::diagnostics::ILogger>(logger);
    }
    catch (const std::exception &)
    {
        duplicate_service_failed = true;
    }
    Assert(duplicate_service_failed, "Duplicate service registration must fail");

    bool missing_service_failed = false;
    try
    {
        epidemic::core::Application missing_services_application;
        missing_services_application.Bootstrap();
    }
    catch (const std::exception &)
    {
        missing_service_failed = true;
    }
    Assert(missing_service_failed, "Missing service must fail");

    epidemic::core::events::EventBus event_bus;
    bool empty_handler_failed = false;
    try
    {
        event_bus.SubscribeQueued<int>(std::function<void(const int &)>{});
    }
    catch (const std::invalid_argument &)
    {
        empty_handler_failed = true;
    }
    Assert(empty_handler_failed, "Empty event handler must fail");
    Assert(!event_bus.Unsubscribe(12345), "Unknown unsubscribe must return false");

    epidemic::core::tasks::SimpleTaskScheduler scheduler(1);
    auto throwing_task = scheduler.Schedule([] { throw std::runtime_error("task failure"); }, "throwing");
    bool task_exception_propagated = false;
    try
    {
        scheduler.Wait(throwing_task);
    }
    catch (const std::runtime_error &exception)
    {
        task_exception_propagated = std::string(exception.what()) == "task failure";
    }
    Assert(task_exception_propagated, "Task exception must propagate");

    epidemic::core::MainThreadDispatcher dispatcher;
    std::vector<int> order;
    dispatcher.Post([&order] { order.push_back(1); }, "one");
    dispatcher.Post([&order] { order.push_back(2); }, "two");
    Assert(dispatcher.Drain() == 2 && order == std::vector<int>({1, 2}), "Dispatcher must execute FIFO");
    bool dispatcher_throw_propagated = false;
    dispatcher.Post([] { throw std::runtime_error("dispatcher failure"); }, "throw");
    try
    {
        static_cast<void>(dispatcher.Drain());
    }
    catch (const std::runtime_error &exception)
    {
        dispatcher_throw_propagated = std::string(exception.what()) == "dispatcher failure";
    }
    Assert(dispatcher_throw_propagated, "Dispatcher exceptions must propagate");

    epidemic::memory::MemoryTracker tracker(true);
    tracker.SetBudget(epidemic::memory::AllocationTag::Core, 10);
    tracker.RecordAllocate(epidemic::memory::AllocationTag::Core, 12);
    Assert(tracker.IsOverBudget(epidemic::memory::AllocationTag::Core), "Memory over-budget detection must work");

    epidemic::diagnostics::SetProfileCollector(nullptr);
    epidemic::diagnostics::SetProfilingEnabled(false);
    {
        EPIDEMIC_PROFILE_SCOPE("RegressionDisabledDiagnostics");
    }
    epidemic::diagnostics::SetProfilingEnabled(true);

    const auto invalid_device = epidemic::rhi::CreateNullRhiDevice(epidemic::rhi::RhiDeviceDesc{false, ""});
    Assert(!invalid_device.HasValue(), "Invalid RHI descriptor validation must fail");

    const auto device = epidemic::rhi::CreateNullRhiDevice(epidemic::rhi::RhiDeviceDesc{false, "RegressionNullRhi"}).Value();
    epidemic::rhi::RhiSwapChainDesc swap_chain_desc;
    swap_chain_desc.surface_handle = epidemic::rhi::PresentationSurfaceHandle(reinterpret_cast<void *>(1));
    swap_chain_desc.width = 64;
    swap_chain_desc.height = 64;
    swap_chain_desc.color_format = epidemic::rhi::RhiPixelFormat::B8G8R8A8_UNorm;
    const auto swap_chain = device->CreateSwapChain(swap_chain_desc).Value();
    Assert(!swap_chain->Resize(0, 64).HasValue(), "Resize(0, h) must fail consistently");
    Assert(!swap_chain->Resize(64, 0).HasValue(), "Resize(w, 0) must fail consistently");
    Assert(!swap_chain->Resize(0, 0).HasValue(), "Resize(0, 0) must fail consistently");

    epidemic::core::Application initialize_failure_application({"InitializeFailureApplication"});
    RegisterApplicationCoreServices(initialize_failure_application, 1, std::nullopt);
    std::vector<std::string> trace;
    initialize_failure_application.Modules().Register(std::make_unique<ProbeModule>("core", std::vector<std::string>{}, trace));
    initialize_failure_application.Modules().Register(
        std::make_unique<ProbeModule>("runtime", std::vector<std::string>{"core"}, trace, false, true));
    Assert(initialize_failure_application.Bootstrap() == 0, "Bootstrap before initialize failure must succeed");
    bool initialize_failed = false;
    try
    {
        initialize_failure_application.Initialize();
    }
    catch (const std::exception &)
    {
        initialize_failed = true;
    }
    Assert(initialize_failed, "Partial initialization failure must propagate");
    Assert(initialize_failure_application.Shutdown() == 0, "Partial initialization failure must allow clean shutdown");
}
}

int main()
{
    return epidemic::tests::RunNamedTests({{"RegressionContracts", &TestRegressionContracts}});
}