#include <Epidemic/Core/application.h>
#include <Epidemic/Core/configuration.h>
#include <Epidemic/Core/event_bus.h>
#include <Epidemic/Core/imodule.h>
#include <Epidemic/Core/module_registry.h>
#include <Epidemic/Core/service_container.h>
#include <Epidemic/Core/task_scheduler.h>
#include <Epidemic/Diagnostics/logger.h>
#include <Epidemic/Foundation/error.h>
#include <Epidemic/Foundation/handle.h>
#include <Epidemic/Foundation/path.h>
#include <Epidemic/Foundation/result.h>
#include <Epidemic/Foundation/string_id.h>
#include <Epidemic/Platform/iplatform_runtime.h>
#include <Epidemic/Platform/windows_platform_runtime.h>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
using epidemic::diagnostics::ILogger;

struct TestFailure : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

void Assert(bool condition, std::string_view message)
{
    if (!condition)
    {
        throw TestFailure(std::string(message));
    }
}

class RecordingLogger final : public ILogger
{
  public:
    void Log(epidemic::diagnostics::LogLevel, std::string_view category, std::string_view message) override
    {
        entries.emplace_back(std::string(category) + ":" + std::string(message));
    }

    std::vector<std::string> entries;
};

class ProbeModule final : public epidemic::core::IModule
{
  public:
    ProbeModule(std::string id, std::string name, std::vector<std::string> dependencies, std::vector<std::string> &trace,
                bool throw_on_bootstrap = false, bool throw_on_initialize = false)
        : manifest_{std::move(id), std::move(name), std::move(dependencies)},
          trace_(trace),
          throw_on_bootstrap_(throw_on_bootstrap),
          throw_on_initialize_(throw_on_initialize)
    {
    }

    [[nodiscard]] const epidemic::core::ModuleManifest &Manifest() const override
    {
        return manifest_;
    }

    void OnBootstrap(epidemic::core::ServiceContainer &) override
    {
        trace_.push_back(manifest_.id + ":bootstrap");
        if (throw_on_bootstrap_)
        {
            throw std::runtime_error("bootstrap failure");
        }
    }

    void OnInitialize(epidemic::core::ServiceContainer &) override
    {
        trace_.push_back(manifest_.id + ":initialize");
        if (throw_on_initialize_)
        {
            throw std::runtime_error("initialize failure");
        }
    }

    void OnShutdown(epidemic::core::ServiceContainer &) override
    {
        trace_.push_back(manifest_.id + ":shutdown");
    }

  private:
    epidemic::core::ModuleManifest manifest_;
    std::vector<std::string> &trace_;
    bool throw_on_bootstrap_{false};
    bool throw_on_initialize_{false};
};

struct SyncTestEvent
{
    int value{};
};

struct QueuedTestEvent
{
    int value{};
};

void TestFoundationPrimitives()
{
    const auto ok = epidemic::foundation::Result<int>::Success(42);
    Assert(ok.HasValue(), "Result<int>::Success must hold a value");
    Assert(ok.Value() == 42, "Successful result must return stored value");

    const auto failure = epidemic::foundation::Result<int>::Failure(
        epidemic::foundation::Error::Create("test.failure", "Failure path"));
    Assert(!failure.HasValue(), "Result<int>::Failure must not hold a value");
    Assert(failure.GetError().HasCode("test.failure"), "Failure result must expose stored error code");

    const auto path = epidemic::foundation::Path::FromString("resource\\textures\\..\\models");
    Assert(path.GenericString() == "resource/models", "Path must normalize separators and dot segments");
    Assert(path.Join("ship").GenericString() == "resource/models/ship", "Path::Join must append child segment");

    constexpr auto empty_string_id = epidemic::foundation::StringId::FromString("");
    constexpr auto first_string_id = epidemic::foundation::StringId::FromString("alpha");
    constexpr auto second_string_id = epidemic::foundation::StringId::FromString("alpha");
    static_assert(!empty_string_id.IsValid(), "Empty string id must be invalid");
    static_assert(first_string_id == second_string_id, "Equal string ids must hash identically");

    struct TextureTag
    {
    };

    const epidemic::foundation::Handle<TextureTag> invalid_handle;
    const epidemic::foundation::Handle<TextureTag> valid_handle(7, 3);
    Assert(!invalid_handle.IsValid(), "Default handle must be invalid");
    Assert(valid_handle.IsValid(), "Explicit handle must be valid");
}

