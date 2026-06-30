#include <Epidemic/Core/application.h>
#include <Epidemic/Core/basic_configuration.h>
#include <Epidemic/Core/configuration.h>
#include <Epidemic/Core/event_bus.h>
#include <Epidemic/Core/imodule.h>
#include <Epidemic/Core/module_registry.h>
#include <Epidemic/Core/service_container.h>
#include <Epidemic/Core/task_scheduler.h>
#include <Epidemic/Diagnostics/counters.h>
#include <Epidemic/Diagnostics/logger.h>
#include <Epidemic/Diagnostics/profiling.h>
#include <Epidemic/Diagnostics/thread_context.h>
#include <Epidemic/Foundation/error.h>
#include <Epidemic/Foundation/handle.h>
#include <Epidemic/Foundation/path.h>
#include <Epidemic/Foundation/result.h>
#include <Epidemic/Foundation/string_id.h>
#include <Epidemic/Foundation/time.h>
#include <Epidemic/Memory/allocation_tag.h>
#include <Epidemic/Memory/allocator.h>
#include <Epidemic/Memory/memory_tracker.h>
#include <Epidemic/Platform/iplatform_runtime.h>
#include <Epidemic/Platform/windows_platform_runtime.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
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

bool NearlyEqual(double left, double right, double epsilon = 1e-9)
{
    return std::abs(left - right) <= epsilon;
}

class RecordingLogger final : public ILogger
{
  public:
    struct Entry
    {
        epidemic::diagnostics::LogLevel level{};
        std::string module_name;
        std::string category;
        std::string message;
        std::string thread_name;
    };

    void Log(const epidemic::diagnostics::LogMessage &message) override
    {
        entries.emplace_back(std::string(message.module_name) + ":" + std::string(message.category) + ":" +
                             std::string(message.message));
        records.push_back(Entry{message.level, std::string(message.module_name), std::string(message.category),
                                std::string(message.message), std::string(message.thread_name)});
    }

    std::vector<std::string> entries;
    std::vector<Entry> records;
};

class ProbeModule final : public epidemic::core::IModule
{
  public:
    ProbeModule(std::string id, std::string name, std::vector<std::string> dependencies, std::vector<std::string> &trace,
                bool throw_on_bootstrap = false, bool throw_on_initialize = false, bool throw_on_tick = false)
        : manifest_{std::move(id), std::move(name), std::move(dependencies)},
          trace_(trace),
          throw_on_bootstrap_(throw_on_bootstrap),
          throw_on_initialize_(throw_on_initialize),
          throw_on_tick_(throw_on_tick)
    {
    }

    [[nodiscard]] const epidemic::core::ModuleManifest &Manifest() const override
    {
        return manifest_;
    }

    void Bootstrap(epidemic::core::ServiceContainer &) override
    {
        trace_.push_back(manifest_.id + ":bootstrap");
        if (throw_on_bootstrap_)
        {
            throw std::runtime_error("bootstrap failure");
        }
    }

    void Initialize(epidemic::core::ServiceContainer &) override
    {
        trace_.push_back(manifest_.id + ":initialize");
        if (throw_on_initialize_)
        {
            throw std::runtime_error("initialize failure");
        }
    }

    void Tick(epidemic::core::ServiceContainer &) override
    {
        trace_.push_back(manifest_.id + ":tick");
        if (throw_on_tick_)
        {
            throw std::runtime_error("tick failure");
        }
    }

    void Shutdown(epidemic::core::ServiceContainer &) override
    {
        trace_.push_back(manifest_.id + ":shutdown");
    }

  private:
    epidemic::core::ModuleManifest manifest_;
    std::vector<std::string> &trace_;
    bool throw_on_bootstrap_{false};
    bool throw_on_initialize_{false};
    bool throw_on_tick_{false};
};

struct SyncTestEvent
{
    int value{};
};

struct QueuedTestEvent
{
    int value{};
};


std::shared_ptr<RecordingLogger> RegisterCoreServices(epidemic::core::ServiceContainer &services,
                                                      std::size_t worker_count = 2,
                                                      std::optional<std::string> runtime_name = std::nullopt)
{
    auto logger = std::make_shared<RecordingLogger>();
    services.RegisterInstance<ILogger>(logger);

    const auto configuration =
        services.Emplace<epidemic::core::config::IConfiguration, epidemic::core::config::BasicConfiguration>();
    if (runtime_name.has_value())
    {
        configuration->SetRuntimeName(*runtime_name);
    }
    configuration->SetWorkerCount(worker_count);
    configuration->SetMemoryTrackingEnabled(true);
    configuration->SetRhiDebugEnabled(false);
    configuration->SetDefaultWindowWidth(1280);
    configuration->SetDefaultWindowHeight(720);

    services.Emplace<epidemic::core::events::IEventBus, epidemic::core::events::EventBus>();
    services.Emplace<epidemic::core::tasks::ITaskScheduler, epidemic::core::tasks::SimpleTaskScheduler>(worker_count);
    return logger;
}

