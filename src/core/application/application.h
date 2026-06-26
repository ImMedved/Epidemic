#pragma once

#include "core/config/configuration.h"
#include "core/diagnostics/logger.h"
#include "core/events/event_bus.h"
#include "core/modules/module_registry.h"
#include "core/services/service_container.h"
#include "core/tasks/task_scheduler.h"

#include <cstddef>
#include <memory>
#include <string>

namespace epidemic::core
{
struct ApplicationOptions
{
    std::string application_name = "Epidemic Engine v1.0";
    std::size_t worker_count = 1;
};

class Application
{
  public:
    explicit Application(ApplicationOptions options = {});
    ~Application();

    [[nodiscard]] ModuleRegistry &Modules() noexcept;
    [[nodiscard]] ServiceContainer &Services() noexcept;

    int Bootstrap();
    int Initialize();
    int Run();
    int Shutdown();

  private:
    enum class State
    {
        Constructed,
        Bootstrapped,
        Initialized,
        Running,
        ShutDown,
    };

    void RegisterCoreServices();
    [[nodiscard]] std::shared_ptr<diagnostics::ILogger> Logger() const;

    ApplicationOptions options_;
    ServiceContainer services_;
    ModuleRegistry modules_;
    State state_{State::Constructed};
    bool services_registered_{false};
};
} // namespace epidemic::core
