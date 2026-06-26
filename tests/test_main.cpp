#include "core/config/configuration.h"
#include "core/diagnostics/logger.h"
#include "core/events/event_bus.h"
#include "core/modules/module_registry.h"
#include "core/services/service_container.h"
#include "core/tasks/task_scheduler.h"

#include <atomic>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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

void TestTaskScheduler()
{
    epidemic::core::tasks::SimpleTaskScheduler scheduler(1);
    std::atomic<int> counter = 0;

    scheduler.Schedule([&counter] { counter.fetch_add(1); });
    scheduler.WaitIdle();

    Assert(counter.load() == 1, "Scheduled task should execute exactly once");
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

int RunAllTests()
{
    struct NamedTest
    {
        const char *name;
        void (*run)();
    };

    const std::vector<NamedTest> tests{
        {"ServiceContainer", &TestServiceContainer},
        {"ModuleLifecycleOrderAndDependencies", &TestModuleLifecycleOrderAndDependencies},
        {"EventBus", &TestEventBus},
        {"TaskScheduler", &TestTaskScheduler},
        {"CircularModuleDependencyFails", &TestCircularModuleDependencyFails},
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
