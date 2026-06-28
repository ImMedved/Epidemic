#include "apps/epidemic_app/application_composition.h"
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
#include <functional>
#include <iostream>
#include <map>
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

class MutableConfiguration final : public epidemic::core::config::IConfiguration
{
  public:
    [[nodiscard]] std::optional<std::string> GetString(std::string_view key) const override
    {
        const auto it = values.find(std::string(key));
        if (it == values.end())
        {
            return std::nullopt;
        }

        return it->second;
    }

    void SetString(std::string key, std::string value) override
    {
        values[std::move(key)] = std::move(value);
    }

    std::map<std::string, std::string> values;
};

class ProbeModule final : public epidemic::core::IModule
{
  public:
    ProbeModule(std::string id,
                std::string name,
                std::vector<std::string> dependencies,
                std::vector<std::string> &trace,
                bool throw_on_bootstrap = false,
                bool throw_on_initialize = false)
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
    constexpr auto empty_string_id = epidemic::foundation::StringId::FromString("");
    constexpr auto first_string_id = epidemic::foundation::StringId::FromString("alpha");
    constexpr auto second_string_id = epidemic::foundation::StringId::FromString("alpha");
    constexpr auto third_string_id = epidemic::foundation::StringId::FromString("epidemic");
    static_assert(!empty_string_id.IsValid(), "Empty string id must be invalid");
    static_assert(first_string_id == second_string_id, "Equal string ids must hash identically");
    static_assert(!(first_string_id == third_string_id), "Different string ids must differ");

    const auto empty_name_id = epidemic::foundation::NameId::FromString("");
    const auto first_name_id = epidemic::foundation::NameId::FromString("Renderer.Main");
    const auto second_name_id = epidemic::foundation::NameId::FromString("Renderer.Main");
    Assert(!empty_name_id.IsValid(), "Empty name id must be invalid");
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

    const auto missing_library_name = "DefinitelyMissingLibrary_12345.dll";
    const auto missing_library_result =
        platform_runtime.LoadDynamicLibrary(epidemic::foundation::Path::FromString(missing_library_name));
    Assert(!missing_library_result.HasValue(), "Missing dynamic library must fail");
    Assert(missing_library_result.GetError().HasCode("platform.load_library_failed"),
           "Missing library must expose stable error code");
    Assert(missing_library_result.GetError().message.find(missing_library_name) != std::string::npos,
           "Missing library error must include the library name");

    const auto valid_library_result =
        platform_runtime.LoadDynamicLibrary(epidemic::foundation::Path::FromString("kernel32.dll"));
    Assert(valid_library_result.HasValue(), "Valid library must load before symbol failure test");

    const auto missing_symbol_name = "DefinitelyMissingSymbol_12345";
    const auto missing_symbol_result = valid_library_result.Value()->FindSymbol(missing_symbol_name);
    Assert(!missing_symbol_result.HasValue(), "Missing symbol lookup must fail");
    Assert(missing_symbol_result.GetError().HasCode("platform.symbol_not_found"),
           "Missing symbol must expose stable error code");
    Assert(missing_symbol_result.GetError().message.find(missing_symbol_name) != std::string::npos,
           "Missing symbol error must include the symbol name");
}

void TestRuntimePlaceholdersAreHonestNulls()
{
    epidemic::layers::runtime::NullVirtualFileSystem vfs;
    epidemic::layers::runtime::NullResourceManager resource_manager;
    epidemic::layers::runtime::NullRenderer renderer;
    epidemic::layers::runtime::NullScriptHost script_host;

    Assert(!vfs.Mount(epidemic::foundation::Path::FromString("content")),
           "Null VFS mount must report that nothing was mounted");
    Assert(!vfs.Exists(epidemic::foundation::Path::FromString("content/texture.dds")),
           "Null VFS must not claim that files exist");
    Assert(!resource_manager.HasResource("ship.texture"), "Null resource manager must not claim that resources exist");

    renderer.RequestFrame();
    Assert(!script_host.IsReady(), "Null script host must report not ready");
}

