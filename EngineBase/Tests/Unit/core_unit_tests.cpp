// This file exercises the Core module contracts including services, events, tasks, dispatch, modules, and application lifecycle behavior.

#include "../core_test_support.h"
#include "event_bus_token_policy.h"
#include "frame_count_policy.h"
#include "module_registry_test_hooks.h"
#include "task_scheduler_test_hooks.h"

#include <Epidemic/Core/event_bus.h>
#include <Epidemic/Core/main_thread_dispatcher.h>
#include <Epidemic/Core/module_registry.h>
#include <Epidemic/Core/service_container.h>
#include <Epidemic/Core/task_scheduler.h>
#include <Epidemic/Diagnostics/counters.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
using epidemic::tests::Assert;
using epidemic::tests::ProbeModule;
using epidemic::tests::RegisterCoreServices;

struct SyncTestEvent
{
    int value{};
};

struct QueuedTestEvent
{
    int value{};
};

struct CountingService
{
    virtual ~CountingService() = default;
};


class RecordingScheduler final : public epidemic::core::tasks::ITaskScheduler
{
  public:
    [[nodiscard]] epidemic::core::tasks::TaskHandle Schedule(Task, std::string = {}) override
    {
        throw std::runtime_error("RecordingScheduler does not run tasks");
    }

    [[nodiscard]] epidemic::core::tasks::TaskHandle Schedule(Task, epidemic::core::tasks::TaskGroup &, std::string = {}) override
    {
        throw std::runtime_error("RecordingScheduler does not run grouped tasks");
    }

    [[nodiscard]] bool Cancel(const epidemic::core::tasks::TaskHandle &) override
    {
        return false;
    }

    void Wait(const epidemic::core::tasks::TaskHandle &) override
    {
    }

    void Wait(const epidemic::core::tasks::TaskGroup &) override
    {
    }

    void WaitIdle() override
    {
        wait_idle_called = true;
    }

    void RequestStop() noexcept override
    {
        request_stop_called = true;
    }

    void Join() override
    {
        join_called = true;
    }

    void Shutdown() override
    {
        RequestStop();
        Join();
    }

    [[nodiscard]] std::size_t WorkerCount() const noexcept override
    {
        return 0;
    }

    [[nodiscard]] epidemic::core::tasks::TaskDiagnostics GetDiagnostics() const noexcept override
    {
        return {};
    }

    [[nodiscard]] std::vector<std::string> WorkerThreadNames() const override
    {
        return {};
    }

    bool request_stop_called{false};
    bool join_called{false};
    bool wait_idle_called{false};
};

class AsyncShutdownProbe final : public epidemic::core::IModule
{
  public:
    explicit AsyncShutdownProbe(std::atomic_bool &task_finished, bool &shutdown_saw_finished)
        : task_finished_(task_finished), shutdown_saw_finished_(shutdown_saw_finished)
    {
    }

    [[nodiscard]] const epidemic::core::ModuleManifest &Manifest() const override
    {
        return manifest_;
    }

    void Bootstrap(epidemic::core::ServiceContainer &) override
    {
    }

    void Initialize(epidemic::core::ServiceContainer &services) override
    {
        static_cast<void>(services.Get<epidemic::core::tasks::ITaskScheduler>()->Schedule([this] {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            task_finished_.store(true, std::memory_order_release);
        }));
    }

    void Tick(epidemic::core::ServiceContainer &, const epidemic::core::FrameContext &) override
    {
    }

    void Shutdown(epidemic::core::ServiceContainer &) override
    {
        shutdown_saw_finished_ = task_finished_.load(std::memory_order_acquire);
    }

  private:
    epidemic::core::ModuleManifest manifest_{"async-shutdown-probe", "AsyncShutdownProbe", {}};
    std::atomic_bool &task_finished_;
    bool &shutdown_saw_finished_;
};

struct CountingServiceImpl final : CountingService
{
    explicit CountingServiceImpl(int &construction_count) : construction_count_(construction_count)
    {
        ++construction_count_;
    }

    int &construction_count_;
};

struct ThrowOnCopyQueuedHandler
{
    std::shared_ptr<bool> throw_on_copy;
    int *sum{};

    ThrowOnCopyQueuedHandler(std::shared_ptr<bool> flag, int &target) : throw_on_copy(std::move(flag)), sum(&target)
    {
    }

    ThrowOnCopyQueuedHandler(const ThrowOnCopyQueuedHandler &other) : throw_on_copy(other.throw_on_copy), sum(other.sum)
    {
        if (*throw_on_copy)
        {
            throw std::bad_alloc{};
        }
    }

    ThrowOnCopyQueuedHandler(ThrowOnCopyQueuedHandler &&) noexcept = default;
    ThrowOnCopyQueuedHandler &operator=(const ThrowOnCopyQueuedHandler &) = default;
    ThrowOnCopyQueuedHandler &operator=(ThrowOnCopyQueuedHandler &&) noexcept = default;

    void operator()(const QueuedTestEvent &event) const
    {
        *sum += event.value;
    }
};

class RetryShutdownModule final : public epidemic::core::IModule
{
  public:
    RetryShutdownModule(std::string id, std::vector<std::string> dependencies, std::vector<std::string> &trace,
                        bool fail_first_shutdown)
        : manifest_{std::move(id), "RetryShutdownModule", std::move(dependencies)},
          trace_(trace),
          fail_first_shutdown_(fail_first_shutdown)
    {
    }

    [[nodiscard]] const epidemic::core::ModuleManifest &Manifest() const override
    {
        return manifest_;
    }
    void Bootstrap(epidemic::core::ServiceContainer &) override
    {
        trace_.push_back(manifest_.id + ":bootstrap");
    }
    void Initialize(epidemic::core::ServiceContainer &) override
    {
        trace_.push_back(manifest_.id + ":initialize");
    }
    void Tick(epidemic::core::ServiceContainer &, const epidemic::core::FrameContext &) override
    {
    }
    void Shutdown(epidemic::core::ServiceContainer &) override
    {
        trace_.push_back(manifest_.id + ":shutdown");
        ++shutdown_calls_;
        if (fail_first_shutdown_ && shutdown_calls_ == 1)
        {
            throw std::runtime_error("retryable shutdown failure");
        }
    }

