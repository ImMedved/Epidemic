#include "core/application/application.h"
#include "core/config/configuration.h"
#include "core/diagnostics/logger.h"
#include "core/events/event_bus.h"
#include "core/modules/module_registry.h"
#include "core/services/service_container.h"
#include "core/tasks/task_scheduler.h"
#include "foundation/error/error.h"
#include "foundation/handles/handle.h"
#include "foundation/ids/string_id.h"
#include "foundation/paths/path.h"
#include "foundation/result/result.h"
#include "layers/platform/interfaces/iplatform_runtime.h"
#include "layers/platform/windows/windows_platform_runtime.h"
#include "layers/runtime/interfaces/irenderer.h"
#include "layers/runtime/interfaces/iresource_manager.h"
#include "layers/runtime/interfaces/iscript_host.h"
#include "layers/runtime/interfaces/ivirtual_file_system.h"
#include "layers/runtime/placeholders/null_services.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
using epidemic::core::diagnostics::ILogger;

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
    void Log(epidemic::core::diagnostics::LogLevel,
             std::string_view category,
             std::string_view message) override
    {
        entries.emplace_back(std::string(category) + ":" + std::string(message));
    }

    std::vector<std::string> entries;
};

class StubConfiguration final : public epidemic::core::config::IConfiguration
{
  public:
    [[nodiscard]] std::optional<std::string> GetString(std::string_view key) const override
    {
        if (key == "known")
        {
            return "value";
        }
        return std::nullopt;
    }

    void SetString(std::string, std::string) override
    {
    }
};

class ProbeModule final : public epidemic::core::IModule
{
  public:
    ProbeModule(std::string id, std::string name, std::vector<std::string> dependencies, std::vector<std::string> &trace)
        : manifest_{std::move(id), std::move(name), std::move(dependencies)}, trace_(trace)
    {
    }

    [[nodiscard]] const epidemic::core::ModuleManifest &Manifest() const override
    {
        return manifest_;
    }

    void OnBootstrap(epidemic::core::ServiceContainer &) override
    {
        trace_.push_back(manifest_.id + ":bootstrap");
    }

    void OnInitialize(epidemic::core::ServiceContainer &) override
    {
        trace_.push_back(manifest_.id + ":initialize");
    }

    void OnShutdown(epidemic::core::ServiceContainer &) override
    {
        trace_.push_back(manifest_.id + ":shutdown");
    }

  private:
    epidemic::core::ModuleManifest manifest_;
    std::vector<std::string> &trace_;
};

struct SyncTestEvent
{
    int value{};
};

struct QueuedTestEvent
{
    int value{};
};

void TestFoundationResult()
{
    const auto ok = epidemic::foundation::Result<int>::Success(42);
    Assert(ok.HasValue(), "Result<int>::Success must hold a value");
    Assert(ok.Value() == 42, "Successful result must return stored value");

    const auto failure =
        epidemic::foundation::Result<int>::Failure(epidemic::foundation::Error::Create("test.failure", "Failure path"));
    Assert(!failure.HasValue(), "Result<int>::Failure must not hold a value");
    Assert(failure.GetError().HasCode("test.failure"), "Failure result must expose stored error code");

    const auto void_ok = epidemic::foundation::Result<void>::Success();
    Assert(void_ok.HasValue(), "Result<void>::Success must represent success");
}

void TestFoundationPath()
{
    const auto path = epidemic::foundation::Path::FromString("resource\\textures\\..\\models");
    Assert(path.GenericString() == "resource/models", "Path must normalize separators and dot segments");

    const auto joined = path.Join("ship");
    Assert(joined.GenericString() == "resource/models/ship", "Path::Join must append child segment");
}

void TestFoundationIdsAndHandles()
{
    constexpr auto first_string_id = epidemic::foundation::StringId::FromString("alpha");
    constexpr auto second_string_id = epidemic::foundation::StringId::FromString("alpha");
    constexpr auto third_string_id = epidemic::foundation::StringId::FromString("epidemic");
    static_assert(first_string_id == second_string_id, "Equal string ids must hash identically");
    static_assert(!(first_string_id == third_string_id), "Different string ids must differ");

    const auto first_name_id = epidemic::foundation::NameId::FromString("Renderer.Main");
    const auto second_name_id = epidemic::foundation::NameId::FromString("Renderer.Main");
    Assert(first_name_id == second_name_id, "Equal name ids must compare equal");

    struct TextureTag
    {
    };

    const epidemic::foundation::Handle<TextureTag> invalid_handle;
    const epidemic::foundation::Handle<TextureTag> valid_handle(7, 3);

    Assert(!invalid_handle.IsValid(), "Default handle must be invalid");
    Assert(valid_handle.IsValid(), "Explicit handle must be valid");
    Assert(valid_handle.Index() == 7, "Handle index must be preserved");
    Assert(valid_handle.Generation() == 3, "Handle generation must be preserved");
}