std::shared_ptr<RecordingLogger> RegisterApplicationCoreServices(epidemic::core::Application &application,
                                                                 std::size_t worker_count = 2,
                                                                 std::optional<std::string> runtime_name = std::nullopt)
{
    return RegisterCoreServices(application.Services(), worker_count, std::move(runtime_name));
}

void TestFoundationPrimitives()
{
    const auto ok = epidemic::foundation::Result<int>::Success(42);
    Assert(ok.HasValue(), "Result<int>::Success must hold a value");
    Assert(ok.Value() == 42, "Successful result must return stored value");

    const auto failure = epidemic::foundation::Result<int>::Failure(
        epidemic::foundation::Error::Create("test.failure", "Failure path", "foundation.tests"));
    Assert(!failure.HasValue(), "Result<int>::Failure must not hold a value");
    Assert(failure.GetError().HasCode("test.failure"), "Failure result must expose stored error code");
    Assert(failure.GetError().message == "Failure path", "Failure result must expose stored error message");
    Assert(failure.GetError().HasContext(), "Failure result must expose optional error context");
    Assert(failure.GetError().context == "foundation.tests", "Failure result must preserve error context");

    const auto path = epidemic::foundation::Path::FromString("resource\\textures\\..\\models");
    Assert(path.GenericString() == "resource/models", "Path must normalize separators and dot segments");
    Assert(path.Join("ship").GenericString() == "resource/models/ship", "Path::Join must append child segment");

    constexpr auto empty_string_id = epidemic::foundation::StringId::FromString("");
    constexpr auto first_string_id = epidemic::foundation::StringId::FromString("alpha");
    constexpr auto second_string_id = epidemic::foundation::StringId::FromString("alpha");
    static_assert(!empty_string_id.IsValid(), "Empty string id must be invalid");
    static_assert(first_string_id == second_string_id, "Equal string ids must hash identically");

    const auto empty_name_id = epidemic::foundation::NameId::FromString("");
    const auto first_name_id = epidemic::foundation::NameId::FromString("Renderer.Main");
    const auto second_name_id = epidemic::foundation::NameId::FromString("Renderer.Main");
    Assert(!empty_name_id.IsValid(), "Empty name id must be invalid");
    Assert(first_name_id == second_name_id, "Equal name ids must compare equal");

    const auto module_id = epidemic::foundation::ModuleId::FromString("core.module");
    const auto duplicate_module_id = epidemic::foundation::ModuleId::FromString("core.module");
    const auto service_id = epidemic::foundation::ServiceId::FromString("diagnostics.logger");
    const auto duplicate_service_id = epidemic::foundation::ServiceId::FromString("diagnostics.logger");
    const auto event_type_id = epidemic::foundation::EventTypeId::FromString("window.close_requested");
    const auto duplicate_event_type_id = epidemic::foundation::EventTypeId::FromString("window.close_requested");
    Assert(module_id.IsValid(), "ModuleId must be valid for non-empty text");
    Assert(service_id.IsValid(), "ServiceId must be valid for non-empty text");
    Assert(event_type_id.IsValid(), "EventTypeId must be valid for non-empty text");
    Assert(module_id == duplicate_module_id, "ModuleId generation must be stable");
    Assert(service_id == duplicate_service_id, "ServiceId generation must be stable");
    Assert(event_type_id == duplicate_event_type_id, "EventTypeId generation must be stable");

    std::unordered_set<epidemic::foundation::ModuleId> module_ids;
    module_ids.insert(module_id);
    module_ids.insert(duplicate_module_id);
    Assert(module_ids.size() == 1, "ModuleId hash support must deduplicate equal ids");

    struct TextureTag
    {
    };

    const epidemic::foundation::Handle<TextureTag> invalid_handle;
    const epidemic::foundation::Handle<TextureTag> valid_handle(7, 3);
    const epidemic::foundation::Handle<TextureTag> same_valid_handle(7, 3);
    Assert(!invalid_handle.IsValid(), "Default handle must be invalid");
    Assert(valid_handle.IsValid(), "Explicit handle must be valid");
    Assert(valid_handle == same_valid_handle, "Equal handles must compare equal");

    std::unordered_set<epidemic::foundation::Handle<TextureTag>> handles;
    handles.insert(valid_handle);
    handles.insert(same_valid_handle);
    Assert(handles.size() == 1, "Handle hash support must deduplicate equal handles");

    const auto frame_time_from_seconds = epidemic::foundation::FrameTime::FromSeconds(0.5);
    const auto frame_time_from_milliseconds = epidemic::foundation::FrameTime::FromMilliseconds(16.5);
    Assert(NearlyEqual(frame_time_from_seconds.Milliseconds(), 500.0), "FrameTime seconds conversion must work");
    Assert(NearlyEqual(frame_time_from_milliseconds.Seconds(), 0.0165), "FrameTime millisecond conversion must work");

    epidemic::foundation::FrameIndex frame_index;
    Assert(frame_index.Value() == 0, "Default FrameIndex must start at zero");
    ++frame_index;
    Assert(frame_index.Value() == 1, "FrameIndex increment must work");
    Assert(frame_index.Next().Value() == 2, "FrameIndex next value must work");

    std::unordered_set<epidemic::foundation::FrameIndex> frame_indices;
    frame_indices.insert(frame_index);
    frame_indices.insert(epidemic::foundation::FrameIndex(1));
    Assert(frame_indices.size() == 1, "FrameIndex hash support must deduplicate equal values");
}

