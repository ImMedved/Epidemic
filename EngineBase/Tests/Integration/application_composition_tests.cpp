#include "../core_test_support.h"
#include "../test_assert.h"

#include <Epidemic/EngineBase/engine_base_support.h>
#include <Epidemic/Diagnostics/counters.h>

#include <vector>

namespace
{
using epidemic::tests::Assert;
using epidemic::tests::ProbeModule;

void TestRegisterEngineBaseAndLifecycle()
{
    epidemic::core::Application application({"CompositionTests"});
    static_cast<void>(epidemic::enginebase::RegisterEngineBase(
        application, {.runtime_name = "CompositionRuntime", .log_module = "CompositionTests", .worker_count = 2}));

    Assert(application.Services().Contains<epidemic::diagnostics::ILogger>(), "RegisterEngineBase must register ILogger");
    Assert(application.Services().Contains<epidemic::core::config::IConfiguration>(), "RegisterEngineBase must register IConfiguration");
    Assert(application.Services().Contains<epidemic::core::events::IEventBus>(), "RegisterEngineBase must register IEventBus");
    Assert(application.Services().Contains<epidemic::core::tasks::ITaskScheduler>(), "RegisterEngineBase must register ITaskScheduler");
    Assert(application.Services().Contains<epidemic::core::IMainThreadDispatcher>(), "RegisterEngineBase must register IMainThreadDispatcher");
    Assert(application.Services().Contains<epidemic::memory::IMemoryTracker>(), "RegisterEngineBase must register IMemoryTracker");

    std::vector<std::string> trace;
    application.Modules().Register(std::make_unique<ProbeModule>("core", std::vector<std::string>{}, trace));
    application.SetFrameLimit(1);
    Assert(application.Bootstrap() == 0, "Application bootstrap must succeed");
    Assert(application.Initialize() == 0, "Application initialize must succeed");
    Assert(application.Run() == 0, "Application run must succeed");
    Assert(application.Shutdown() == 0, "Application shutdown must succeed");

    Assert(epidemic::diagnostics::GlobalCounters().Get(epidemic::diagnostics::CounterId::Frames) >= 1,
           "Application run must update frame counters");
}

void TestPartialInitializationFailureAllowsShutdown()
{
    epidemic::core::Application application({"InitFailureTests"});
    static_cast<void>(epidemic::enginebase::RegisterEngineBase(
        application, {.runtime_name = "InitFailureRuntime", .log_module = "InitFailureTests"}));

    std::vector<std::string> trace;
    application.Modules().Register(std::make_unique<ProbeModule>("core", std::vector<std::string>{}, trace));
    application.Modules().Register(
        std::make_unique<ProbeModule>("runtime", std::vector<std::string>{"core"}, trace, false, true));

    Assert(application.Bootstrap() == 0, "Bootstrap before init failure must succeed");
    bool initialize_failed = false;
    try
    {
        application.Initialize();
    }
    catch (const std::exception &)
    {
        initialize_failed = true;
    }
    Assert(initialize_failed, "Initialize failure must propagate");
    Assert(application.Shutdown() == 0, "Shutdown after partial initialization failure must succeed");
}
}

int main()
{
    return epidemic::tests::RunNamedTests({
        {"RegisterEngineBaseAndLifecycle", &TestRegisterEngineBaseAndLifecycle},
        {"PartialInitializationFailureAllowsShutdown", &TestPartialInitializationFailureAllowsShutdown},
    });
}