void TestPlatformProcessInfoAndClock()
{
    epidemic::layers::platform::WindowsPlatformRuntime platform_runtime;

    Assert(platform_runtime.Name() == "WindowsPlatformRuntime", "Platform runtime name must match implementation");
    Assert(!platform_runtime.GetProcessInfo().working_directory.Empty(),
           "Platform runtime must expose non-empty working directory");
    Assert(!platform_runtime.GetProcessInfo().executable_path.Empty(),
           "Platform runtime must expose executable path");
    Assert(!platform_runtime.GetProcessInfo().arguments.empty(),
           "Platform runtime must expose command line arguments");

    const auto start = platform_runtime.Now();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto finish = platform_runtime.Now();
    Assert(finish >= start, "Platform clock must be monotonic");
}

void TestPlatformDynamicLibraryLoading()
{
    epidemic::layers::platform::WindowsPlatformRuntime platform_runtime;

    const auto library_result = platform_runtime.LoadDynamicLibrary(epidemic::foundation::Path::FromString("kernel32.dll"));
    Assert(library_result.HasValue(), "Platform runtime must load kernel32.dll");

    const auto symbol_result = library_result.Value()->FindSymbol("GetTickCount64");
    Assert(symbol_result.HasValue(), "Platform runtime must resolve GetTickCount64");
    Assert(symbol_result.Value() != nullptr, "Resolved dynamic library symbol must not be null");
}

void TestPlatformDynamicLibraryFailures()
{
    epidemic::layers::platform::WindowsPlatformRuntime platform_runtime;

    const auto empty_path_result = platform_runtime.LoadDynamicLibrary(epidemic::foundation::Path{});
    Assert(!empty_path_result.HasValue(), "Empty dynamic library path must fail");
    Assert(empty_path_result.GetError().HasCode("platform.empty_library_path"),
           "Empty path failure must expose stable error code");

    const auto valid_library_result =
        platform_runtime.LoadDynamicLibrary(epidemic::foundation::Path::FromString("kernel32.dll"));
    Assert(valid_library_result.HasValue(), "Valid library must load before symbol failure test");

    const auto missing_symbol_result = valid_library_result.Value()->FindSymbol("DefinitelyMissingSymbol_12345");
    Assert(!missing_symbol_result.HasValue(), "Missing symbol lookup must fail");
    Assert(missing_symbol_result.GetError().HasCode("platform.symbol_not_found"),
           "Missing symbol must expose stable error code");

    const auto missing_library_result =
        platform_runtime.LoadDynamicLibrary(epidemic::foundation::Path::FromString("DefinitelyMissingLibrary_12345.dll"));
    Assert(!missing_library_result.HasValue(), "Missing dynamic library must fail");
    Assert(missing_library_result.GetError().HasCode("platform.load_library_failed"),
           "Missing library must expose stable error code");
}

void TestRuntimePathContract()
{
    epidemic::layers::runtime::NullVirtualFileSystem vfs;

    Assert(!vfs.Mount(epidemic::foundation::Path{}), "Mount on empty path must fail");
    Assert(vfs.Mount(epidemic::foundation::Path::FromString("content")), "Mount on non-empty path must succeed");
    Assert(!vfs.Exists(epidemic::foundation::Path{}), "Exists on empty path must be false");
    Assert(vfs.Exists(epidemic::foundation::Path::FromString("content/texture.dds")),
           "Exists on non-empty path must be true in placeholder runtime");
}

void TestServiceContainer()
{
    epidemic::core::ServiceContainer services;
    auto logger = std::make_shared<RecordingLogger>();
    services.RegisterInstance<ILogger>(logger);
    services.Emplace<epidemic::core::config::IConfiguration, StubConfiguration>();

    Assert(services.Contains<ILogger>(), "Logger service should be registered");
    Assert(services.Get<ILogger>() == logger, "Service container must return the same logger instance");
    Assert(services.Get<epidemic::core::config::IConfiguration>()->GetString("known") == "value",
           "Configuration service should return its stored value");
}