void TestMemoryBaseline()
{
    using epidemic::memory::AllocationTag;
    using epidemic::memory::DefaultAllocator;
    using epidemic::memory::MemoryTracker;
    using epidemic::memory::TrackingAllocator;

    MemoryTracker tracker(true);
    tracker.RecordAllocate(AllocationTag::Core, 64);
    tracker.RecordAllocate(AllocationTag::Core, 16);
    tracker.RecordAllocate(AllocationTag::Platform, 32);

    const auto core_stats = tracker.GetStatistics(AllocationTag::Core);
    Assert(core_stats.allocated_bytes == 80, "Memory tracker must accumulate per-tag usage");
    Assert(core_stats.peak_allocated_bytes == 80, "Memory tracker must track peak usage");
    Assert(core_stats.allocation_count == 2, "Memory tracker must count allocations per tag");

    const auto platform_stats = tracker.GetStatistics(AllocationTag::Platform);
    Assert(platform_stats.allocated_bytes == 32, "Memory tracker must store independent per-tag usage");
    Assert(platform_stats.allocation_count == 1, "Memory tracker must store independent per-tag allocation count");

    tracker.RecordFree(AllocationTag::Core, 48);
    Assert(tracker.GetUsage(AllocationTag::Core) == 32, "Memory tracker must reduce usage on free");
    Assert(tracker.GetStatistics(AllocationTag::Core).peak_allocated_bytes == 80,
           "Memory tracker must preserve the peak usage after free");

    tracker.SetBudget(AllocationTag::Core, 24);
    tracker.SetBudget(AllocationTag::Platform, 64);
    Assert(tracker.GetBudget(AllocationTag::Core).has_value(), "Memory tracker must store a budget");
    Assert(*tracker.GetBudget(AllocationTag::Core) == 24, "Memory tracker must return the configured budget");
    Assert(tracker.IsOverBudget(AllocationTag::Core), "Memory tracker must detect when usage is over budget");
    Assert(!tracker.IsOverBudget(AllocationTag::Platform), "Memory tracker must not flag tags under budget");
    Assert(!tracker.IsOverBudget(AllocationTag::Diagnostics), "Memory tracker must treat missing budgets as non-failing");

    tracker.RecordAllocate(static_cast<AllocationTag>(255), 8);
    const auto unknown_stats = tracker.GetStatistics(AllocationTag::Unknown);
    Assert(unknown_stats.allocated_bytes == 8, "Memory tracker must route invalid tags into Unknown");
    Assert(unknown_stats.allocation_count == 1, "Memory tracker must count allocations routed into Unknown");

    tracker.ResetStatistics();
    Assert(tracker.GetStatistics(AllocationTag::Core).allocated_bytes == 0, "ResetStatistics must clear usage");
    Assert(tracker.GetStatistics(AllocationTag::Core).peak_allocated_bytes == 0, "ResetStatistics must clear peak usage");
    Assert(tracker.GetStatistics(AllocationTag::Unknown).allocation_count == 0,
           "ResetStatistics must clear Unknown allocation counters");
    Assert(tracker.GetBudget(AllocationTag::Core).has_value() && *tracker.GetBudget(AllocationTag::Core) == 24,
           "ResetStatistics must keep diagnostic budgets intact");

    tracker.SetTrackingEnabled(false);
    tracker.RecordAllocate(AllocationTag::Tests, 128);
    Assert(tracker.GetStatistics(AllocationTag::Tests).allocated_bytes == 0,
           "Disabled tracking mode must ignore allocation accounting");

    tracker.SetTrackingEnabled(true);
    DefaultAllocator default_allocator;
    TrackingAllocator tracking_allocator(tracker, default_allocator);
    void *pointer = tracking_allocator.Allocate(40, alignof(std::max_align_t), AllocationTag::Tests);
    Assert(pointer != nullptr, "Tracking allocator must return allocated memory");
    const auto tests_after_allocate = tracker.GetStatistics(AllocationTag::Tests);
    Assert(tests_after_allocate.allocated_bytes == 40, "Tracking allocator must report allocations to the tracker");
    Assert(tests_after_allocate.peak_allocated_bytes == 40, "Tracking allocator must contribute to peak usage");
    Assert(tests_after_allocate.allocation_count == 1, "Tracking allocator must contribute to allocation counts");

    tracking_allocator.Deallocate(pointer, 40, alignof(std::max_align_t), AllocationTag::Tests);
    Assert(tracker.GetStatistics(AllocationTag::Tests).allocated_bytes == 0,
           "Tracking allocator must report frees to the tracker");

    void *zero_pointer = tracking_allocator.Allocate(0, alignof(std::max_align_t), AllocationTag::Tests);
    Assert(zero_pointer != nullptr, "Tracking allocator must materialize zero-sized allocations through the upstream");
    const auto tests_after_zero_allocate = tracker.GetStatistics(AllocationTag::Tests);
    Assert(tests_after_zero_allocate.allocated_bytes == 1,
           "Tracking allocator must normalize zero-sized allocations to 1 tracked byte");
    Assert(tests_after_zero_allocate.allocation_count == 2,
           "Tracking allocator must count normalized zero-sized allocations");

    tracking_allocator.Deallocate(zero_pointer, 0, alignof(std::max_align_t), AllocationTag::Tests);
    Assert(tracker.GetStatistics(AllocationTag::Tests).allocated_bytes == 0,
           "Tracking allocator must normalize zero-sized frees consistently");
}
void TestDiagnosticsBaseline()
{
    epidemic::diagnostics::GlobalCounters().Reset();
    epidemic::diagnostics::SetCurrentThreadName("DiagnosticsTest");

    RecordingLogger logger;
    logger.Info("Diagnostics", "Logger", "baseline ready");
    Assert(logger.records.size() == 1, "Logger must receive messages");
    Assert(logger.records.front().module_name == "Diagnostics", "Logger must preserve module name");
    Assert(logger.records.front().category == "Logger", "Logger must preserve category");
    Assert(logger.records.front().message == "baseline ready", "Logger must preserve message text");
    Assert(logger.records.front().thread_name == "DiagnosticsTest", "Logger must capture thread name when available");

    auto collector = std::make_shared<epidemic::diagnostics::InMemoryProfileCollector>();
    epidemic::diagnostics::SetProfileCollector(collector);
    epidemic::diagnostics::SetProfilingEnabled(true);
    {
        EPIDEMIC_PROFILE_SCOPE("DiagnosticsScope");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto profile_events = collector->Snapshot();
    Assert(profile_events.size() == 1, "Profiling scope must record an event");
    Assert(profile_events.front().name == "DiagnosticsScope", "Profiling scope must preserve the scope name");
    Assert(profile_events.front().duration.count() > 0, "Profiling scope must record a non-zero duration");

    epidemic::diagnostics::GlobalCounters().Increment(epidemic::diagnostics::CounterId::Frames);
    epidemic::diagnostics::GlobalCounters().Increment(epidemic::diagnostics::CounterId::TasksScheduled, 3);
    epidemic::diagnostics::GlobalCounters().Decrement(epidemic::diagnostics::CounterId::TasksScheduled);
    epidemic::diagnostics::GlobalCounters().Set(epidemic::diagnostics::CounterId::FrameTimeMicros, 16667);
    Assert(epidemic::diagnostics::GlobalCounters().Get(epidemic::diagnostics::CounterId::Frames) == 1,
           "Diagnostics counters must increment correctly");
    Assert(epidemic::diagnostics::GlobalCounters().Get(epidemic::diagnostics::CounterId::TasksScheduled) == 2,
           "Diagnostics counters must support increment and decrement");
    Assert(epidemic::diagnostics::GlobalCounters().Get(epidemic::diagnostics::CounterId::FrameTimeMicros) == 16667,
           "Diagnostics counters must support explicit set/get");

    epidemic::diagnostics::SetProfileCollector(nullptr);
    epidemic::diagnostics::SetProfilingEnabled(false);
    {
        EPIDEMIC_PROFILE_SCOPE("DisabledDiagnosticsScope");
    }
    epidemic::diagnostics::SetProfilingEnabled(true);
}

void TestConfigurationContracts()
{
    epidemic::core::config::BasicConfiguration configuration;
    configuration.SetString("test.string", "value");
    configuration.SetInt("test.int", 7);
    configuration.SetBool("test.bool", true);
    configuration.SetRuntimeName("EpidemicRuntime");
    configuration.SetWorkerCount(3);
    configuration.SetMemoryTrackingEnabled(true);
    configuration.SetRhiDebugEnabled(false);
    configuration.SetDefaultWindowWidth(1920);
    configuration.SetDefaultWindowHeight(1080);

    Assert(configuration.GetString("test.string").has_value() && *configuration.GetString("test.string") == "value",
           "Configuration must support string set/get");
    Assert(configuration.GetInt("test.int").has_value() && *configuration.GetInt("test.int") == 7,
           "Configuration must support integer set/get");
    Assert(configuration.GetBool("test.bool").has_value() && *configuration.GetBool("test.bool"),
           "Configuration must support bool set/get");
    Assert(configuration.GetRuntimeName().has_value() && *configuration.GetRuntimeName() == "EpidemicRuntime",
           "Configuration must expose runtime name");
    Assert(configuration.GetWorkerCount().has_value() && *configuration.GetWorkerCount() == 3,
           "Configuration must expose worker count");
    Assert(configuration.GetMemoryTrackingEnabled().has_value() && *configuration.GetMemoryTrackingEnabled(),
           "Configuration must expose memory tracking flag");
    Assert(configuration.GetRhiDebugEnabled().has_value() && !*configuration.GetRhiDebugEnabled(),
           "Configuration must expose RHI debug flag");
    Assert(configuration.GetDefaultWindowWidth().has_value() && *configuration.GetDefaultWindowWidth() == 1920,
           "Configuration must expose default window width");
    Assert(configuration.GetDefaultWindowHeight().has_value() && *configuration.GetDefaultWindowHeight() == 1080,
           "Configuration must expose default window height");
}

void TestServiceContainerContracts()
{
    epidemic::core::ServiceContainer services;
    auto logger = std::make_shared<RecordingLogger>();
    services.RegisterInstance<ILogger>(logger);
    Assert(services.Contains<ILogger>(), "Logger service should be registered");
    Assert(services.Get<ILogger>() == logger, "Service container must return the same logger instance");

    bool missing_failed = false;
    try
    {
        static_cast<void>(services.Get<epidemic::core::events::IEventBus>());
    }
    catch (const std::exception &)
    {
        missing_failed = true;
    }
    Assert(missing_failed, "Service container must provide a controlled missing-service error path");

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

void TestModuleRegistryContracts()
{
    epidemic::core::ServiceContainer services;
    auto logger = RegisterCoreServices(services);

    epidemic::core::ModuleRegistry registry;
    std::vector<std::string> trace;
    registry.Register(std::make_unique<ProbeModule>("runtime", "Runtime", std::vector<std::string>{"events"}, trace));
    registry.Register(std::make_unique<ProbeModule>("core", "Core", std::vector<std::string>{}, trace));
    registry.Register(std::make_unique<ProbeModule>("events", "Events", std::vector<std::string>{"core"}, trace));

    registry.BootstrapAll(services, *logger);
    registry.InitializeAll(services, *logger);
    registry.TickAll(services, *logger);
    registry.ShutdownAll(services, *logger);

    const std::vector<std::string> expected{
        "core:bootstrap",
        "events:bootstrap",
        "runtime:bootstrap",
        "core:initialize",
        "events:initialize",
        "runtime:initialize",
        "core:tick",
        "events:tick",
        "runtime:tick",
        "runtime:shutdown",
        "events:shutdown",
        "core:shutdown",
    };
    Assert(trace == expected, "Modules must respect lifecycle ordering, ticking, and reverse shutdown order");

    epidemic::core::ModuleRegistry duplicate_registry;
    std::vector<std::string> duplicate_trace;
    duplicate_registry.Register(std::make_unique<ProbeModule>("dup", "One", std::vector<std::string>{}, duplicate_trace));
    bool duplicate_failed = false;
    try
    {
        duplicate_registry.Register(std::make_unique<ProbeModule>("dup", "Two", std::vector<std::string>{}, duplicate_trace));
    }
    catch (const std::exception &)
    {
        duplicate_failed = true;
    }
    Assert(duplicate_failed, "Module registry must reject duplicate module ids");

    epidemic::core::ModuleRegistry missing_dependency_registry;
    std::vector<std::string> missing_trace;
    missing_dependency_registry.Register(
        std::make_unique<ProbeModule>("runtime", "Runtime", std::vector<std::string>{"missing"}, missing_trace));
    bool missing_dependency_failed = false;
    try
    {
        missing_dependency_registry.BootstrapAll(services, *logger);
    }
    catch (const std::exception &)
    {
        missing_dependency_failed = true;
    }
    Assert(missing_dependency_failed, "Module registry must detect missing dependencies");

    epidemic::core::ModuleRegistry circular_registry;
    std::vector<std::string> circular_trace;
    circular_registry.Register(
        std::make_unique<ProbeModule>("first", "First", std::vector<std::string>{"second"}, circular_trace));
    circular_registry.Register(
        std::make_unique<ProbeModule>("second", "Second", std::vector<std::string>{"first"}, circular_trace));
    bool circular_failed = false;
    try
    {
        circular_registry.BootstrapAll(services, *logger);
    }
    catch (const std::exception &)
    {
        circular_failed = true;
    }
    Assert(circular_failed, "Module registry must detect circular dependencies");

    epidemic::core::ModuleRegistry post_start_registry;
    std::vector<std::string> post_start_trace;
    post_start_registry.Register(std::make_unique<ProbeModule>("core", "Core", std::vector<std::string>{}, post_start_trace));
    post_start_registry.BootstrapAll(services, *logger);
    bool post_start_failed = false;
    try
    {
        post_start_registry.Register(
            std::make_unique<ProbeModule>("late", "Late", std::vector<std::string>{}, post_start_trace));
    }
    catch (const std::exception &)
    {
        post_start_failed = true;
    }
    Assert(post_start_failed, "Module registry must reject registration after lifecycle start");
    post_start_registry.ShutdownAll(services, *logger);

    epidemic::core::ModuleRegistry failing_registry;
    std::vector<std::string> failure_trace;
    failing_registry.Register(std::make_unique<ProbeModule>("first", "First", std::vector<std::string>{}, failure_trace));
    failing_registry.Register(
        std::make_unique<ProbeModule>("second", "Second", std::vector<std::string>{"first"}, failure_trace, true));

    bool bootstrap_failed = false;
    try
    {
        failing_registry.BootstrapAll(services, *logger);
    }
    catch (const std::exception &)
    {
        bootstrap_failed = true;
    }
    Assert(bootstrap_failed, "Module registry must surface bootstrap failures");
    failing_registry.ShutdownAll(services, *logger);
    Assert(failure_trace == std::vector<std::string>({"first:bootstrap", "second:bootstrap", "first:shutdown"}),
           "Module registry must only shut down modules that actually bootstrapped");
}

void TestEventBusContracts()
{
    epidemic::core::events::EventBus event_bus;

    int sync_total = 0;
    std::vector<int> queued_values;

    const auto sync_token =
        event_bus.SubscribeSync<SyncTestEvent>([&sync_total](const SyncTestEvent &event) { sync_total += event.value; });
    const auto queued_token = event_bus.SubscribeQueued<QueuedTestEvent>(
        [&queued_values](const QueuedTestEvent &event) { queued_values.push_back(event.value); });

    event_bus.PublishSync(SyncTestEvent{4});
    event_bus.Enqueue(QueuedTestEvent{1});
    event_bus.Enqueue(QueuedTestEvent{2});
    event_bus.Enqueue(QueuedTestEvent{3});

    Assert(sync_total == 4, "Sync event should dispatch immediately");
    Assert(event_bus.DrainQueued() == 3, "All queued events must be drained");
    Assert(queued_values == std::vector<int>({1, 2, 3}), "Queued events must preserve FIFO order");

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
    epidemic::core::tasks::SimpleTaskScheduler scheduler(2);
    Assert(scheduler.WorkerCount() == 2, "Task scheduler must honor worker count configuration");
    const auto worker_names = scheduler.WorkerThreadNames();
    Assert(worker_names.size() == 2, "Task scheduler must expose worker thread names");
    Assert(worker_names[0] != worker_names[1], "Task scheduler worker thread names must be distinct");

    std::atomic<int> counter = 0;
    const auto handle = scheduler.Schedule([&counter] { ++counter; }, "increment");
    scheduler.Wait(handle);
    Assert(counter.load() == 1, "Wait(handle) must observe task completion");

    epidemic::core::tasks::TaskGroup group;
    static_cast<void>(scheduler.Schedule([&counter] { ++counter; }, group, "group-a"));
    static_cast<void>(scheduler.Schedule([&counter] { ++counter; }, group, "group-b"));
    scheduler.Wait(group);
    Assert(counter.load() == 3, "Wait(group) must observe all grouped tasks");

    epidemic::core::tasks::TaskGroup failing_group;
    static_cast<void>(scheduler.Schedule([] { throw std::runtime_error("group failure"); }, failing_group, "group-fail"));
    bool group_exception_failed = false;
    try
    {
        scheduler.Wait(failing_group);
    }
    catch (const std::runtime_error &exception)
    {
        group_exception_failed = std::string(exception.what()) == "group failure";
    }
    Assert(group_exception_failed, "Wait(group) must rethrow task failures");
    try
    {
        scheduler.WaitIdle();
    }
    catch (const std::runtime_error &)
    {
    }

    const auto diagnostics = scheduler.GetDiagnostics();
    Assert(diagnostics.worker_count == 2, "Task diagnostics must report worker count");
    Assert(diagnostics.completed_tasks >= 4, "Task diagnostics must report completed tasks");

    bool null_failed = false;
    try
    {
        static_cast<void>(scheduler.Schedule({}, "invalid"));
    }
    catch (const std::exception &)
    {
        null_failed = true;
    }
    Assert(null_failed, "Task scheduler must reject empty tasks");

    epidemic::core::tasks::SimpleTaskScheduler wait_exception_scheduler(1);
    const auto failing_handle = wait_exception_scheduler.Schedule(
        [] { throw std::runtime_error("task failure"); }, "failing-handle");
    bool wait_exception_failed = false;
    try
    {
        wait_exception_scheduler.Wait(failing_handle);
    }
    catch (const std::runtime_error &exception)
    {
        wait_exception_failed = std::string(exception.what()) == "task failure";
    }
    Assert(wait_exception_failed, "Wait(handle) must rethrow task failures");

    epidemic::core::tasks::SimpleTaskScheduler idle_exception_scheduler(1);
    static_cast<void>(idle_exception_scheduler.Schedule([] { throw std::runtime_error("idle failure"); }, "failing-idle"));
    bool idle_exception_failed = false;
    try
    {
        idle_exception_scheduler.WaitIdle();
    }
    catch (const std::runtime_error &exception)
    {
        idle_exception_failed = std::string(exception.what()) == "idle failure";
    }
    Assert(idle_exception_failed, "WaitIdle must rethrow task failures");
    idle_exception_scheduler.WaitIdle();

    scheduler.Shutdown();
    bool shutdown_failed = false;
    try
    {
        static_cast<void>(scheduler.Schedule([] {}, "after-shutdown"));
    }
    catch (const std::exception &)
    {
        shutdown_failed = true;
    }
    Assert(shutdown_failed, "Task scheduler must reject scheduling after shutdown");

    epidemic::core::tasks::TaskGroup shutdown_group;
    bool shutdown_group_failed = false;
    try
    {
        static_cast<void>(scheduler.Schedule([] {}, shutdown_group, "after-shutdown-group"));
    }
    catch (const std::exception &)
    {
        shutdown_group_failed = true;
    }
    Assert(shutdown_group_failed, "Schedule(task, group) must reject scheduling after shutdown");
    scheduler.Wait(shutdown_group);
}

void TestApplicationLifecycle()
{
    epidemic::core::Application application({"EpidemicApplicationTests"});
    auto logger = RegisterApplicationCoreServices(application, 2, std::nullopt);

    std::vector<std::string> trace;
    application.Modules().Register(
        std::make_unique<ProbeModule>("runtime", "Runtime", std::vector<std::string>{"events"}, trace));
    application.Modules().Register(std::make_unique<ProbeModule>("core", "Core", std::vector<std::string>{}, trace));
    application.Modules().Register(
        std::make_unique<ProbeModule>("events", "Events", std::vector<std::string>{"core"}, trace));

    Assert(application.Bootstrap() == 0, "Application bootstrap must succeed");
    Assert(application.Initialize() == 0, "Application initialize must succeed");
    Assert(application.Tick() == 0, "Application tick must succeed");
    Assert(application.Run() == 0, "Application run must succeed");
    Assert(application.Shutdown() == 0, "Application shutdown must succeed");

    const auto configuration = application.Services().Get<epidemic::core::config::IConfiguration>();
    Assert(configuration->GetRuntimeName().has_value() && *configuration->GetRuntimeName() == "EpidemicApplicationTests",
           "Application must seed runtime name through the external configuration service");
    Assert(application.Services().Contains<ILogger>(), "Application must use externally registered logger service");
    Assert(application.Services().Contains<epidemic::core::config::IConfiguration>(),
           "Application must use externally registered configuration service");
    Assert(application.Services().Contains<epidemic::core::events::IEventBus>(),
           "Application must use externally registered event bus service");
    Assert(application.Services().Contains<epidemic::core::tasks::ITaskScheduler>(),
           "Application must use externally registered task scheduler service");

    const std::vector<std::string> expected_trace{
        "core:bootstrap",
        "events:bootstrap",
        "runtime:bootstrap",
        "core:initialize",
        "events:initialize",
        "runtime:initialize",
        "core:tick",
        "events:tick",
        "runtime:tick",
        "core:tick",
        "events:tick",
        "runtime:tick",
        "runtime:shutdown",
        "events:shutdown",
        "core:shutdown",
    };
    Assert(trace == expected_trace, "Application lifecycle must drive module bootstrap, initialize, tick, run, and shutdown");
    Assert(!logger->entries.empty(), "Application lifecycle must produce diagnostic logs through the registered logger");

    epidemic::core::Application missing_services_application;
    bool missing_services_failed = false;
    try
    {
        missing_services_application.Bootstrap();
    }
    catch (const std::exception &)
    {
        missing_services_failed = true;
    }
    Assert(missing_services_failed, "Application must fail fast when required services are not registered externally");

    epidemic::core::Application initialize_failure_application({"InitializeFailureApplication"});
    RegisterApplicationCoreServices(initialize_failure_application, 1, std::nullopt);
    std::vector<std::string> initialize_failure_trace;
    initialize_failure_application.Modules().Register(
        std::make_unique<ProbeModule>("core", "Core", std::vector<std::string>{}, initialize_failure_trace));
    initialize_failure_application.Modules().Register(std::make_unique<ProbeModule>(
        "runtime", "Runtime", std::vector<std::string>{"core"}, initialize_failure_trace, false, true));
    Assert(initialize_failure_application.Bootstrap() == 0, "Application bootstrap before initialize failure must succeed");
    bool initialize_failed = false;
    try
    {
        initialize_failure_application.Initialize();
    }
    catch (const std::exception &)
    {
        initialize_failed = true;
    }
    Assert(initialize_failed, "Application must surface initialize failures");
    Assert(initialize_failure_application.Shutdown() == 0, "Application shutdown after initialize failure must succeed");

    epidemic::core::Application tick_failure_application({"TickFailureApplication"});
    RegisterApplicationCoreServices(tick_failure_application, 1, std::nullopt);
    std::vector<std::string> tick_failure_trace;
    tick_failure_application.Modules().Register(
        std::make_unique<ProbeModule>("core", "Core", std::vector<std::string>{}, tick_failure_trace));
    tick_failure_application.Modules().Register(std::make_unique<ProbeModule>(
        "runtime", "Runtime", std::vector<std::string>{"core"}, tick_failure_trace, false, false, true));
    Assert(tick_failure_application.Bootstrap() == 0, "Application bootstrap before tick failure must succeed");
    Assert(tick_failure_application.Initialize() == 0, "Application initialize before tick failure must succeed");
    bool tick_failed = false;
    try
    {
        tick_failure_application.Tick();
    }
    catch (const std::exception &)
    {
        tick_failed = true;
    }
    Assert(tick_failed, "Application must surface tick failures");
    Assert(tick_failure_application.Shutdown() == 0, "Application shutdown after tick failure must succeed");
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
        {"MemoryBaseline", &TestMemoryBaseline},
        {"DiagnosticsBaseline", &TestDiagnosticsBaseline},
        {"ConfigurationContracts", &TestConfigurationContracts},
        {"ServiceContainerContracts", &TestServiceContainerContracts},
        {"ModuleRegistryContracts", &TestModuleRegistryContracts},
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