  private:
    epidemic::core::ModuleManifest manifest_;
    std::vector<std::string> &trace_;
    bool fail_first_shutdown_{false};
    int shutdown_calls_{0};
};

// Verifies registration, lookup, duplicate rejection, and sealing behavior of ServiceContainer.
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

    int duplicate_emplace_construction_count = 0;
    static_cast<void>(services.Emplace<CountingService, CountingServiceImpl>(duplicate_emplace_construction_count));
    bool duplicate_emplace_failed = false;
    try
    {
        static_cast<void>(services.Emplace<CountingService, CountingServiceImpl>(duplicate_emplace_construction_count));
    }
    catch (const std::exception &)
    {
        duplicate_emplace_failed = true;
    }
    Assert(duplicate_emplace_failed, "Duplicate Emplace registration must fail");
    Assert(duplicate_emplace_construction_count == 1,
           "Duplicate Emplace must validate before constructing the implementation");

    epidemic::core::ServiceContainer duplicate_bundle_services;
    auto duplicate_bundle_instance = std::make_shared<CountingServiceImpl>(duplicate_emplace_construction_count);
    bool duplicate_bundle_failed = false;
    try
    {
        duplicate_bundle_services.RegisterInstancesAtomic<CountingService, CountingService>(duplicate_bundle_instance,
                                                                                            duplicate_bundle_instance);
    }
    catch (const std::invalid_argument &)
    {
        duplicate_bundle_failed = true;
    }
    Assert(duplicate_bundle_failed, "Atomic service bundles must reject duplicate service types explicitly");
    Assert(!duplicate_bundle_services.Contains<CountingService>(),
           "Rejected duplicate service bundle must leave the container unchanged");

    epidemic::core::ServiceContainer precommit_services;
    auto precommit_instance = std::make_shared<CountingServiceImpl>(duplicate_emplace_construction_count);
    bool precommit_failed = false;
    try
    {
        precommit_services.RegisterInstancesAtomicWithPreCommit(
            [] { throw std::runtime_error("precommit failure"); },
            std::shared_ptr<CountingService>(precommit_instance));
    }
    catch (const std::runtime_error &)
    {
        precommit_failed = true;
    }
    Assert(precommit_failed && !precommit_services.Contains<CountingService>(),
           "Failed composition pre-commit must not publish the staged service bundle");

    epidemic::core::ServiceContainer factory_services;
    bool factory_called = false;
    auto factory_instance = factory_services.RegisterInstanceFromFactoryAtomic<CountingService>([&] {
        factory_called = true;
        return std::make_shared<CountingServiceImpl>(duplicate_emplace_construction_count);
    });
    Assert(factory_called && factory_services.Get<CountingService>() == factory_instance,
           "Atomic service factory must publish exactly the instance returned by its non-reentrant callback");
    bool duplicate_factory_called = false;
    bool duplicate_factory_failed = false;
    try
    {
        static_cast<void>(factory_services.RegisterInstanceFromFactoryAtomic<CountingService>([&] {
            duplicate_factory_called = true;
            return factory_instance;
        }));
    }
    catch (const std::runtime_error &)
    {
        duplicate_factory_failed = true;
    }
    Assert(duplicate_factory_failed && !duplicate_factory_called,
           "Atomic service factory must reject duplicate ownership before invoking its callback");

    epidemic::core::ServiceContainer sealed_services;
    sealed_services.Seal();
    Assert(sealed_services.IsSealed(), "Seal must mark the service container as sealed");

    int sealed_emplace_construction_count = 0;
    bool sealed_failed = false;
    try
    {
        static_cast<void>(sealed_services.Emplace<CountingService, CountingServiceImpl>(sealed_emplace_construction_count));
    }
    catch (const std::exception &)
    {
        sealed_failed = true;
    }
    Assert(sealed_failed, "Registering a service after seal must fail");
    Assert(sealed_emplace_construction_count == 0,
           "Sealed Emplace must validate before constructing the implementation");
}

// Verifies synchronous and queued event-bus dispatch semantics.
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

    int reentrant_deliveries = 0;
    const auto reentrant_token = event_bus.SubscribeQueued<QueuedTestEvent>(
        [&event_bus, &reentrant_deliveries](const QueuedTestEvent &) {
            ++reentrant_deliveries;
            event_bus.Enqueue(QueuedTestEvent{1});
        });
    event_bus.Enqueue(QueuedTestEvent{1});
    Assert(event_bus.DrainQueued() == 1 && reentrant_deliveries == 1,
           "DrainQueued must process only the events present at the start of its wave");
    Assert(event_bus.DrainQueued() == 1 && reentrant_deliveries == 2,
           "Events enqueued by handlers must remain queued for the next wave");
    Assert(event_bus.Unsubscribe(reentrant_token), "Reentrant handler must unsubscribe cleanly");
    Assert(event_bus.DrainQueued() == 1, "Pending event from the final wave must remain drainable");

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

    epidemic::core::events::EventBus unsubscribe_bus;
    std::vector<int> unsubscribe_trace;
    epidemic::core::events::IEventBus::HandlerToken second_token = 0;
    static_cast<void>(unsubscribe_bus.SubscribeSync<SyncTestEvent>([&](const SyncTestEvent &) {
        unsubscribe_trace.push_back(1);
        static_cast<void>(unsubscribe_bus.Unsubscribe(second_token));
    }));
    second_token = unsubscribe_bus.SubscribeSync<SyncTestEvent>(
        [&](const SyncTestEvent &) { unsubscribe_trace.push_back(2); });
    unsubscribe_bus.PublishSync(SyncTestEvent{});
    Assert(unsubscribe_trace == std::vector<int>({1, 2}),
           "Unsubscribe during dispatch must affect the next publication, not the current snapshot");
    unsubscribe_trace.clear();
    unsubscribe_bus.PublishSync(SyncTestEvent{});
    Assert(unsubscribe_trace == std::vector<int>({1}),
           "A handler removed during the previous dispatch must not run again");

    epidemic::core::events::EventBus reentrant_sync_bus;
    std::vector<int> reentrant_sync_trace;
    bool nested = false;
    static_cast<void>(reentrant_sync_bus.SubscribeSync<SyncTestEvent>([&](const SyncTestEvent &) {
        reentrant_sync_trace.push_back(nested ? 2 : 1);
        if (!nested)
        {
            nested = true;
            reentrant_sync_bus.PublishSync(SyncTestEvent{});
            nested = false;
        }
    }));
    reentrant_sync_bus.PublishSync(SyncTestEvent{});
    Assert(reentrant_sync_trace == std::vector<int>({1, 2}),
           "Reentrant synchronous publication must not corrupt subscriber iteration");
}