void TestServiceContainerRejectsNullRegistration()
{
    epidemic::core::ServiceContainer services;

    bool failed = false;
    try
    {
        services.RegisterInstance<ILogger>({});
    }
    catch (const std::exception &)
    {
        failed = true;
    }

    Assert(failed, "Service container must reject null registration");
}

void TestServiceContainerRejectsDuplicateRegistration()
{
    epidemic::core::ServiceContainer services;
    services.RegisterInstance<ILogger>(std::make_shared<RecordingLogger>());

    bool failed = false;
    try
    {
        services.RegisterInstance<ILogger>(std::make_shared<RecordingLogger>());
    }
    catch (const std::exception &)
    {
        failed = true;
    }

    Assert(failed, "Service container must reject duplicate registration");
}

void TestModuleRegistryRejectsDuplicateId()
{
    epidemic::core::ModuleRegistry registry;
    std::vector<std::string> trace;
    registry.Register(std::make_unique<ProbeModule>("runtime", "Runtime", std::vector<std::string>{}, trace));

    bool failed = false;
    try
    {
        registry.Register(std::make_unique<ProbeModule>("runtime", "Runtime 2", std::vector<std::string>{}, trace));
    }
    catch (const std::exception &)
    {
        failed = true;
    }

    Assert(failed, "Module registry must reject duplicate ids");
}

void TestModuleLifecycleOrderAndDependencies()
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
}

void TestMissingModuleDependencyFails()
{
    epidemic::core::ServiceContainer services;
    auto logger = std::make_shared<RecordingLogger>();
    services.RegisterInstance<ILogger>(logger);

    epidemic::core::ModuleRegistry registry;
    std::vector<std::string> trace;
    registry.Register(
        std::make_unique<ProbeModule>("runtime", "Runtime", std::vector<std::string>{"missing.module"}, trace));

    bool failed = false;
    try
    {
        registry.BootstrapAll(services, *logger);
    }
    catch (const std::exception &)
    {
        failed = true;
    }

    Assert(failed, "Missing module dependency must fail during bootstrap planning");
}

void TestEventBus()
{
    epidemic::core::events::EventBus event_bus;

    int sync_total = 0;
    int queued_total = 0;

    event_bus.SubscribeSync<SyncTestEvent>([&sync_total](const SyncTestEvent &event) { sync_total += event.value; });
    event_bus.SubscribeQueued<QueuedTestEvent>(
        [&queued_total](const QueuedTestEvent &event) { queued_total += event.value; });

    event_bus.PublishSync(SyncTestEvent{4});
    event_bus.Enqueue(QueuedTestEvent{7});

    Assert(sync_total == 4, "Sync event should dispatch immediately");
    Assert(queued_total == 0, "Queued event must not dispatch before drain");

    const auto drained = event_bus.DrainQueued();
    Assert(drained == 1, "Exactly one queued event should be drained");
    Assert(queued_total == 7, "Queued event should dispatch during drain");
}

void TestEventBusQueuedFifoOrdering()
{
    epidemic::core::events::EventBus event_bus;
    std::vector<int> observed_values;

    event_bus.SubscribeQueued<QueuedTestEvent>(
        [&observed_values](const QueuedTestEvent &event) { observed_values.push_back(event.value); });

    event_bus.Enqueue(QueuedTestEvent{1});
    event_bus.Enqueue(QueuedTestEvent{2});
    event_bus.Enqueue(QueuedTestEvent{3});

    Assert(event_bus.DrainQueued() == 3, "All queued events must be drained");
    Assert(observed_values == std::vector<int>({1, 2, 3}), "Queued events must preserve FIFO order");
}

void TestTaskScheduler()
{
    epidemic::core::tasks::SimpleTaskScheduler scheduler(1);
    std::atomic<int> counter = 0;

    scheduler.Schedule([&counter] { counter.fetch_add(1); });
    scheduler.WaitIdle();

    Assert(counter.load() == 1, "Scheduled task should execute exactly once");
}

void TestTaskSchedulerRejectsNullTask()
{
    epidemic::core::tasks::SimpleTaskScheduler scheduler(1);

    bool failed = false;
    try
    {
        scheduler.Schedule({});
    }
    catch (const std::exception &)
    {
        failed = true;
    }

    Assert(failed, "Task scheduler must reject empty tasks");
}

