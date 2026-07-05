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
#include <Epidemic/Input/input_system.h>
#include <Epidemic/Input/key_code.h>
#include <Epidemic/Input/mouse_button.h>
#include <Epidemic/Memory/allocation_tag.h>
#include <Epidemic/Memory/allocator.h>
#include <Epidemic/Memory/memory_tracker.h>
#include <Epidemic/Platform/iplatform_runtime.h>
#include <Epidemic/Platform/iwindow_system.h>
#include <Epidemic/Platform/platform_event.h>
#include <Epidemic/Platform/windows_platform_runtime.h>
#include <Epidemic/RHI/descriptors.h>
#include <Epidemic/RHI/null_rhi_device.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#ifdef CreateWindow
#undef CreateWindow
#endif

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

namespace epidemic::tests
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
                bool throw_on_bootstrap = false, bool throw_on_initialize = false, bool throw_on_tick = false,
                std::vector<epidemic::core::FrameContext> *frame_contexts = nullptr)
        : manifest_{std::move(id), std::move(name), std::move(dependencies)},
          trace_(trace),
          throw_on_bootstrap_(throw_on_bootstrap),
          throw_on_initialize_(throw_on_initialize),
          throw_on_tick_(throw_on_tick),
          frame_contexts_(frame_contexts)
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

    void Tick(epidemic::core::ServiceContainer &, const epidemic::core::FrameContext &frame_context) override
    {
        trace_.push_back(manifest_.id + ":tick");
        if (frame_contexts_ != nullptr)
        {
            frame_contexts_->push_back(frame_context);
        }
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
    std::vector<epidemic::core::FrameContext> *frame_contexts_{nullptr};
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
    registry.TickAll(services, *logger, epidemic::core::FrameContext{});
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

void TestFrameLoopContracts()
{
    epidemic::diagnostics::GlobalCounters().Reset();

    epidemic::core::Application frame_limit_application({"FrameLoopTests"});
    RegisterApplicationCoreServices(frame_limit_application, 1, std::nullopt);

    std::vector<std::string> frame_limit_trace;
    std::vector<epidemic::core::FrameContext> module_frame_contexts;
    frame_limit_application.Modules().Register(std::make_unique<ProbeModule>(
        "core", "Core", std::vector<std::string>{}, frame_limit_trace, false, false, false, &module_frame_contexts));

    std::vector<std::string> phase_trace;
    std::vector<std::string> execution_trace;
    std::vector<epidemic::core::FrameContext> begin_frame_contexts;

    for (const auto phase : epidemic::core::FramePhaseOrder())
    {
        frame_limit_application.AddFramePhaseHandler(
            phase,
            [&frame_limit_application, &phase_trace, &execution_trace, &begin_frame_contexts, phase](
                const epidemic::core::FrameContext &frame_context) {
                phase_trace.push_back(std::string(epidemic::core::ToString(phase)) + ":" +
                                      std::to_string(frame_context.frame_index.Value()));
                if (phase == epidemic::core::FramePhase::BeginFrame)
                {
                    begin_frame_contexts.push_back(frame_context);
                    if (frame_context.frame_index.Value() == 1)
                    {
                        frame_limit_application.ScheduleMainThreadTask(
                            [&execution_trace] { execution_trace.push_back("task:main-thread-1"); }, "main-thread-1");
                    }
                }
                if (phase == epidemic::core::FramePhase::RunScheduledMainThreadTasks)
                {
                    execution_trace.push_back("phase:RunScheduledMainThreadTasks:" +
                                              std::to_string(frame_context.frame_index.Value()));
                }
                if (phase == epidemic::core::FramePhase::TickModules)
                {
                    execution_trace.push_back("phase:TickModules:" + std::to_string(frame_context.frame_index.Value()));
                }
                if (phase == epidemic::core::FramePhase::Present && frame_context.frame_index.Value() == 0)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            },
            std::string(epidemic::core::ToString(phase)));
    }

    frame_limit_application.ScheduleMainThreadTask(
        [&execution_trace] { execution_trace.push_back("task:main-thread-0"); }, "main-thread-0");
    frame_limit_application.SetFrameLimit(2);

    Assert(frame_limit_application.Bootstrap() == 0, "Frame-loop bootstrap must succeed");
    Assert(frame_limit_application.Initialize() == 0, "Frame-loop initialize must succeed");
    Assert(frame_limit_application.Run() == 0, "Frame-loop run must succeed");
    Assert(frame_limit_application.Shutdown() == 0, "Frame-loop shutdown must succeed");

    Assert(begin_frame_contexts.size() == 2, "Frame limit must stop the run loop after two frames");
    Assert(begin_frame_contexts[0].frame_index.Value() == 0, "First frame index must start at zero");
    Assert(begin_frame_contexts[1].frame_index.Value() == 1, "Frame index must increment each frame");
    Assert(begin_frame_contexts[0].delta_time.IsZero(), "First frame delta must be zero-initialized");
    Assert(begin_frame_contexts[1].raw_delta_time.Raw() > epidemic::foundation::Duration::zero(),
           "Second frame raw delta must be positive");
    Assert(begin_frame_contexts[1].delta_time == begin_frame_contexts[1].raw_delta_time,
           "Baseline frame delta must match raw delta before smoothing exists");
    Assert(begin_frame_contexts[1].absolute_time.Raw() > begin_frame_contexts[0].absolute_time.Raw(),
           "Absolute frame time must increase across frames");

    std::vector<std::string> expected_phase_trace;
    for (std::uint64_t frame_index = 0; frame_index < 2; ++frame_index)
    {
        for (const auto phase : epidemic::core::FramePhaseOrder())
        {
            expected_phase_trace.push_back(std::string(epidemic::core::ToString(phase)) + ":" +
                                           std::to_string(frame_index));
        }
    }
    Assert(phase_trace == expected_phase_trace, "Frame phases must execute in the fixed order for each frame");

    const std::vector<std::string> expected_execution_trace{
        "phase:RunScheduledMainThreadTasks:0",
        "task:main-thread-0",
        "phase:TickModules:0",
        "phase:RunScheduledMainThreadTasks:1",
        "task:main-thread-1",
        "phase:TickModules:1",
    };
    Assert(execution_trace == expected_execution_trace,
           "Main-thread tasks must run inside the dedicated frame phase before module ticks");

    const std::vector<std::string> expected_frame_limit_trace{
        "core:bootstrap",
        "core:initialize",
        "core:tick",
        "core:tick",
        "core:shutdown",
    };
    Assert(frame_limit_trace == expected_frame_limit_trace,
           "Modules must tick through the frame loop and shut down in the expected order");
    Assert(module_frame_contexts.size() == 2, "Modules must receive a frame context for each frame-loop tick");
    Assert(module_frame_contexts[0].frame_index.Value() == 0 && module_frame_contexts[1].frame_index.Value() == 1,
           "Module frame contexts must preserve the running frame index");
    Assert(frame_limit_application.CurrentFrameContext().frame_index.Value() == 1,
           "Application must retain the most recent frame context");
    Assert(epidemic::diagnostics::GlobalCounters().Get(epidemic::diagnostics::CounterId::Frames) == 2,
           "Frame loop must increment the global frame counter once per frame");
    Assert(epidemic::diagnostics::GlobalCounters().Get(epidemic::diagnostics::CounterId::CurrentFrameIndex) == 1,
           "Frame loop must expose the current frame index through diagnostics counters");
    Assert(epidemic::diagnostics::GlobalCounters().Get(epidemic::diagnostics::CounterId::MainThreadTasksExecuted) == 1,
           "Frame loop must expose per-frame main-thread task execution counts");

    epidemic::core::Application stop_application({"StopRequestTests"});
    RegisterApplicationCoreServices(stop_application, 1, std::nullopt);

    std::vector<std::string> stop_trace;
    std::vector<epidemic::core::FrameContext> stop_frame_contexts;
    stop_application.Modules().Register(std::make_unique<ProbeModule>(
        "core", "Core", std::vector<std::string>{}, stop_trace, false, false, false, &stop_frame_contexts));
    stop_application.AddFramePhaseHandler(
        epidemic::core::FramePhase::EndFrame,
        [&stop_application](const epidemic::core::FrameContext &) { stop_application.RequestStop(); }, "stop-request");

    Assert(stop_application.Bootstrap() == 0, "Stop-request bootstrap must succeed");
    Assert(stop_application.Initialize() == 0, "Stop-request initialize must succeed");
    Assert(stop_application.Run() == 0, "Stop-request run must succeed");
    Assert(stop_application.Shutdown() == 0, "Stop-request shutdown must succeed");
    Assert(stop_application.StopRequested(), "Application must preserve the stop-request flag after exiting the run loop");
    Assert(stop_frame_contexts.size() == 1, "Stop request must exit the frame loop after the first frame");
}


void TestInputContracts()
{
    epidemic::input::InputSystem input_system;

    epidemic::platform::PlatformEvent key_down_event;
    key_down_event.type = epidemic::platform::PlatformEventType::KeyPressed;
    key_down_event.key_code = static_cast<std::uint32_t>(epidemic::input::KeyCode::A);
    input_system.QueuePlatformEvent(key_down_event);
    input_system.PublishSnapshot();

    const auto press_snapshot = input_system.CurrentSnapshot();
    Assert(press_snapshot.keyboard.IsKeyDown(epidemic::input::KeyCode::A),
           "Input snapshot must mark key-down state after a press event");
    Assert(press_snapshot.keyboard.WasPressedThisFrame(epidemic::input::KeyCode::A),
           "Input snapshot must expose pressed-this-frame transitions");
    Assert(!press_snapshot.keyboard.WasReleasedThisFrame(epidemic::input::KeyCode::A),
           "Input snapshot must not mark release transitions during a key press");
    Assert(input_system.CurrentEvents().size() == 1 &&
               input_system.CurrentEvents().front().type == epidemic::input::InputEventType::KeyPressed,
           "Input system must publish frame-local key press events");

    input_system.PublishSnapshot();
    const auto stable_snapshot = input_system.CurrentSnapshot();
    Assert(stable_snapshot.keyboard.IsKeyDown(epidemic::input::KeyCode::A),
           "Input snapshot must keep persistent down state when no new events arrive");
    Assert(!stable_snapshot.keyboard.WasPressedThisFrame(epidemic::input::KeyCode::A),
           "Pressed-this-frame must clear on the next snapshot");
    Assert(stable_snapshot.update_index == press_snapshot.update_index + 1,
           "Each snapshot publication must advance the snapshot sequence");

    epidemic::platform::PlatformEvent invalid_key_event;
    invalid_key_event.type = epidemic::platform::PlatformEventType::KeyPressed;
    invalid_key_event.key_code = 1;
    input_system.QueuePlatformEvent(invalid_key_event);
    input_system.PublishSnapshot();
    Assert(input_system.CurrentEvents().empty(), "Unknown key codes must be ignored");
    Assert(!input_system.CurrentSnapshot().keyboard.IsKeyDown(static_cast<epidemic::input::KeyCode>(1)),
           "Unknown key codes must not pollute keyboard state");

    epidemic::platform::PlatformEvent key_up_event;
    key_up_event.type = epidemic::platform::PlatformEventType::KeyReleased;
    key_up_event.key_code = static_cast<std::uint32_t>(epidemic::input::KeyCode::A);
    input_system.QueuePlatformEvent(key_up_event);
    input_system.PublishSnapshot();

    const auto release_snapshot = input_system.CurrentSnapshot();
    Assert(!release_snapshot.keyboard.IsKeyDown(epidemic::input::KeyCode::A),
           "Input snapshot must clear key-down state after release");
    Assert(release_snapshot.keyboard.WasReleasedThisFrame(epidemic::input::KeyCode::A),
           "Input snapshot must expose released-this-frame transitions");

    epidemic::platform::PlatformEvent mouse_move_event;
    mouse_move_event.type = epidemic::platform::PlatformEventType::MouseMoved;
    mouse_move_event.mouse_x = 320;
    mouse_move_event.mouse_y = 240;
    input_system.QueuePlatformEvent(mouse_move_event);
    input_system.PublishSnapshot();

    const auto mouse_snapshot = input_system.CurrentSnapshot();
    Assert(mouse_snapshot.mouse.PositionX() == 320 && mouse_snapshot.mouse.PositionY() == 240,
           "Input snapshot must preserve mouse position");
    Assert(mouse_snapshot.mouse.DeltaX() == 320 && mouse_snapshot.mouse.DeltaY() == 240,
           "Input snapshot must expose mouse deltas within the frame they were produced");

    epidemic::platform::PlatformEvent invalid_mouse_button_event;
    invalid_mouse_button_event.type = epidemic::platform::PlatformEventType::MouseButtonPressed;
    invalid_mouse_button_event.mouse_button = 99;
    input_system.QueuePlatformEvent(invalid_mouse_button_event);
    input_system.PublishSnapshot();
    Assert(input_system.CurrentEvents().empty(), "Unknown mouse buttons must be ignored");
    Assert(!input_system.CurrentSnapshot().mouse.IsButtonDown(epidemic::input::MouseButton::Left),
           "Unknown mouse buttons must not alias to Left");

    epidemic::platform::PlatformEvent mouse_press_event;
    mouse_press_event.type = epidemic::platform::PlatformEventType::MouseButtonPressed;
    mouse_press_event.mouse_button = static_cast<std::uint8_t>(epidemic::input::MouseButton::Left);
    mouse_press_event.mouse_x = 320;
    mouse_press_event.mouse_y = 240;
    input_system.QueuePlatformEvent(mouse_press_event);

    epidemic::platform::PlatformEvent capture_event;
    capture_event.type = epidemic::platform::PlatformEventType::MouseCaptureChanged;
    capture_event.captured = true;
    input_system.QueuePlatformEvent(capture_event);
    input_system.PublishSnapshot();

    const auto button_snapshot = input_system.CurrentSnapshot();
    Assert(button_snapshot.mouse.IsButtonDown(epidemic::input::MouseButton::Left),
           "Input snapshot must track pressed mouse buttons");
    Assert(button_snapshot.mouse.WasPressedThisFrame(epidemic::input::MouseButton::Left),
           "Input snapshot must expose mouse pressed-this-frame transitions");
    Assert(button_snapshot.HasCapture(), "Input snapshot must track mouse capture state");

    epidemic::platform::PlatformEvent wheel_event;
    wheel_event.type = epidemic::platform::PlatformEventType::MouseWheel;
    wheel_event.wheel_delta = 120;
    input_system.QueuePlatformEvent(wheel_event);
    input_system.PublishSnapshot();
    Assert(input_system.CurrentSnapshot().mouse.WheelDelta() == 120,
           "Input snapshot must accumulate per-frame mouse wheel deltas");

    epidemic::platform::PlatformEvent focus_lost_event;
    focus_lost_event.type = epidemic::platform::PlatformEventType::WindowFocusChanged;
    focus_lost_event.focused = false;
    input_system.QueuePlatformEvent(focus_lost_event);
    input_system.PublishSnapshot();

    const auto focus_lost_snapshot = input_system.CurrentSnapshot();
    Assert(!focus_lost_snapshot.HasFocus(), "Input snapshot must reflect focus loss");
    Assert(!focus_lost_snapshot.keyboard.IsKeyDown(epidemic::input::KeyCode::A),
           "Focus loss must clear pressed keyboard state");
    Assert(!focus_lost_snapshot.mouse.IsButtonDown(epidemic::input::MouseButton::Left),
           "Focus loss must clear pressed mouse button state");
    Assert(!focus_lost_snapshot.keyboard.WasPressedThisFrame(epidemic::input::KeyCode::A) &&
               !focus_lost_snapshot.keyboard.WasReleasedThisFrame(epidemic::input::KeyCode::A),
           "Focus loss must clear keyboard transient state");
    Assert(focus_lost_snapshot.mouse.DeltaX() == 0 && focus_lost_snapshot.mouse.DeltaY() == 0 &&
               focus_lost_snapshot.mouse.WheelDelta() == 0,
           "Focus loss must clear transient mouse movement and wheel state");

    epidemic::platform::PlatformEvent pending_move_event;
    pending_move_event.type = epidemic::platform::PlatformEventType::MouseMoved;
    pending_move_event.mouse_x = 400;
    pending_move_event.mouse_y = 260;
    input_system.QueuePlatformEvent(pending_move_event);

    const auto snapshot_before_publish = input_system.CurrentSnapshot();
    Assert(snapshot_before_publish.mouse.PositionX() == focus_lost_snapshot.mouse.PositionX() &&
               snapshot_before_publish.mouse.PositionY() == focus_lost_snapshot.mouse.PositionY(),
           "Queued platform events must not mutate the published snapshot before PublishSnapshot");

    input_system.PublishSnapshot();
    const auto snapshot_after_publish = input_system.CurrentSnapshot();
    Assert(snapshot_after_publish.mouse.PositionX() == 400 && snapshot_after_publish.mouse.PositionY() == 260,
           "Queued platform events must become visible only after PublishSnapshot");
}

void TestRhiContracts()
{
    const epidemic::rhi::RhiDeviceDesc invalid_device_desc{false, {}};
    const auto invalid_device_result = epidemic::rhi::CreateNullRhiDevice(invalid_device_desc);
    Assert(!invalid_device_result.HasValue(), "RHI device creation must reject empty device names");
    Assert(invalid_device_result.GetError().HasCode("rhi.empty_device_name"),
           "RHI device validation must return a meaningful error code");

    const auto device_result = epidemic::rhi::CreateNullRhiDevice(epidemic::rhi::RhiDeviceDesc{true, "UnitTestNullRHI"});
    Assert(device_result.HasValue(), "Null RHI device creation must succeed for valid descriptors");
    const auto device = device_result.Value();
    Assert(device->BackendName() == "NullRHI", "Null RHI device must report its backend name");
    Assert(device->Descriptor().enable_debug_validation,
           "RHI device descriptor must preserve debug-validation settings");

    const auto command_context_result = device->CreateCommandContext();
    Assert(command_context_result.HasValue(), "RHI device must create a command context");
    const auto command_context = command_context_result.Value();
    Assert(!command_context->IsFrameActive(), "Fresh RHI command context must start without an active frame");
    Assert(!command_context->Clear(epidemic::rhi::RhiClearDesc{}).HasValue(),
           "RHI command context must reject Clear outside an active frame");
    Assert(command_context->BeginFrame().HasValue(), "RHI command context must begin a frame");
    Assert(command_context->IsFrameActive(), "BeginFrame must activate the RHI command context");
    Assert(command_context->BeginFrame().GetError().HasCode("rhi.frame_already_active"),
           "RHI command context must reject nested BeginFrame calls");

    epidemic::rhi::RhiClearDesc clear_desc;
    clear_desc.color = epidemic::rhi::RhiColor{0.1f, 0.2f, 0.3f, 1.0f};
    Assert(command_context->Clear(clear_desc).HasValue(), "RHI command context must accept valid clear operations");
    Assert(command_context->EndFrame().HasValue(), "RHI command context must end an active frame");
    Assert(!command_context->IsFrameActive(), "EndFrame must deactivate the RHI command context");
    Assert(command_context->EndFrame().GetError().HasCode("rhi.no_active_frame"),
           "RHI command context must reject EndFrame when no frame is active");

    epidemic::rhi::RhiClearDesc invalid_clear_desc;
    invalid_clear_desc.clear_color = false;
    const auto invalid_clear_result = epidemic::rhi::Validate(invalid_clear_desc);
    Assert(!invalid_clear_result.HasValue(), "RHI clear descriptor validation must reject no-op clears");
    Assert(invalid_clear_result.GetError().HasCode("rhi.nothing_to_clear"),
           "RHI clear descriptor validation must return a meaningful error code");

    epidemic::rhi::RhiSwapChainDesc invalid_swap_chain_desc;
    invalid_swap_chain_desc.width = 1280;
    invalid_swap_chain_desc.height = 720;
    invalid_swap_chain_desc.buffer_count = 2;
    invalid_swap_chain_desc.color_format = epidemic::rhi::RhiPixelFormat::B8G8R8A8_UNorm;
    const auto invalid_swap_chain_result = device->CreateSwapChain(invalid_swap_chain_desc);
    Assert(!invalid_swap_chain_result.HasValue(), "RHI swap chain creation must reject missing surface handles");
    Assert(invalid_swap_chain_result.GetError().HasCode("rhi.invalid_surface_handle"),
           "RHI swap chain validation must return a meaningful error code");

    epidemic::rhi::RhiSwapChainDesc swap_chain_desc;
    swap_chain_desc.surface_handle = epidemic::rhi::PresentationSurfaceHandle(reinterpret_cast<void *>(1));
    swap_chain_desc.width = 1280;
    swap_chain_desc.height = 720;
    swap_chain_desc.buffer_count = 2;
    swap_chain_desc.color_format = epidemic::rhi::RhiPixelFormat::B8G8R8A8_UNorm;
    const auto swap_chain_result = device->CreateSwapChain(swap_chain_desc);
    Assert(swap_chain_result.HasValue(), "RHI device must create a swap chain for valid descriptors");
    const auto swap_chain = swap_chain_result.Value();
    Assert(swap_chain->Width() == 1280 && swap_chain->Height() == 720,
           "RHI swap chain must preserve initial dimensions");
    Assert(swap_chain->BufferCount() == 2, "RHI swap chain must preserve initial buffer count");
    Assert(swap_chain->ColorFormat() == epidemic::rhi::RhiPixelFormat::B8G8R8A8_UNorm,
           "RHI swap chain must preserve initial color format");
    Assert(swap_chain->Present().HasValue(), "RHI swap chain must present successfully in the null backend");
    Assert(swap_chain->Resize(1920, 1080).HasValue(), "RHI swap chain must resize successfully");
    Assert(swap_chain->Width() == 1920 && swap_chain->Height() == 1080,
           "RHI swap chain resize must update reported dimensions");
    Assert(swap_chain->Resize(0, 1080).GetError().HasCode("rhi.invalid_swap_chain_size"),
           "RHI swap chain resize must reject zero dimensions");

    const auto invalid_buffer_result = device->CreateBuffer(epidemic::rhi::RhiBufferDesc{});
    Assert(!invalid_buffer_result.HasValue(), "RHI buffer creation must reject zero-sized buffers");
    Assert(invalid_buffer_result.GetError().HasCode("rhi.invalid_buffer_size"),
           "RHI buffer validation must return a meaningful error code");

    epidemic::rhi::RhiBufferDesc buffer_desc;
    buffer_desc.size_bytes = 256;
    buffer_desc.debug_name = "VertexBuffer";
    const auto buffer_result = device->CreateBuffer(buffer_desc);
    Assert(buffer_result.HasValue(), "RHI buffer creation must succeed for valid descriptors");
    Assert(buffer_result.Value()->SizeBytes() == 256, "RHI buffer must preserve its size");
    Assert(buffer_result.Value()->DebugName() == "VertexBuffer", "RHI buffer must preserve its debug name");

    epidemic::rhi::RhiTextureDesc invalid_texture_desc;
    invalid_texture_desc.width = 64;
    invalid_texture_desc.height = 64;
    const auto invalid_texture_result = device->CreateTexture(invalid_texture_desc);
    Assert(!invalid_texture_result.HasValue(), "RHI texture creation must reject unknown formats");
    Assert(invalid_texture_result.GetError().HasCode("rhi.invalid_texture_format"),
           "RHI texture validation must return a meaningful error code");

    epidemic::rhi::RhiTextureDesc texture_desc;
    texture_desc.width = 64;
    texture_desc.height = 32;
    texture_desc.format = epidemic::rhi::RhiPixelFormat::R8G8B8A8_UNorm;
    texture_desc.debug_name = "Albedo";
    const auto texture_result = device->CreateTexture(texture_desc);
    Assert(texture_result.HasValue(), "RHI texture creation must succeed for valid descriptors");
    Assert(texture_result.Value()->Width() == 64 && texture_result.Value()->Height() == 32,
           "RHI texture must preserve its dimensions");
    Assert(texture_result.Value()->Format() == epidemic::rhi::RhiPixelFormat::R8G8B8A8_UNorm,
           "RHI texture must preserve its format");
    Assert(texture_result.Value()->DebugName() == "Albedo", "RHI texture must preserve its debug name");
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
    application.SetFrameLimit(2);
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

void TestApplicationShutdownContracts()
{
    epidemic::core::Application application({"ShutdownContractsApplication"});
    auto logger = RegisterApplicationCoreServices(application, 1, std::nullopt);

    Assert(application.Bootstrap() == 0, "Shutdown-contract bootstrap must succeed");
    Assert(application.Initialize() == 0, "Shutdown-contract initialize must succeed");

    const auto scheduler = application.Services().Get<epidemic::core::tasks::ITaskScheduler>();
    static_cast<void>(scheduler->Schedule([] { throw std::runtime_error("shutdown task failure"); },
                                          "shutdown-task-failure"));

    Assert(application.Shutdown() == 0,
           "Application shutdown must finish even when the scheduler captured a task exception");

    bool scheduler_error_logged = false;
    for (const auto &record : logger->records)
    {
        if (record.level == epidemic::diagnostics::LogLevel::Error &&
            record.category == "Shutdown" &&
            record.message.find("shutdown task failure") != std::string::npos)
        {
            scheduler_error_logged = true;
            break;
        }
    }

    Assert(scheduler_error_logged,
           "Application shutdown must log the scheduler exception instead of throwing it");
}
void TestPlatformRuntime()
{
    auto platform_runtime = std::make_shared<epidemic::platform::WindowsPlatformRuntime>();
    Assert(platform_runtime->Name() == "WindowsPlatformRuntime", "Platform runtime name must match implementation");
    Assert(!platform_runtime->GetProcessInfo().working_directory.Empty(), "Platform runtime must expose working directory");
    Assert(!platform_runtime->GetProcessInfo().executable_path.Empty(), "Platform runtime must expose executable path");
    Assert(!platform_runtime->GetProcessInfo().arguments.empty(), "Platform runtime must expose arguments");

    const auto start = platform_runtime->Now();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto finish = platform_runtime->Now();
    Assert(finish >= start, "Platform clock must be monotonic");

    epidemic::core::ServiceContainer services;
    services.RegisterInstance<epidemic::platform::IPlatformRuntime>(platform_runtime);
    services.RegisterInstance<epidemic::platform::IWindowSystem>(platform_runtime);
    Assert(services.Contains<epidemic::platform::IPlatformRuntime>(), "Platform runtime service registration must work");
    Assert(services.Contains<epidemic::platform::IWindowSystem>(), "Window system service registration must work");

    const auto library_result = platform_runtime->LoadDynamicLibrary(epidemic::foundation::Path::FromString("kernel32.dll"));
    Assert(library_result.HasValue(), "Platform runtime must load kernel32.dll");
    const auto symbol_result = library_result.Value()->FindSymbol("GetTickCount64");
    Assert(symbol_result.HasValue(), "Platform runtime must resolve GetTickCount64");

    const auto missing_symbol_result = library_result.Value()->FindSymbol("DefinitelyMissingSymbolForPlatformTests");
    Assert(!missing_symbol_result.HasValue(), "Missing symbols must produce a failure result");
    Assert(missing_symbol_result.GetError().HasCode("platform.symbol_not_found"),
           "Missing symbols must produce a meaningful error code");

    const auto empty_path_result = platform_runtime->LoadDynamicLibrary(epidemic::foundation::Path{});
    Assert(!empty_path_result.HasValue(), "Empty dynamic library path must fail");

    const auto missing_library_result =
        platform_runtime->LoadDynamicLibrary(epidemic::foundation::Path::FromString("epidemic_missing_library_for_tests.dll"));
    Assert(!missing_library_result.HasValue(), "Missing dynamic libraries must produce a failure result");
    Assert(missing_library_result.GetError().HasCode("platform.load_library_failed"),
           "Missing dynamic libraries must produce a meaningful error code");

    const auto window_result = services.Get<epidemic::platform::IWindowSystem>()->CreateWindow(
        epidemic::platform::WindowCreateInfo{"Platform Hidden Test", 320, 240, false});
    Assert(window_result.HasValue(), "Window system must be able to create a hidden Win32 window");
    const auto window = window_result.Value();
    Assert(window->GetNativeHandle().IsValid(), "Created window must expose a valid native handle");
    Assert(window->ClientWidth() > 0, "Created window must expose a positive client width");
    Assert(window->ClientHeight() > 0, "Created window must expose a positive client height");
    Assert(window->Dpi() >= 96, "Created window must expose a valid DPI value");
    Assert(services.Get<epidemic::platform::IWindowSystem>()->WindowCount() == 1,
           "Window system must track created windows");

    platform_runtime->PumpEvents();
    static_cast<void>(services.Get<epidemic::platform::IWindowSystem>()->DrainEvents());

    SendMessageW(window->GetNativeHandle().As<HWND>(), WM_CLOSE, 0, 0);
    platform_runtime->PumpEvents();

    bool close_event_seen = false;
    for (const auto &event : services.Get<epidemic::platform::IWindowSystem>()->DrainEvents())
    {
        if (event.type == epidemic::platform::PlatformEventType::WindowCloseRequested && event.window_id == window->Id())
        {
            close_event_seen = true;
        }
    }

    Assert(close_event_seen, "WM_CLOSE must emit a close-requested platform event");
    Assert(window->IsCloseRequested(), "WM_CLOSE must mark the window as close-requested");
    Assert(services.Get<epidemic::platform::IWindowSystem>()->WindowCount() == 1,
           "WM_CLOSE must not destroy the window until the app accepts the request");
    Assert(!platform_runtime->IsExitRequested(),
           "A close request alone must not mark the runtime as exiting before the window is destroyed");

    window->Close();
    platform_runtime->PumpEvents();
    platform_runtime->PumpEvents();

    Assert(platform_runtime->IsExitRequested(), "Destroying the last window must request platform exit");
    Assert(services.Get<epidemic::platform::IWindowSystem>()->WindowCount() == 0,
           "Destroyed windows must be removed from the window system");
}
} // namespace epidemic::tests