// Verifies a queued event is retained when copying its subscriber set fails before dispatch.
void TestQueuedEventCopyFailureIsRetryable()
{
    epidemic::core::events::EventBus event_bus;
    int delivered_sum = 0;
    auto throw_on_copy = std::make_shared<bool>(false);
    ThrowOnCopyQueuedHandler handler(throw_on_copy, delivered_sum);
    event_bus.SubscribeQueued<QueuedTestEvent>(handler);
    event_bus.Enqueue(QueuedTestEvent{7});

    *throw_on_copy = true;
    bool copy_failed = false;
    try
    {
        static_cast<void>(event_bus.DrainQueued());
    }
    catch (const std::bad_alloc &)
    {
        copy_failed = true;
    }
    Assert(copy_failed, "Injected queued-handler copy failure must propagate as the drain failure");
    Assert(delivered_sum == 0, "A failed subscriber snapshot must not dispatch or lose the queued event");

    *throw_on_copy = false;
    Assert(event_bus.DrainQueued() == 1, "The same queued event must remain available for retry");
    Assert(delivered_sum == 7, "Retry must deliver the retained event exactly once");
    Assert(event_bus.DrainQueued() == 0, "Successful retry must consume the event exactly once");
}

void TestFramePhaseValidation()
{
    epidemic::core::Application application({"FramePhaseValidation"});
    bool count_rejected = false;
    bool unknown_rejected = false;
    try
    {
        application.AddFramePhaseHandler(epidemic::core::FramePhase::Count, [](const auto &) {});
    }
    catch (const std::invalid_argument &)
    {
        count_rejected = true;
    }

    try
    {
        application.AddFramePhaseHandler(static_cast<epidemic::core::FramePhase>(255), [](const auto &) {});
    }
    catch (const std::invalid_argument &)
    {
        unknown_rejected = true;
    }

    Assert(count_rejected && unknown_rejected, "Invalid frame phases must fail before indexing handler storage");
}

void TestCoreCounterBoundaries()
{
    using HandlerToken = epidemic::core::events::IEventBus::HandlerToken;
    const auto maximum_token = std::numeric_limits<HandlerToken>::max();
    Assert(epidemic::core::events::detail::CanAllocateHandlerToken(maximum_token),
           "The final non-zero EventBus handler token must remain allocatable");
    Assert(epidemic::core::events::detail::AdvanceHandlerToken(maximum_token) == 0,
           "EventBus token advancement must enter an exhausted sentinel instead of wrapping to a live identity");
    Assert(!epidemic::core::events::detail::CanAllocateHandlerToken(0),
           "The exhausted EventBus token sentinel must reject further allocation");

    const auto maximum_frame = std::numeric_limits<std::uint64_t>::max();
    Assert(epidemic::core::detail::AdvanceExecutedFrameCount(maximum_frame - 1) == maximum_frame,
           "Application frame count must reach its final representable value");
    Assert(epidemic::core::detail::AdvanceExecutedFrameCount(maximum_frame) == maximum_frame,
           "Application frame count must saturate rather than reuse frame zero");
}