void TestCircularModuleDependencyFails()
{
    epidemic::core::ServiceContainer services;
    auto logger = std::make_shared<RecordingLogger>();
    services.RegisterInstance<ILogger>(logger);

    epidemic::core::ModuleRegistry registry;
    std::vector<std::string> trace;
    registry.Register(std::make_unique<ProbeModule>("A", "A", std::vector<std::string>{"B"}, trace));
    registry.Register(std::make_unique<ProbeModule>("B", "B", std::vector<std::string>{"A"}, trace));

    bool failed = false;
    try
    {
        registry.BootstrapAll(services, *logger);
    }
    catch (const std::exception &)
    {
        failed = true;
    }

    Assert(failed, "Circular module dependency must fail during bootstrap planning");
}

void TestApplicationRegistersCoreServices()
{
    epidemic::core::Application application;

    Assert(application.Bootstrap() == 0, "Application bootstrap must succeed");

    auto &services = application.Services();
    Assert(services.Contains<ILogger>(), "Application must register logger service");
    Assert(services.Contains<epidemic::core::config::IConfiguration>(),
           "Application must register configuration service");
    Assert(services.Contains<epidemic::core::events::IEventBus>(), "Application must register event bus");
    Assert(services.Contains<epidemic::core::tasks::ITaskScheduler>(), "Application must register task scheduler");
    Assert(services.Contains<epidemic::layers::platform::IPlatformRuntime>(),
           "Application must register platform runtime");
    Assert(services.Contains<epidemic::layers::runtime::IVirtualFileSystem>(), "Application must register VFS");
    Assert(services.Contains<epidemic::layers::runtime::IResourceManager>(),
           "Application must register resource manager");
    Assert(services.Contains<epidemic::layers::runtime::IRenderer>(), "Application must register renderer");
    Assert(services.Contains<epidemic::layers::runtime::IScriptHost>(), "Application must register script host");

    Assert(application.Shutdown() == 0, "Application shutdown must succeed");
}

void TestApplicationLifecycleSmoke()
{
    epidemic::core::Application application;

    Assert(application.Bootstrap() == 0, "Application bootstrap must succeed");
    Assert(application.Initialize() == 0, "Application initialize must succeed");
    Assert(application.Run() == 0, "Application run must succeed");
    Assert(application.Shutdown() == 0, "Application shutdown must succeed");
}

int RunAllTests()
{
    struct NamedTest
    {
        const char *name;
        void (*run)();
    };

    const std::vector<NamedTest> tests{
        {"FoundationResult", &TestFoundationResult},
        {"FoundationPath", &TestFoundationPath},
        {"FoundationIdsAndHandles", &TestFoundationIdsAndHandles},
        {"PlatformProcessInfoAndClock", &TestPlatformProcessInfoAndClock},
        {"PlatformDynamicLibraryLoading", &TestPlatformDynamicLibraryLoading},
        {"PlatformDynamicLibraryFailures", &TestPlatformDynamicLibraryFailures},
        {"RuntimePathContract", &TestRuntimePathContract},
        {"ServiceContainer", &TestServiceContainer},
        {"ServiceContainerRejectsNullRegistration", &TestServiceContainerRejectsNullRegistration},
        {"ServiceContainerRejectsDuplicateRegistration", &TestServiceContainerRejectsDuplicateRegistration},
        {"ModuleRegistryRejectsDuplicateId", &TestModuleRegistryRejectsDuplicateId},
        {"ModuleLifecycleOrderAndDependencies", &TestModuleLifecycleOrderAndDependencies},
        {"MissingModuleDependencyFails", &TestMissingModuleDependencyFails},
        {"EventBus", &TestEventBus},
        {"EventBusQueuedFifoOrdering", &TestEventBusQueuedFifoOrdering},
        {"TaskScheduler", &TestTaskScheduler},
        {"TaskSchedulerRejectsNullTask", &TestTaskSchedulerRejectsNullTask},
        {"CircularModuleDependencyFails", &TestCircularModuleDependencyFails},
        {"ApplicationRegistersCoreServices", &TestApplicationRegistersCoreServices},
        {"ApplicationLifecycleSmoke", &TestApplicationLifecycleSmoke},
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
