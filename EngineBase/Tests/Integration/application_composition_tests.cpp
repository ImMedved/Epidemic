// This file exercises composition-level integration of baseline EngineBase services and lifecycle wiring.

#include "../core_test_support.h"
#include "../test_assert.h"

#include <Epidemic/EngineBase/engine_base_support.h>
#include <Epidemic/Diagnostics/counters.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <vector>

namespace
{
using epidemic::tests::Assert;
using epidemic::tests::ProbeModule;

// Verifies that RegisterEngineBase installs the expected services and that a minimal lifecycle succeeds.
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
    Assert(application.Services().IsSealed(), "Application initialize must seal the service container");
    Assert(application.Run() == 0, "Application run must succeed");
    Assert(application.Shutdown() == 0, "Application shutdown must succeed");

    Assert(epidemic::diagnostics::GlobalCounters().Get(epidemic::diagnostics::CounterId::Frames) >= 1,
           "Application run must update frame counters");
}

// Verifies that partial initialization failure still permits a clean shutdown path.
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

// Verifies that the frame loop no longer blocks every frame on unrelated background scheduler work.
void TestFrameLoopDoesNotWaitIdleEveryFrame()
{
    epidemic::core::Application application({"AsyncFrameLoopTests"});
    static_cast<void>(epidemic::enginebase::RegisterEngineBase(
        application, {.runtime_name = "AsyncFrameLoopTests", .log_module = "AsyncFrameLoopTests", .worker_count = 1}));

    std::atomic<bool> task_completed{false};
    application.AddFramePhaseHandler(
        epidemic::core::FramePhase::BeginFrame,
        [&application, &task_completed](const epidemic::core::FrameContext &) {
            static_cast<void>(application.Services().Get<epidemic::core::tasks::ITaskScheduler>()->Schedule(
                [&task_completed] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    task_completed.store(true, std::memory_order_relaxed);
                },
                "background-task"));
        },
        "AsyncFrameLoopTests::ScheduleBackgroundTask");

    application.SetFrameLimit(1);
    Assert(application.Bootstrap() == 0, "Bootstrap before async frame test must succeed");
    Assert(application.Initialize() == 0, "Initialize before async frame test must succeed");
    Assert(application.Run() == 0, "Run before async frame test must succeed");
    Assert(!task_completed.load(std::memory_order_relaxed),
           "Run must not wait for unrelated background work at the end of every frame");
    Assert(application.Shutdown() == 0, "Shutdown after async frame test must succeed");
    Assert(task_completed.load(std::memory_order_relaxed), "Shutdown must still drain scheduler work");
}
}

// Runs the application-composition integration-test group.
int main()
{
    return epidemic::tests::RunNamedTests({
        {"RegisterEngineBaseAndLifecycle", &TestRegisterEngineBaseAndLifecycle},
        {"PartialInitializationFailureAllowsShutdown", &TestPartialInitializationFailureAllowsShutdown},
        {"FrameLoopDoesNotWaitIdleEveryFrame", &TestFrameLoopDoesNotWaitIdleEveryFrame},
    });
}