// Verifies the complete application lifecycle, rejection of invalid lifecycle calls, and stable frame phase ordering.
void TestApplicationLifecycleAndFrameOrder()
{
    epidemic::core::Application invalid_application({"InvalidLifecycle"});
    RegisterCoreServices(invalid_application.Services());
    bool initialize_before_bootstrap_rejected = false;
    bool tick_before_bootstrap_rejected = false;
    bool run_before_bootstrap_rejected = false;
    try
    {
        static_cast<void>(invalid_application.Initialize());
    }
    catch (const std::runtime_error &)
    {
        initialize_before_bootstrap_rejected = true;
    }
    try
    {
        static_cast<void>(invalid_application.Tick());
    }
    catch (const std::runtime_error &)
    {
        tick_before_bootstrap_rejected = true;
    }
    try
    {
        static_cast<void>(invalid_application.Run());
    }
    catch (const std::runtime_error &)
    {
        run_before_bootstrap_rejected = true;
    }
    Assert(initialize_before_bootstrap_rejected && tick_before_bootstrap_rejected && run_before_bootstrap_rejected,
           "Initialize, Tick, and Run must reject the Constructed state");

    epidemic::core::Application application({"LifecycleAndOrder"});
    RegisterCoreServices(application.Services());
    std::vector<std::string> module_trace;
    application.Modules().Register(std::make_unique<ProbeModule>("core", std::vector<std::string>{}, module_trace));

    std::vector<epidemic::core::FramePhase> observed_phases;
    for (const auto phase : epidemic::core::FramePhaseOrder())
    {
        application.AddFramePhaseHandler(phase, [&observed_phases, phase](const epidemic::core::FrameContext &) {
            observed_phases.push_back(phase);
        });
    }

    Assert(application.Bootstrap() == 0, "Application bootstrap must succeed from Constructed");
    bool duplicate_bootstrap_rejected = false;
    try
    {
        static_cast<void>(application.Bootstrap());
    }
    catch (const std::runtime_error &)
    {
        duplicate_bootstrap_rejected = true;
    }
    Assert(duplicate_bootstrap_rejected, "Bootstrap must reject the Bootstrapped state");

    Assert(application.Initialize() == 0, "Application initialize must succeed from Bootstrapped");
    Assert(application.Services().IsSealed(), "Initialize must seal ServiceContainer");
    bool duplicate_initialize_rejected = false;
    try
    {
        static_cast<void>(application.Initialize());
    }
    catch (const std::runtime_error &)
    {
        duplicate_initialize_rejected = true;
    }
    Assert(duplicate_initialize_rejected, "Initialize must reject the Initialized state");

    bool registration_after_initialize_rejected = false;
    try
    {
        application.Services().RegisterInstance<epidemic::diagnostics::ILogger>(
            std::make_shared<epidemic::tests::RecordingLogger>());
    }
    catch (const std::runtime_error &)
    {
        registration_after_initialize_rejected = true;
    }
    Assert(registration_after_initialize_rejected, "ServiceContainer must remain sealed after initialization");

    application.SetFrameLimit(1);
    Assert(application.Run() == 0, "Application run must succeed from Initialized");
    Assert(observed_phases == std::vector<epidemic::core::FramePhase>(epidemic::core::FramePhaseOrder().begin(),
                                                                     epidemic::core::FramePhaseOrder().end()),
           "Frame handlers must execute in the canonical fixed phase order");

    epidemic::core::Application running_guard_application({"RunningLifecycleGuards", std::nullopt});
    RegisterCoreServices(running_guard_application.Services());
    std::vector<std::string> running_trace;
    running_guard_application.Modules().Register(
        std::make_unique<ProbeModule>("core", std::vector<std::string>{}, running_trace));
    bool bootstrap_while_running_rejected = false;
    bool initialize_while_running_rejected = false;
    bool tick_while_running_rejected = false;
    bool run_while_running_rejected = false;
    bool shutdown_while_running_rejected = false;
    running_guard_application.AddFramePhaseHandler(
        epidemic::core::FramePhase::BeginFrame,
        [&](const epidemic::core::FrameContext &) {
            try { static_cast<void>(running_guard_application.Bootstrap()); } catch (const std::runtime_error &) { bootstrap_while_running_rejected = true; }
            try { static_cast<void>(running_guard_application.Initialize()); } catch (const std::runtime_error &) { initialize_while_running_rejected = true; }
            try { static_cast<void>(running_guard_application.Tick()); } catch (const std::runtime_error &) { tick_while_running_rejected = true; }
            try { static_cast<void>(running_guard_application.Run()); } catch (const std::runtime_error &) { run_while_running_rejected = true; }
            try { static_cast<void>(running_guard_application.Shutdown()); } catch (const std::runtime_error &) { shutdown_while_running_rejected = true; }
            running_guard_application.RequestStop();
        });
    Assert(running_guard_application.Bootstrap() == 0, "Running-guard application must bootstrap");
    Assert(running_guard_application.Initialize() == 0, "Running-guard application must initialize");
    Assert(running_guard_application.Run() == 0, "Running-guard application must exit after stop request");
    Assert(bootstrap_while_running_rejected && initialize_while_running_rejected && tick_while_running_rejected &&
               run_while_running_rejected && shutdown_while_running_rejected,
           "All lifecycle methods must reject reentrant calls while the application is Running");
    Assert(running_guard_application.Shutdown() == 0, "Running-guard application must shut down after Run returns");

    Assert(application.Shutdown() == 0, "Application shutdown must succeed after Run");
    Assert(application.Shutdown() == 0, "Application shutdown must be idempotent");

    bool bootstrap_after_shutdown_rejected = false;
    bool initialize_after_shutdown_rejected = false;
    bool tick_after_shutdown_rejected = false;
    bool run_after_shutdown_rejected = false;
    try { static_cast<void>(application.Bootstrap()); } catch (const std::runtime_error &) { bootstrap_after_shutdown_rejected = true; }
    try { static_cast<void>(application.Initialize()); } catch (const std::runtime_error &) { initialize_after_shutdown_rejected = true; }
    try { static_cast<void>(application.Tick()); } catch (const std::runtime_error &) { tick_after_shutdown_rejected = true; }
    try { static_cast<void>(application.Run()); } catch (const std::runtime_error &) { run_after_shutdown_rejected = true; }
    Assert(bootstrap_after_shutdown_rejected && initialize_after_shutdown_rejected && tick_after_shutdown_rejected &&
               run_after_shutdown_rejected,
           "Lifecycle methods must reject the terminal ShutDown state");
}

// Verifies that a stop request raised before TickModules prevents an extra module tick in the current frame.
void TestStopRequestPreventsExtraTick()
{
    epidemic::core::Application application({"StopBeforeTick"});
    RegisterCoreServices(application.Services());
    std::vector<std::string> trace;
    application.Modules().Register(std::make_unique<ProbeModule>("core", std::vector<std::string>{}, trace));
    application.AddFramePhaseHandler(epidemic::core::FramePhase::PumpPlatformEvents,
                                     [&application](const epidemic::core::FrameContext &) { application.RequestStop(); });

    Assert(application.Bootstrap() == 0, "Stop-before-tick application must bootstrap");
    Assert(application.Initialize() == 0, "Stop-before-tick application must initialize");
    Assert(application.Run() == 0, "Stop-before-tick application must stop cleanly");
    Assert(std::find(trace.begin(), trace.end(), "core:tick") == trace.end(),
           "A stop request raised before TickModules must prevent an extra module tick");
    Assert(application.Shutdown() == 0, "Stop-before-tick application must shut down");
}

void TestAtomicFrameHandlerRegistration()
{
    epidemic::core::Application application({"AtomicHandlers", std::nullopt});
    RegisterCoreServices(application.Services());
    int invoked = 0;

    std::vector<epidemic::core::Application::FramePhaseHandlerRegistration> registrations;
    registrations.push_back({epidemic::core::FramePhase::BeginFrame,
                             [&invoked](const epidemic::core::FrameContext &) { ++invoked; }, "valid-first"});
    registrations.push_back({epidemic::core::FramePhase::EndFrame, {}, "invalid-second"});

    bool rejected = false;
    try
    {
        application.AddFramePhaseHandlersAtomic(std::move(registrations));
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }
    Assert(rejected, "Atomic handler batch must reject an invalid member");
    Assert(application.Bootstrap() == 0 && application.Initialize() == 0 && application.Tick() == 0,
           "Application must remain usable after rejected handler batch");
    Assert(invoked == 0, "Rejected handler batch must not publish earlier valid handlers");
    Assert(application.Shutdown() == 0, "Atomic handler test application must shut down cleanly");
}