void TestServiceContainer()
{
    epidemic::core::ServiceContainer services;
    auto logger = std::make_shared<RecordingLogger>();
    services.RegisterInstance<ILogger>(logger);
    services.Emplace<epidemic::core::config::IConfiguration, MutableConfiguration>();

    Assert(services.Contains<ILogger>(), "Logger service should be registered");
    Assert(services.Get<ILogger>() == logger, "Service container must return the same logger instance");
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

void TestModuleRegistryRejectsNullModule()
{
    epidemic::core::ModuleRegistry registry;

    bool failed = false;
    try
    {
        registry.Register({});
    }
    catch (const std::exception &)
    {
        failed = true;
    }

    Assert(failed, "Module registry must reject null modules");
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

void TestRegisterAfterBootstrapFails()
{
    epidemic::core::ServiceContainer services;
    auto logger = std::make_shared<RecordingLogger>();
    services.RegisterInstance<ILogger>(logger);

    epidemic::core::ModuleRegistry registry;
    std::vector<std::string> trace;
    registry.Register(std::make_unique<ProbeModule>("core", "Core", std::vector<std::string>{}, trace));
    registry.BootstrapAll(services, *logger);

    bool failed = false;
    try
    {
        registry.Register(std::make_unique<ProbeModule>("late", "Late", std::vector<std::string>{}, trace));
    }
    catch (const std::exception &)
    {
        failed = true;
    }

    Assert(failed, "Module registry must reject late registration after bootstrap");
}

void TestBootstrapFailureTraceIsShutdownSafe()
{
    epidemic::core::ServiceContainer services;
    auto logger = std::make_shared<RecordingLogger>();
    services.RegisterInstance<ILogger>(logger);

    epidemic::core::ModuleRegistry registry;
    std::vector<std::string> trace;
    registry.Register(std::make_unique<ProbeModule>("first", "First", std::vector<std::string>{}, trace));
    registry.Register(std::make_unique<ProbeModule>("second", "Second", std::vector<std::string>{"first"}, trace, true));

    bool failed = false;
    try
    {
        registry.BootstrapAll(services, *logger);
    }
    catch (const std::exception &)
    {
        failed = true;
    }

    Assert(failed, "Bootstrap failure test must fail during bootstrap");
    registry.ShutdownAll(services, *logger);

    const std::vector<std::string> expected{
        "first:bootstrap",
        "second:bootstrap",
        "first:shutdown",
    };

    Assert(trace == expected, "Already bootstrapped modules must be shut down after bootstrap failure");
}

void TestInitializeFailureStillAllowsShutdownOfBootstrappedModules()
{
    epidemic::core::ServiceContainer services;
    auto logger = std::make_shared<RecordingLogger>();
    services.RegisterInstance<ILogger>(logger);

    epidemic::core::ModuleRegistry registry;
    std::vector<std::string> trace;
    registry.Register(std::make_unique<ProbeModule>("first", "First", std::vector<std::string>{}, trace));
    registry.Register(
        std::make_unique<ProbeModule>("second", "Second", std::vector<std::string>{"first"}, trace, false, true));

    bool failed = false;
    try
    {
        registry.BootstrapAll(services, *logger);
        registry.InitializeAll(services, *logger);
    }
    catch (const std::exception &)
    {
        failed = true;
    }

    Assert(failed, "Initialize failure test must fail during initialize");
    registry.ShutdownAll(services, *logger);

    const std::vector<std::string> expected{
        "first:bootstrap",
        "second:bootstrap",
        "first:initialize",
        "second:initialize",
        "second:shutdown",
        "first:shutdown",
    };

    Assert(trace == expected, "Bootstrapped modules must still shut down after initialize failure");
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

void TestEventBusUnsubscribeSyncHandler()
{
    epidemic::core::events::EventBus event_bus;
    int sync_total = 0;

    const auto token =
        event_bus.SubscribeSync<SyncTestEvent>([&sync_total](const SyncTestEvent &event) { sync_total += event.value; });
    Assert(event_bus.Unsubscribe(token), "Unsubscribe must remove an existing sync handler");

    event_bus.PublishSync(SyncTestEvent{5});
    Assert(sync_total == 0, "Unsubscribed sync handler must not receive events");
}

void TestEventBusUnsubscribeQueuedHandler()
{
    epidemic::core::events::EventBus event_bus;
    int queued_total = 0;

    const auto token = event_bus.SubscribeQueued<QueuedTestEvent>(
        [&queued_total](const QueuedTestEvent &event) { queued_total += event.value; });
    Assert(event_bus.Unsubscribe(token), "Unsubscribe must remove an existing queued handler");

    event_bus.Enqueue(QueuedTestEvent{5});
    Assert(event_bus.DrainQueued() == 1, "Queued events should still be drained after unsubscription");
    Assert(queued_total == 0, "Unsubscribed queued handler must not receive events");
}

void TestEventBusRejectsUnknownUnsubscribe()
{
    epidemic::core::events::EventBus event_bus;
    Assert(!event_bus.Unsubscribe(9999), "Unsubscribe must return false for unknown tokens");
}

void TestEventBusRejectsEmptyHandlers()
{
    epidemic::core::events::EventBus event_bus;
    std::function<void(const SyncTestEvent &)> empty_sync_handler;
    std::function<void(const QueuedTestEvent &)> empty_queued_handler;

    bool sync_failed = false;
    try
    {
        event_bus.SubscribeSync<SyncTestEvent>(empty_sync_handler);
    }
    catch (const std::exception &)
    {
        sync_failed = true;
    }

    bool queued_failed = false;
    try
    {
        event_bus.SubscribeQueued<QueuedTestEvent>(empty_queued_handler);
    }
    catch (const std::exception &)
    {
        queued_failed = true;
    }

    Assert(sync_failed && queued_failed, "Event bus must reject empty handlers");
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

void TestTaskSchedulerRethrowsTaskFailures()
{
    epidemic::core::tasks::SimpleTaskScheduler scheduler(1);
    scheduler.Schedule([] { throw std::runtime_error("task failure"); });

    bool failed = false;
    try
    {
        scheduler.WaitIdle();
    }
    catch (const std::runtime_error &exception)
    {
        failed = std::string(exception.what()) == "task failure";
    }

    Assert(failed, "WaitIdle must rethrow task failures");
    scheduler.WaitIdle();
}

void TestApplicationRegistersOnlyCoreServicesByDefault()
{
    epidemic::core::Application application;

    Assert(application.Bootstrap() == 0, "Application bootstrap must succeed");

    auto &services = application.Services();
    Assert(services.Contains<ILogger>(), "Application must register logger service");
    Assert(services.Contains<epidemic::core::config::IConfiguration>(),
           "Application must register configuration service");
    Assert(services.Contains<epidemic::core::events::IEventBus>(), "Application must register event bus");
    Assert(services.Contains<epidemic::core::tasks::ITaskScheduler>(), "Application must register task scheduler");
    Assert(!services.Contains<epidemic::layers::platform::IPlatformRuntime>(),
           "Application core must not register platform runtime");
    Assert(!services.Contains<epidemic::layers::runtime::IVirtualFileSystem>(),
           "Application core must not register runtime placeholders");

    Assert(application.Shutdown() == 0, "Application shutdown must succeed");
}

void TestApplicationRespectsPreRegisteredCoreServices()
{
    epidemic::core::Application application;

    auto logger = std::make_shared<RecordingLogger>();
    auto configuration = std::make_shared<MutableConfiguration>();
    auto event_bus = std::make_shared<epidemic::core::events::EventBus>();
    auto scheduler = std::make_shared<epidemic::core::tasks::SimpleTaskScheduler>(1);

    auto &services = application.Services();
    services.RegisterInstance<ILogger>(logger);
    services.RegisterInstance<epidemic::core::config::IConfiguration>(configuration);
    services.RegisterInstance<epidemic::core::events::IEventBus>(event_bus);
    services.RegisterInstance<epidemic::core::tasks::ITaskScheduler>(scheduler);

    Assert(application.Bootstrap() == 0, "Application bootstrap must succeed with pre-registered services");
    Assert(services.Get<ILogger>() == logger, "Application must preserve pre-registered logger");
    Assert(services.Get<epidemic::core::config::IConfiguration>() == configuration,
           "Application must preserve pre-registered configuration");
    Assert(services.Get<epidemic::core::events::IEventBus>() == event_bus,
           "Application must preserve pre-registered event bus");
    Assert(services.Get<epidemic::core::tasks::ITaskScheduler>() == scheduler,
           "Application must preserve pre-registered scheduler");
    Assert(configuration->GetString("application.name") == "Epidemic Engine v1.0",
           "Application bootstrap must still write application.name");

    Assert(application.Shutdown() == 0, "Application shutdown must succeed");
}

void TestApplicationRunDoesNotNeedPlatformService()
{
    epidemic::core::Application application;

    Assert(application.Bootstrap() == 0, "Application bootstrap must succeed");
    Assert(application.Initialize() == 0, "Application initialize must succeed");
    Assert(application.Run() == 0, "Application run must succeed without any platform service");
    Assert(application.Shutdown() == 0, "Application shutdown must succeed");
}

void TestApplicationShutdownAfterBootstrapFailure()
{
    epidemic::core::Application application;
    std::vector<std::string> trace;
    application.Modules().Register(std::make_unique<ProbeModule>("first", "First", std::vector<std::string>{}, trace));
    application.Modules().Register(
        std::make_unique<ProbeModule>("second", "Second", std::vector<std::string>{"first"}, trace, true));

    bool failed = false;
    try
    {
        application.Bootstrap();
    }
    catch (const std::exception &)
    {
        failed = true;
    }

    Assert(failed, "Application bootstrap failure test must fail");
    Assert(application.Shutdown() == 0, "Application shutdown must succeed after bootstrap failure");

    const std::vector<std::string> expected{
        "first:bootstrap",
        "second:bootstrap",
        "first:shutdown",
    };

    Assert(trace == expected, "Application shutdown must clean up modules that bootstrapped before failure");
}

void TestApplicationCompositionRootRegistersServices()
{
    epidemic::core::Application application;
    epidemic::apps::epidemic_app::RegisterApplicationServices(application.Services());

    auto &services = application.Services();
    Assert(services.Contains<epidemic::layers::platform::IPlatformRuntime>(),
           "Composition root must register platform runtime");
    Assert(services.Contains<epidemic::layers::runtime::IVirtualFileSystem>(),
           "Composition root must register VFS placeholder");
    Assert(services.Contains<epidemic::layers::runtime::IResourceManager>(),
           "Composition root must register resource manager placeholder");
    Assert(services.Contains<epidemic::layers::runtime::IRenderer>(),
           "Composition root must register renderer placeholder");
    Assert(services.Contains<epidemic::layers::runtime::IScriptHost>(),
           "Composition root must register script host placeholder");
}

void TestApplicationCompositionRootRegistersModules()
{
    epidemic::core::Application application;
    epidemic::apps::epidemic_app::RegisterApplicationModules(application.Modules());
    Assert(application.Modules().Size() == 3, "Composition root must register all demo modules");
}

void TestApplicationLifecycleSmoke()
{
    epidemic::core::Application application;
    epidemic::apps::epidemic_app::RegisterApplicationServices(application.Services());
    epidemic::apps::epidemic_app::RegisterApplicationModules(application.Modules());

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
        {"RuntimePlaceholdersAreHonestNulls", &TestRuntimePlaceholdersAreHonestNulls},
        {"ServiceContainer", &TestServiceContainer},
        {"ServiceContainerRejectsNullRegistration", &TestServiceContainerRejectsNullRegistration},
        {"ServiceContainerRejectsDuplicateRegistration", &TestServiceContainerRejectsDuplicateRegistration},
        {"ModuleRegistryRejectsNullModule", &TestModuleRegistryRejectsNullModule},
        {"ModuleRegistryRejectsDuplicateId", &TestModuleRegistryRejectsDuplicateId},
        {"ModuleLifecycleOrderAndDependencies", &TestModuleLifecycleOrderAndDependencies},
        {"MissingModuleDependencyFails", &TestMissingModuleDependencyFails},
        {"CircularModuleDependencyFails", &TestCircularModuleDependencyFails},
        {"RegisterAfterBootstrapFails", &TestRegisterAfterBootstrapFails},
        {"BootstrapFailureTraceIsShutdownSafe", &TestBootstrapFailureTraceIsShutdownSafe},
        {"InitializeFailureStillAllowsShutdownOfBootstrappedModules",
         &TestInitializeFailureStillAllowsShutdownOfBootstrappedModules},
        {"EventBus", &TestEventBus},
        {"EventBusUnsubscribeSyncHandler", &TestEventBusUnsubscribeSyncHandler},
        {"EventBusUnsubscribeQueuedHandler", &TestEventBusUnsubscribeQueuedHandler},
        {"EventBusRejectsUnknownUnsubscribe", &TestEventBusRejectsUnknownUnsubscribe},
        {"EventBusRejectsEmptyHandlers", &TestEventBusRejectsEmptyHandlers},
        {"EventBusQueuedFifoOrdering", &TestEventBusQueuedFifoOrdering},
        {"TaskScheduler", &TestTaskScheduler},
        {"TaskSchedulerRejectsNullTask", &TestTaskSchedulerRejectsNullTask},
        {"TaskSchedulerRethrowsTaskFailures", &TestTaskSchedulerRethrowsTaskFailures},
        {"ApplicationRegistersOnlyCoreServicesByDefault", &TestApplicationRegistersOnlyCoreServicesByDefault},
        {"ApplicationRespectsPreRegisteredCoreServices", &TestApplicationRespectsPreRegisteredCoreServices},
        {"ApplicationRunDoesNotNeedPlatformService", &TestApplicationRunDoesNotNeedPlatformService},
        {"ApplicationShutdownAfterBootstrapFailure", &TestApplicationShutdownAfterBootstrapFailure},
        {"ApplicationCompositionRootRegistersServices", &TestApplicationCompositionRootRegistersServices},
        {"ApplicationCompositionRootRegistersModules", &TestApplicationCompositionRootRegistersModules},
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
