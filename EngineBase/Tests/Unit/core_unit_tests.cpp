#include "../core_test_support.h"

#include <Epidemic/Core/event_bus.h>
#include <Epidemic/Core/main_thread_dispatcher.h>
#include <Epidemic/Core/module_registry.h>
#include <Epidemic/Core/service_container.h>
#include <Epidemic/Core/task_scheduler.h>
#include <Epidemic/Diagnostics/counters.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace
{
using epidemic::tests::Assert;
using epidemic::tests::ProbeModule;
using epidemic::tests::RecordingLogger;
using epidemic::tests::RegisterCoreServices;

struct SyncTestEvent
{
    int value{};
};

struct QueuedTestEvent
{
    int value{};
};

void TestServiceContainerContracts()
{
    epidemic::core::ServiceContainer services;
    auto logger = RegisterCoreServices(services);
    Assert(services.Contains<epidemic::diagnostics::ILogger>(), "Registered service must be discoverable");
    Assert(services.Get<epidemic::diagnostics::ILogger>() == logger, "ServiceContainer must return registered instance");

    bool duplicate_failed = false;
    try
    {
        services.RegisterInstance<epidemic::diagnostics::ILogger>(logger);
    }
    catch (const std::exception &)
    {
        duplicate_failed = true;
    }
    Assert(duplicate_failed, "Duplicate service registration must fail");

    bool null_failed = false;
    try
    {
        services.RegisterInstance<epidemic::memory::IMemoryTracker>(std::shared_ptr<epidemic::memory::IMemoryTracker>{});
    }
    catch (const std::invalid_argument &)
    {
        null_failed = true;
    }
    Assert(null_failed, "Null service registration must fail");
}

void TestEventBusContracts()
{
    epidemic::core::events::EventBus event_bus;
    int sync_sum = 0;
    int queued_sum = 0;

    event_bus.SubscribeSync<SyncTestEvent>([&sync_sum](const SyncTestEvent &event) { sync_sum += event.value; });
    event_bus.SubscribeQueued<QueuedTestEvent>([&queued_sum](const QueuedTestEvent &event) { queued_sum += event.value; });

    event_bus.PublishSync(SyncTestEvent{2});
    event_bus.Enqueue(QueuedTestEvent{3});
    Assert(event_bus.DrainQueued() == 1, "EventBus must drain queued events");
    Assert(sync_sum == 2, "Sync events must dispatch immediately");
    Assert(queued_sum == 3, "Queued events must dispatch on drain");
    Assert(!event_bus.Unsubscribe(9999), "Unsubscribe must return false for unknown handlers");

    bool empty_handler_failed = false;
    try
    {
        event_bus.SubscribeSync<SyncTestEvent>(std::function<void(const SyncTestEvent &)>{});
    }
    catch (const std::invalid_argument &)
    {
        empty_handler_failed = true;
    }
    Assert(empty_handler_failed, "Empty event handler must fail");
}

void TestTaskSchedulerAndDispatcherContracts()
{
    epidemic::core::tasks::SimpleTaskScheduler scheduler(1);
    const auto handle = scheduler.Schedule([] { throw std::runtime_error("task failure"); }, "throwing-task");
    bool task_failed = false;
    try
    {
        scheduler.Wait(handle);
    }
    catch (const std::runtime_error &exception)
    {
        task_failed = std::string(exception.what()) == "task failure";
    }
    Assert(task_failed, "Task exceptions must propagate");

    epidemic::diagnostics::GlobalCounters().Reset();
    epidemic::core::MainThreadDispatcher dispatcher;
    std::vector<int> order;
    dispatcher.Post([&order] { order.push_back(1); }, "first");
    dispatcher.Post([&order] { order.push_back(2); }, "second");
    Assert(dispatcher.Drain() == 2, "Dispatcher must drain queued tasks");
    Assert(order == std::vector<int>({1, 2}), "Dispatcher must execute tasks FIFO");

    bool empty_task_failed = false;
    try
    {
        dispatcher.Post({}, "empty");
    }
    catch (const std::invalid_argument &)
    {
        empty_task_failed = true;
    }
    Assert(empty_task_failed, "Dispatcher must reject empty tasks");
}

void TestModuleRegistryContracts()
{
    epidemic::core::ServiceContainer services;
    auto logger = RegisterCoreServices(services);

    epidemic::core::ModuleRegistry registry;
    std::vector<std::string> trace;
    registry.Register(std::make_unique<ProbeModule>("runtime", std::vector<std::string>{"events"}, trace));
    registry.Register(std::make_unique<ProbeModule>("core", std::vector<std::string>{}, trace));
    registry.Register(std::make_unique<ProbeModule>("events", std::vector<std::string>{"core"}, trace));
    registry.BootstrapAll(services, *logger);
    registry.InitializeAll(services, *logger);
    registry.TickAll(services, *logger, epidemic::core::FrameContext{});
    registry.ShutdownAll(services, *logger);
    Assert(!trace.empty(), "Module registry must execute lifecycle stages");

    bool duplicate_failed = false;
    try
    {
        epidemic::core::ModuleRegistry duplicate_registry;
        duplicate_registry.Register(std::make_unique<ProbeModule>("dup", std::vector<std::string>{}, trace));
        duplicate_registry.Register(std::make_unique<ProbeModule>("dup", std::vector<std::string>{}, trace));
    }
    catch (const std::exception &)
    {
        duplicate_failed = true;
    }
    Assert(duplicate_failed, "Duplicate module id must fail");

    bool missing_dependency_failed = false;
    try
    {
        epidemic::core::ModuleRegistry missing_dependency_registry;
        missing_dependency_registry.Register(
            std::make_unique<ProbeModule>("runtime", std::vector<std::string>{"missing"}, trace));
        missing_dependency_registry.BootstrapAll(services, *logger);
    }
    catch (const std::exception &)
    {
        missing_dependency_failed = true;
    }
    Assert(missing_dependency_failed, "Missing module dependency must fail");

    bool circular_failed = false;
    try
    {
        epidemic::core::ModuleRegistry circular_registry;
        circular_registry.Register(std::make_unique<ProbeModule>("a", std::vector<std::string>{"b"}, trace));
        circular_registry.Register(std::make_unique<ProbeModule>("b", std::vector<std::string>{"a"}, trace));
        circular_registry.BootstrapAll(services, *logger);
    }
    catch (const std::exception &)
    {
        circular_failed = true;
    }
    Assert(circular_failed, "Circular module dependency must fail");

    bool late_register_failed = false;
    try
    {
        epidemic::core::ModuleRegistry late_registry;
        late_registry.Register(std::make_unique<ProbeModule>("core", std::vector<std::string>{}, trace));
        late_registry.BootstrapAll(services, *logger);
        late_registry.Register(std::make_unique<ProbeModule>("late", std::vector<std::string>{}, trace));
    }
    catch (const std::exception &)
    {
        late_register_failed = true;
    }
    Assert(late_register_failed, "Registering a module after lifecycle start must fail");
}
}

int main()
{
    return epidemic::tests::RunNamedTests({
        {"ServiceContainerContracts", &TestServiceContainerContracts},
        {"EventBusContracts", &TestEventBusContracts},
        {"TaskSchedulerAndDispatcherContracts", &TestTaskSchedulerAndDispatcherContracts},
        {"ModuleRegistryContracts", &TestModuleRegistryContracts},
    });
}