void TestServiceContainerContracts()
{
    epidemic::core::ServiceContainer services;
    auto logger = std::make_shared<RecordingLogger>();
    services.RegisterInstance<ILogger>(logger);
    Assert(services.Contains<ILogger>(), "Logger service should be registered");
    Assert(services.Get<ILogger>() == logger, "Service container must return the same logger instance");

    bool null_failed = false;
    try
    {
        services.RegisterInstance<epidemic::platform::IPlatformRuntime>({});
    }
    catch (const std::exception &)
    {
        null_failed = true;
    }
    Assert(null_failed, "Service container must reject null registration");

    bool duplicate_failed = false;
    try
    {
        services.RegisterInstance<ILogger>(std::make_shared<RecordingLogger>());
    }
    catch (const std::exception &)
    {
        duplicate_failed = true;
    }
    Assert(duplicate_failed, "Service container must reject duplicate registration");
}

void TestModuleLifecycleAndFailures()
{
    epidemic::core::ServiceContainer services;
    auto logger = std::make_shared<RecordingLogger>();
    services.RegisterInstance<ILogger>(logger);

    epidemic::core::ModuleRegistry registry;
    std::vector<std::string> trace;
    registry.Register(std::make_unique<ProbeModule>("runtime", "Runtime", std::vector<std::string>{"events"}, trace));
    registry.Register(std::make_unique<ProbeModule>("core", "Core", std::vector<std::string>{}, trace));
    registry.Register(std::make_unique<ProbeModule>("events", "Events", std::vector<std::string>{"core"}, trace));

    registry.BootstrapAll(services, *logger);
    registry.InitializeAll(services, *logger);
    registry.ShutdownAll(services, *logger);

    const std::vector<std::string> expected{
        "core:bootstrap",
        "events:bootstrap",
        "runtime:bootstrap",
        "core:initialize",
        "events:initialize",
        "runtime:initialize",
        "runtime:shutdown",
        "events:shutdown",
        "core:shutdown",
    };
    Assert(trace == expected, "Modules must respect dependency order and reverse shutdown order");

    epidemic::core::ModuleRegistry failing_registry;
    std::vector<std::string> failure_trace;
    failing_registry.Register(std::make_unique<ProbeModule>("first", "First", std::vector<std::string>{}, failure_trace));
    failing_registry.Register(std::make_unique<ProbeModule>("second", "Second", std::vector<std::string>{"first"}, failure_trace, true));

    bool bootstrap_failed = false;
    try
    {
        failing_registry.BootstrapAll(services, *logger);
    }
    catch (const std::exception &)
    {
        bootstrap_failed = true;
    }
    Assert(bootstrap_failed, "Bootstrap failure test must fail during bootstrap");
    failing_registry.ShutdownAll(services, *logger);
}

void TestEventBusContracts()
{
    epidemic::core::events::EventBus event_bus;

    int sync_total = 0;
    int queued_total = 0;

    const auto sync_token =
        event_bus.SubscribeSync<SyncTestEvent>([&sync_total](const SyncTestEvent &event) { sync_total += event.value; });
    const auto queued_token = event_bus.SubscribeQueued<QueuedTestEvent>(
        [&queued_total](const QueuedTestEvent &event) { queued_total += event.value; });

    event_bus.PublishSync(SyncTestEvent{4});
    event_bus.Enqueue(QueuedTestEvent{1});
    event_bus.Enqueue(QueuedTestEvent{2});
    event_bus.Enqueue(QueuedTestEvent{3});

    Assert(sync_total == 4, "Sync event should dispatch immediately");
    Assert(event_bus.DrainQueued() == 3, "All queued events must be drained");
    Assert(queued_total == 6, "Queued events must dispatch during drain");

    Assert(event_bus.Unsubscribe(sync_token), "Existing sync token must unsubscribe");
    Assert(event_bus.Unsubscribe(queued_token), "Existing queued token must unsubscribe");
    Assert(!event_bus.Unsubscribe(9999), "Unknown token must return false");

    std::function<void(const SyncTestEvent &)> empty_sync_handler;
    bool handler_failed = false;
    try
    {
        event_bus.SubscribeSync<SyncTestEvent>(empty_sync_handler);
    }
    catch (const std::exception &)
    {
        handler_failed = true;
    }
    Assert(handler_failed, "Event bus must reject empty handlers");
}

