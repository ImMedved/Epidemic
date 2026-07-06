#pragma once

#include "test_assert.h"

#include <Epidemic/Core/application.h>
#include <Epidemic/Core/basic_configuration.h>
#include <Epidemic/Core/configuration.h>
#include <Epidemic/Core/event_bus.h>
#include <Epidemic/Core/imodule.h>
#include <Epidemic/Core/main_thread_dispatcher.h>
#include <Epidemic/Core/module_registry.h>
#include <Epidemic/Core/service_container.h>
#include <Epidemic/Core/task_scheduler.h>
#include <Epidemic/Diagnostics/logger.h>
#include <Epidemic/Memory/imemory_tracker.h>
#include <Epidemic/Memory/memory_tracker.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace epidemic::tests
{
class RecordingLogger final : public epidemic::diagnostics::ILogger
{
  public:
    struct Entry
    {
        epidemic::diagnostics::LogLevel level{};
        std::string module_name;
        std::string category;
        std::string message;
    };

    void Log(const epidemic::diagnostics::LogMessage &message) override
    {
        records.push_back(
            Entry{message.level, std::string(message.module_name), std::string(message.category), std::string(message.message)});
    }

    std::vector<Entry> records;
};

class ProbeModule final : public epidemic::core::IModule
{
  public:
    ProbeModule(std::string id, std::vector<std::string> dependencies, std::vector<std::string> &trace,
                bool throw_on_bootstrap = false, bool throw_on_initialize = false, bool throw_on_tick = false)
        : manifest_{std::move(id), "ProbeModule", std::move(dependencies)},
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

    void Tick(epidemic::core::ServiceContainer &, const epidemic::core::FrameContext &) override
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

inline std::shared_ptr<RecordingLogger> RegisterCoreServices(epidemic::core::ServiceContainer &services,
                                                             std::size_t worker_count = 1,
                                                             std::optional<std::string> runtime_name = std::nullopt)
{
    auto logger = std::make_shared<RecordingLogger>();
    services.RegisterInstance<epidemic::diagnostics::ILogger>(logger);

    const auto configuration =
        services.Emplace<epidemic::core::config::IConfiguration, epidemic::core::config::BasicConfiguration>();
    configuration->SetRuntimeName(runtime_name.value_or("Tests"));
    configuration->SetWorkerCount(worker_count);
    configuration->SetMemoryTrackingEnabled(true);
    configuration->SetRhiDebugEnabled(false);
    configuration->SetDefaultWindowWidth(1280);
    configuration->SetDefaultWindowHeight(720);

    services.Emplace<epidemic::core::events::IEventBus, epidemic::core::events::EventBus>();
    services.Emplace<epidemic::core::tasks::ITaskScheduler, epidemic::core::tasks::SimpleTaskScheduler>(worker_count);
    services.Emplace<epidemic::core::IMainThreadDispatcher, epidemic::core::MainThreadDispatcher>();
    services.Emplace<epidemic::memory::IMemoryTracker, epidemic::memory::MemoryTracker>(true);
    return logger;
}

inline std::shared_ptr<RecordingLogger> RegisterApplicationCoreServices(epidemic::core::Application &application,
                                                                        std::size_t worker_count = 1,
                                                                        std::optional<std::string> runtime_name = std::nullopt)
{
    return RegisterCoreServices(application.Services(), worker_count, std::move(runtime_name));
}
} // namespace epidemic::tests