// Verifies scheduler grouping, exception propagation, and main-thread dispatch behavior.
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

    epidemic::core::tasks::TaskGroup group;
    static_cast<void>(scheduler.Schedule([] { throw std::runtime_error("group batch failure"); }, group, "group-fail"));
    bool group_failed = false;
    try
    {
        scheduler.Wait(group);
    }
    catch (const std::runtime_error &exception)
    {
        group_failed = std::string(exception.what()) == "group batch failure";
    }
    Assert(group_failed, "TaskGroup must propagate the first exception for the failed batch");

    static_cast<void>(scheduler.Schedule([] {}, group, "group-success"));
    bool old_group_error_rethrown = false;
    try
    {
        scheduler.Wait(group);
    }
    catch (const std::exception &)
    {
        old_group_error_rethrown = true;
    }
    Assert(!old_group_error_rethrown, "TaskGroup must not retain an exception after Wait consumes it");

    epidemic::core::tasks::SimpleTaskScheduler request_stop_scheduler(1);
    std::atomic<bool> request_stop_completed{false};
    const auto request_stop_handle = request_stop_scheduler.Schedule([&request_stop_scheduler, &request_stop_completed] {
        request_stop_scheduler.RequestStop();
        request_stop_completed.store(true, std::memory_order_relaxed);
    }, "worker-request-stop");
    request_stop_scheduler.Wait(request_stop_handle);
    request_stop_scheduler.Join();
    request_stop_scheduler.Shutdown();
    Assert(request_stop_completed.load(std::memory_order_relaxed), "RequestStop must be safe from a worker task");

    epidemic::core::tasks::SimpleTaskScheduler self_shutdown_scheduler(1);
    const auto self_shutdown_handle = self_shutdown_scheduler.Schedule([&self_shutdown_scheduler] {
        self_shutdown_scheduler.Shutdown();
    }, "worker-shutdown");
    bool self_shutdown_failed_safely = false;
    try
    {
        self_shutdown_scheduler.Wait(self_shutdown_handle);
    }
    catch (const std::runtime_error &exception)
    {
        self_shutdown_failed_safely = std::string(exception.what()).find("worker thread") != std::string::npos;
    }
    self_shutdown_scheduler.Shutdown();
    Assert(self_shutdown_failed_safely, "Shutdown from a worker must fail deterministically instead of self-joining");

    epidemic::core::tasks::SimpleTaskScheduler worker_wait_scheduler(1);
    std::atomic_bool handle_wait_rejected{false};
    const auto parent_handle = worker_wait_scheduler.Schedule([&] {
        const auto child_handle = worker_wait_scheduler.Schedule([] {});
        try
        {
            worker_wait_scheduler.Wait(child_handle);
        }
        catch (const std::runtime_error &)
        {
            handle_wait_rejected.store(true, std::memory_order_release);
        }
    });
    worker_wait_scheduler.Wait(parent_handle);
    worker_wait_scheduler.WaitIdle();
    Assert(handle_wait_rejected.load(std::memory_order_acquire), "Worker Wait(handle) must fail before blocking");

    std::atomic_bool idle_wait_rejected{false};
    const auto idle_wait_handle = worker_wait_scheduler.Schedule([&] {
        try
        {
            worker_wait_scheduler.WaitIdle();
        }
        catch (const std::runtime_error &)
        {
            idle_wait_rejected.store(true, std::memory_order_release);
        }
    });
    worker_wait_scheduler.Wait(idle_wait_handle);
    Assert(idle_wait_rejected.load(std::memory_order_acquire), "Worker WaitIdle must fail before blocking");

    epidemic::core::tasks::TaskGroup self_wait_group;
    std::atomic_bool group_wait_rejected{false};
    const auto group_wait_handle = worker_wait_scheduler.Schedule([&] {
        try
        {
            worker_wait_scheduler.Wait(self_wait_group);
        }
        catch (const std::runtime_error &)
        {
            group_wait_rejected.store(true, std::memory_order_release);
        }
    }, self_wait_group);
    worker_wait_scheduler.Wait(group_wait_handle);
    worker_wait_scheduler.Wait(self_wait_group);
    Assert(group_wait_rejected.load(std::memory_order_acquire), "Worker Wait(group) must fail before blocking");

    std::atomic_bool release_owned_scheduler{false};
    std::atomic_bool self_destroy_task_finished{false};
    auto worker_owned_scheduler = std::make_shared<epidemic::core::tasks::SimpleTaskScheduler>(1);
    static_cast<void>(worker_owned_scheduler->Schedule(
        [owned_scheduler = worker_owned_scheduler, &release_owned_scheduler, &self_destroy_task_finished]() mutable {
            while (!release_owned_scheduler.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            owned_scheduler.reset();
            self_destroy_task_finished.store(true, std::memory_order_release);
        }));
    worker_owned_scheduler.reset();
    release_owned_scheduler.store(true, std::memory_order_release);
    const auto self_destroy_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!self_destroy_task_finished.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < self_destroy_deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Assert(self_destroy_task_finished.load(std::memory_order_acquire),
           "Scheduler facade may be released by its worker without self-join or use-after-free");

    std::vector<int> exception_order;
    dispatcher.Post([&exception_order] { exception_order.push_back(10); }, "before-throw");
    dispatcher.Post([&exception_order] {
        exception_order.push_back(20);
        throw std::runtime_error("dispatcher batch failure");
    }, "throw-middle");
    dispatcher.Post([&exception_order] { exception_order.push_back(30); }, "after-throw");
    bool dispatcher_batch_failed = false;
    try
    {
        static_cast<void>(dispatcher.Drain());
    }
    catch (const std::runtime_error &exception)
    {
        dispatcher_batch_failed = std::string(exception.what()) == "dispatcher batch failure";
    }
    Assert(dispatcher_batch_failed, "Dispatcher must still report the first task exception");
    Assert(exception_order == std::vector<int>({10, 20, 30}),
           "Dispatcher must continue draining later tasks after one task throws");

    std::vector<int> deferred_dispatch_order;
    dispatcher.Post([&] {
        deferred_dispatch_order.push_back(1);
        dispatcher.Post([&] { deferred_dispatch_order.push_back(2); }, "posted-during-drain");
    }, "posting-task");
    Assert(dispatcher.Drain() == 1 && deferred_dispatch_order == std::vector<int>({1}),
           "Tasks posted while draining must remain pending for the next drain wave");
    Assert(dispatcher.Drain() == 1 && deferred_dispatch_order == std::vector<int>({1, 2}),
           "Dispatcher must retain and complete tasks posted during the previous drain wave");

    epidemic::core::tasks::SimpleTaskScheduler cancel_scheduler(1);
    std::atomic_bool release_blocker{false};
    std::atomic_bool cancelled_task_ran{false};
    const auto blocker = cancel_scheduler.Schedule([&] {
        while (!release_blocker.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
    }, "cancel-blocker");
    const auto cancelled = cancel_scheduler.Schedule([&] { cancelled_task_ran.store(true, std::memory_order_release); },
                                                     "cancelled-task");
    Assert(cancel_scheduler.Cancel(cancelled), "Cancel must succeed for a task that is still queued");
    Assert(!cancel_scheduler.Cancel(cancelled), "Cancelling the same task twice must report no second cancellation");
    cancel_scheduler.Wait(cancelled);
    Assert(!cancelled_task_ran.load(std::memory_order_acquire), "A cancelled queued task must never execute");
    release_blocker.store(true, std::memory_order_release);
    cancel_scheduler.Wait(blocker);
    cancel_scheduler.WaitIdle();

    epidemic::core::tasks::TaskHandle invalid_handle;
    Assert(!cancel_scheduler.Cancel(invalid_handle), "Cancel must reject an invalid default handle without side effects");
    cancel_scheduler.Wait(invalid_handle);

    epidemic::core::tasks::SimpleTaskScheduler foreign_scheduler(1);
    const auto foreign_handle = foreign_scheduler.Schedule([] {}, "foreign-task");
    Assert(!cancel_scheduler.Cancel(foreign_handle), "Cancel must reject a handle owned by another scheduler");
    bool foreign_wait_rejected = false;
    try
    {
        cancel_scheduler.Wait(foreign_handle);
    }
    catch (const std::invalid_argument &)
    {
        foreign_wait_rejected = true;
    }
    Assert(foreign_wait_rejected, "Wait must reject a handle owned by another scheduler");
    foreign_scheduler.Wait(foreign_handle);

    epidemic::core::tasks::TaskGroup foreign_group;
    const auto grouped_foreign_handle = foreign_scheduler.Schedule([] {}, foreign_group, "foreign-group-task");
    foreign_scheduler.Wait(grouped_foreign_handle);
    bool foreign_group_schedule_rejected = false;
    try
    {
        static_cast<void>(cancel_scheduler.Schedule([] {}, foreign_group, "wrong-scheduler-group"));
    }
    catch (const std::invalid_argument &)
    {
        foreign_group_schedule_rejected = true;
    }
    Assert(foreign_group_schedule_rejected, "A TaskGroup must remain bound to the scheduler that first used it");
    foreign_scheduler.Wait(foreign_group);

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

// Verifies module dependency ordering and lifecycle failure handling.
void TestTaskGroupScheduleFailureDoesNotBindGroup()
{
    epidemic::core::tasks::TaskGroup group;
    {
        epidemic::core::tasks::SimpleTaskScheduler first_scheduler(1);
        bool bad_alloc_seen = false;
        epidemic::core::tasks::testing::SetFaultPoint(
            epidemic::core::tasks::testing::FaultPoint::BeforeQueuePush);
        try
        {
            static_cast<void>(first_scheduler.Schedule([] {}, group));
        }
        catch (const std::bad_alloc &)
        {
            bad_alloc_seen = true;
        }
        epidemic::core::tasks::testing::ClearFaultPoint();
        Assert(bad_alloc_seen, "Injected grouped Schedule queue failure must surface as bad_alloc");
        first_scheduler.WaitIdle();
    }

    epidemic::core::tasks::SimpleTaskScheduler second_scheduler(1);
    const auto recovered = second_scheduler.Schedule([] {}, group);
    second_scheduler.Wait(recovered);
    second_scheduler.Wait(group);
}

void TestSchedulerConstructorFailureCleansStartedWorkers()
{
    epidemic::diagnostics::GlobalCounters().Reset();
    bool bad_alloc_seen = false;
    epidemic::core::tasks::testing::SetFaultPoint(epidemic::core::tasks::testing::FaultPoint::AfterWorkerStart);
    try
    {
        epidemic::core::tasks::SimpleTaskScheduler scheduler(2);
    }
    catch (const std::bad_alloc &)
    {
        bad_alloc_seen = true;
    }
    epidemic::core::tasks::testing::ClearFaultPoint();
    Assert(bad_alloc_seen, "Injected post-worker scheduler construction failure must surface as bad_alloc");
    Assert(epidemic::diagnostics::GlobalCounters().Get(epidemic::diagnostics::CounterId::WorkerCount) == 0,
           "Failed scheduler construction must not publish WorkerCount");

    epidemic::core::tasks::SimpleTaskScheduler recovered(1);
    Assert(recovered.WorkerCount() == 1, "Scheduler construction must recover after injected constructor failure");
}

void TestExpiredSchedulerIdentityIsRejected()
{
    epidemic::core::tasks::TaskHandle stale_handle;
    epidemic::core::tasks::TaskGroup stale_group;
    {
        auto scheduler = std::make_unique<epidemic::core::tasks::SimpleTaskScheduler>(1);
        stale_handle = scheduler->Schedule([] {}, stale_group);
        scheduler->Wait(stale_handle);
        scheduler->Wait(stale_group);
    }

    epidemic::core::tasks::SimpleTaskScheduler replacement(1);
    Assert(!replacement.Cancel(stale_handle), "Expired scheduler handle must never be accepted by a replacement scheduler");
    bool stale_handle_rejected = false;
    try
    {
        replacement.Wait(stale_handle);
    }
    catch (const std::invalid_argument &)
    {
        stale_handle_rejected = true;
    }
    Assert(stale_handle_rejected, "Wait must reject a handle whose scheduler lifetime has ended");

    bool stale_group_rejected = false;
    try
    {
        static_cast<void>(replacement.Schedule([] {}, stale_group));
    }
    catch (const std::invalid_argument &)
    {
        stale_group_rejected = true;
    }
    Assert(stale_group_rejected, "A group bound to an expired scheduler lifetime must not rebind implicitly");
}

void TestModuleRegistrationAllocationFailureAtomicity()
{
    epidemic::core::ModuleRegistry registry;
    std::vector<std::string> trace;

    bool bad_alloc_seen = false;
    epidemic::core::testing::SetModuleRegistryFaultPoint(
        epidemic::core::testing::ModuleRegistryFaultPoint::BeforeRegistrationCommit);
    try
    {
        registry.Register(std::make_unique<ProbeModule>("atomic-register", std::vector<std::string>{}, trace));
    }
    catch (const std::bad_alloc &)
    {
        bad_alloc_seen = true;
    }
    epidemic::core::testing::ClearModuleRegistryFaultPoint();

    Assert(bad_alloc_seen, "Injected module registration commit failure must surface as bad_alloc");
    Assert(registry.Size() == 0, "Failed module registration must leave the live module list empty");
    registry.Register(std::make_unique<ProbeModule>("atomic-register", std::vector<std::string>{}, trace));
    Assert(registry.Size() == 1, "Retry after failed module registration must not see ghost id/name entries");
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

    epidemic::core::ModuleRegistry shutdown_failure_registry;
    std::vector<std::string> shutdown_trace;
    shutdown_failure_registry.Register(std::make_unique<ProbeModule>("core", std::vector<std::string>{}, shutdown_trace));
    shutdown_failure_registry.Register(
        std::make_unique<ProbeModule>("middle", std::vector<std::string>{"core"}, shutdown_trace, false, false, false, true));
    shutdown_failure_registry.Register(
        std::make_unique<ProbeModule>("top", std::vector<std::string>{"middle"}, shutdown_trace));
    shutdown_failure_registry.BootstrapAll(services, *logger);
    shutdown_failure_registry.InitializeAll(services, *logger);
    bool shutdown_failed = false;
    try
    {
        shutdown_failure_registry.ShutdownAll(services, *logger);
    }
    catch (const std::runtime_error &exception)
    {
        shutdown_failed = std::string(exception.what()) == "shutdown failure";
    }
    Assert(shutdown_failed, "Module shutdown failure must be reported after best-effort shutdown");
    Assert(shutdown_trace == std::vector<std::string>({"core:bootstrap", "middle:bootstrap", "top:bootstrap",
                                                       "core:initialize", "middle:initialize", "top:initialize",
                                                       "top:shutdown", "middle:shutdown", "core:shutdown"}),
           "ModuleRegistry must continue shutting down remaining modules after one module throws");

    std::vector<std::string> retry_shutdown_trace;
    epidemic::core::ModuleRegistry retry_shutdown_registry;
    retry_shutdown_registry.Register(
        std::make_unique<RetryShutdownModule>("core", std::vector<std::string>{}, retry_shutdown_trace, false));
    retry_shutdown_registry.Register(std::make_unique<RetryShutdownModule>(
        "middle", std::vector<std::string>{"core"}, retry_shutdown_trace, true));
    retry_shutdown_registry.Register(std::make_unique<RetryShutdownModule>(
        "top", std::vector<std::string>{"middle"}, retry_shutdown_trace, false));
    retry_shutdown_registry.BootstrapAll(services, *logger);
    retry_shutdown_registry.InitializeAll(services, *logger);
    bool first_retry_shutdown_failed = false;
    try
    {
        retry_shutdown_registry.ShutdownAll(services, *logger);
    }
    catch (const std::runtime_error &exception)
    {
        first_retry_shutdown_failed = std::string(exception.what()) == "retryable shutdown failure";
    }
    Assert(first_retry_shutdown_failed, "First shutdown must preserve the retryable module cleanup failure");
    retry_shutdown_registry.ShutdownAll(services, *logger);
    Assert(std::count(retry_shutdown_trace.begin(), retry_shutdown_trace.end(), "top:shutdown") == 1 &&
               std::count(retry_shutdown_trace.begin(), retry_shutdown_trace.end(), "core:shutdown") == 1 &&
               std::count(retry_shutdown_trace.begin(), retry_shutdown_trace.end(), "middle:shutdown") == 2,
           "Shutdown retry must skip modules already cleaned and retry only the failed cleanup");

    epidemic::core::Application shutdown_failure_application(epidemic::core::ApplicationOptions{"ShutdownFailureApplication", std::nullopt});
    auto app_logger = std::make_shared<epidemic::tests::RecordingLogger>();
    auto app_scheduler = std::make_shared<RecordingScheduler>();
    auto app_configuration = std::make_shared<epidemic::core::config::BasicConfiguration>();
    app_configuration->SetRuntimeName("ShutdownFailureApplication");
    app_configuration->SetWorkerCount(1);
    shutdown_failure_application.Services().RegisterInstance<epidemic::diagnostics::ILogger>(app_logger);
    shutdown_failure_application.Services().RegisterInstance<epidemic::core::config::IConfiguration>(app_configuration);
    shutdown_failure_application.Services().Emplace<epidemic::core::events::IEventBus, epidemic::core::events::EventBus>();
    shutdown_failure_application.Services().RegisterInstance<epidemic::core::tasks::ITaskScheduler>(app_scheduler);
    shutdown_failure_application.Services().Emplace<epidemic::core::IMainThreadDispatcher, epidemic::core::MainThreadDispatcher>();
    std::vector<std::string> app_shutdown_trace;
    shutdown_failure_application.Modules().Register(
        std::make_unique<ProbeModule>("failing", std::vector<std::string>{}, app_shutdown_trace, false, false, false, true));
    Assert(shutdown_failure_application.Bootstrap() == 0, "Shutdown failure application bootstrap must succeed");
    Assert(shutdown_failure_application.Initialize() == 0, "Shutdown failure application initialize must succeed");
    bool application_shutdown_failed = false;
    try
    {
        static_cast<void>(shutdown_failure_application.Shutdown());
    }
    catch (const std::runtime_error &exception)
    {
        application_shutdown_failed = std::string(exception.what()) == "shutdown failure";
    }
    Assert(application_shutdown_failed, "Application shutdown must preserve the first module shutdown error");
    Assert(app_scheduler->request_stop_called && app_scheduler->join_called && app_scheduler->wait_idle_called,
           "Application must stop and drain scheduler even after module shutdown failure");

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

    std::vector<std::string> deterministic_trace_a;
    epidemic::core::ModuleRegistry deterministic_registry_a;
    deterministic_registry_a.Register(std::make_unique<ProbeModule>("z", std::vector<std::string>{"b", "a"}, deterministic_trace_a));
    deterministic_registry_a.Register(std::make_unique<ProbeModule>("b", std::vector<std::string>{}, deterministic_trace_a));
    deterministic_registry_a.Register(std::make_unique<ProbeModule>("a", std::vector<std::string>{}, deterministic_trace_a));
    deterministic_registry_a.BootstrapAll(services, *logger);
    deterministic_registry_a.InitializeAll(services, *logger);

    std::vector<std::string> deterministic_trace_b;
    epidemic::core::ModuleRegistry deterministic_registry_b;
    deterministic_registry_b.Register(std::make_unique<ProbeModule>("a", std::vector<std::string>{}, deterministic_trace_b));
    deterministic_registry_b.Register(std::make_unique<ProbeModule>("z", std::vector<std::string>{"a", "b"}, deterministic_trace_b));
    deterministic_registry_b.Register(std::make_unique<ProbeModule>("b", std::vector<std::string>{}, deterministic_trace_b));
    deterministic_registry_b.BootstrapAll(services, *logger);
    deterministic_registry_b.InitializeAll(services, *logger);
    Assert(deterministic_trace_a == deterministic_trace_b,
           "Module initialization order must be canonical and independent of registration/dependency list order");
    Assert(deterministic_trace_a == std::vector<std::string>({"a:bootstrap", "b:bootstrap", "z:bootstrap",
                                                              "a:initialize", "b:initialize", "z:initialize"}),
           "Canonical module order must place dependencies before dependents with stable id tie-breaking");
    deterministic_registry_a.ShutdownAll(services, *logger);
    deterministic_registry_b.ShutdownAll(services, *logger);

    std::vector<std::string> partial_trace;
    epidemic::core::ModuleRegistry partial_registry;
    partial_registry.Register(std::make_unique<ProbeModule>("a", std::vector<std::string>{}, partial_trace));
    partial_registry.Register(std::make_unique<ProbeModule>("b", std::vector<std::string>{"a"}, partial_trace, false, true));
    partial_registry.Register(std::make_unique<ProbeModule>("c", std::vector<std::string>{"b"}, partial_trace));
    partial_registry.BootstrapAll(services, *logger);
    bool partial_initialize_failed = false;
    try
    {
        partial_registry.InitializeAll(services, *logger);
    }
    catch (const std::runtime_error &)
    {
        partial_initialize_failed = true;
    }
    Assert(partial_initialize_failed, "Partial initialization failure must propagate");
    partial_registry.ShutdownAll(services, *logger);
    Assert(partial_trace == std::vector<std::string>({"a:bootstrap", "b:bootstrap", "c:bootstrap",
                                                      "a:initialize", "b:initialize",
                                                      "c:shutdown", "b:shutdown", "a:shutdown"}),
           "Partial initialization failure must release every bootstrapped module in reverse dependency order");

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

void TestApplicationQuiescesSchedulerBeforeModuleShutdown()
{
    epidemic::core::Application application({"SchedulerQuiesce"});
    RegisterCoreServices(application.Services());
    std::atomic_bool task_finished{false};
    bool shutdown_saw_finished = false;
    application.Modules().Register(std::make_unique<AsyncShutdownProbe>(task_finished, shutdown_saw_finished));

    Assert(application.Bootstrap() == 0, "Quiesce test application must bootstrap");
    Assert(application.Initialize() == 0, "Quiesce test application must initialize");
    Assert(application.Shutdown() == 0, "Quiesce test application must shut down");
    Assert(shutdown_saw_finished, "Module state must not be destroyed before scheduled work finishes");
}
} // namespace

// Runs the Core unit-test group.
int main()
{
    return epidemic::tests::RunNamedTests({
        {"ServiceContainerContracts", &TestServiceContainerContracts},
        {"EventBusContracts", &TestEventBusContracts},
        {"QueuedEventCopyFailureIsRetryable", &TestQueuedEventCopyFailureIsRetryable},
        {"FramePhaseValidation", &TestFramePhaseValidation},
        {"CoreCounterBoundaries", &TestCoreCounterBoundaries},
        {"ApplicationLifecycleAndFrameOrder", &TestApplicationLifecycleAndFrameOrder},
        {"StopRequestPreventsExtraTick", &TestStopRequestPreventsExtraTick},
        {"AtomicFrameHandlerRegistration", &TestAtomicFrameHandlerRegistration},
        {"TaskSchedulerAndDispatcherContracts", &TestTaskSchedulerAndDispatcherContracts},
        {"TaskGroupScheduleFailureDoesNotBindGroup", &TestTaskGroupScheduleFailureDoesNotBindGroup},
        {"SchedulerConstructorFailureCleansStartedWorkers", &TestSchedulerConstructorFailureCleansStartedWorkers},
        {"ExpiredSchedulerIdentityIsRejected", &TestExpiredSchedulerIdentityIsRejected},
        {"ModuleRegistrationAllocationFailureAtomicity", &TestModuleRegistrationAllocationFailureAtomicity},
        {"ModuleRegistryContracts", &TestModuleRegistryContracts},
        {"ApplicationQuiescesSchedulerBeforeModuleShutdown", &TestApplicationQuiescesSchedulerBeforeModuleShutdown},
    });
}