void TestTaskSchedulerContracts()
{
    epidemic::core::tasks::SimpleTaskScheduler scheduler(1);
    int counter = 0;

    scheduler.Schedule([&counter] { ++counter; });
    scheduler.WaitIdle();
    Assert(counter == 1, "Scheduled task should execute exactly once");

    bool null_failed = false;
    try
    {
        scheduler.Schedule({});
    }
    catch (const std::exception &)
    {
        null_failed = true;
    }
    Assert(null_failed, "Task scheduler must reject empty tasks");

    scheduler.Schedule([] { throw std::runtime_error("task failure"); });
    bool exception_failed = false;
    try
    {
        scheduler.WaitIdle();
    }
    catch (const std::runtime_error &exception)
    {
        exception_failed = std::string(exception.what()) == "task failure";
    }
    Assert(exception_failed, "WaitIdle must rethrow task failures");
    scheduler.WaitIdle();
}

void TestApplicationLifecycle()
{
    epidemic::core::Application application;
    Assert(application.Bootstrap() == 0, "Application bootstrap must succeed");
    Assert(application.Initialize() == 0, "Application initialize must succeed");
    Assert(application.Run() == 0, "Application run must succeed");
    Assert(application.Shutdown() == 0, "Application shutdown must succeed");

    auto &services = application.Services();
    Assert(services.Contains<ILogger>(), "Application must register logger service");
    Assert(services.Contains<epidemic::core::config::IConfiguration>(), "Application must register configuration service");
    Assert(services.Contains<epidemic::core::events::IEventBus>(), "Application must register event bus");
    Assert(services.Contains<epidemic::core::tasks::ITaskScheduler>(), "Application must register task scheduler");
}

void TestPlatformRuntime()
{
    epidemic::platform::WindowsPlatformRuntime platform_runtime;
    Assert(platform_runtime.Name() == "WindowsPlatformRuntime", "Platform runtime name must match implementation");
    Assert(!platform_runtime.GetProcessInfo().working_directory.Empty(), "Platform runtime must expose working directory");
    Assert(!platform_runtime.GetProcessInfo().executable_path.Empty(), "Platform runtime must expose executable path");
    Assert(!platform_runtime.GetProcessInfo().arguments.empty(), "Platform runtime must expose arguments");

    const auto start = platform_runtime.Now();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto finish = platform_runtime.Now();
    Assert(finish >= start, "Platform clock must be monotonic");

    const auto library_result = platform_runtime.LoadDynamicLibrary(epidemic::foundation::Path::FromString("kernel32.dll"));
    Assert(library_result.HasValue(), "Platform runtime must load kernel32.dll");
    const auto symbol_result = library_result.Value()->FindSymbol("GetTickCount64");
    Assert(symbol_result.HasValue(), "Platform runtime must resolve GetTickCount64");

    const auto empty_path_result = platform_runtime.LoadDynamicLibrary(epidemic::foundation::Path{});
    Assert(!empty_path_result.HasValue(), "Empty dynamic library path must fail");
}

int RunAllTests()
{
    struct NamedTest
    {
        const char *name;
        void (*run)();
    };

    const std::vector<NamedTest> tests{
        {"FoundationPrimitives", &TestFoundationPrimitives},
        {"ServiceContainerContracts", &TestServiceContainerContracts},
        {"ModuleLifecycleAndFailures", &TestModuleLifecycleAndFailures},
        {"EventBusContracts", &TestEventBusContracts},
        {"TaskSchedulerContracts", &TestTaskSchedulerContracts},
        {"ApplicationLifecycle", &TestApplicationLifecycle},
        {"PlatformRuntime", &TestPlatformRuntime},
    };

    for (const auto &test : tests)
    {
        test.run();
        std::cout << "[PASS] " << test.name << '\n';
    }

    return EXIT_SUCCESS;
}
} // namespace

int main()
{
    try
    {
        return RunAllTests();
    }
    catch (const std::exception &exception)
    {
        std::cerr << "[FAIL] " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}