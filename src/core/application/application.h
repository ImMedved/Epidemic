#pragma once

#include "core/modules/module_registry.h"
#include "core/services/service_container.h"

#include <cstddef>
#include <memory>
#include <string>

namespace epidemic::core::diagnostics
{
class ILogger;
}

namespace epidemic::core
{
struct ApplicationOptions
{
    // User-facing name that gets exposed through the initial configuration service.
    std::string application_name = "Epidemic Engine v1.0";
    // Number of worker threads reserved for the baseline task scheduler.
    std::size_t worker_count = 1;
};

class Application
{
  public:
    explicit Application(ApplicationOptions options = {});
    ~Application();

    // Exposes the module registry so the host can register feature modules before bootstrap.
    [[nodiscard]] ModuleRegistry &Modules() noexcept;
    // Exposes the service graph mainly for verification and host-level wiring.
    [[nodiscard]] ServiceContainer &Services() noexcept;

    // Registers core services and runs module bootstrap in dependency order.
    int Bootstrap();
    // Initializes modules after bootstrap has completed successfully.
    int Initialize();
    // Pumps one headless runtime slice: platform events, queued events and scheduled tasks.
    int Run();
    // Shuts modules down in reverse order and waits for background work to settle.
    int Shutdown();

  private:
    enum class State
    {
        Constructed,
        Bootstrapped,
        Initialized,
        Running,
        Failed,
        ShutDown,
    };

    // Installs the built-in services once for the lifetime of the application instance.
    void RegisterCoreServices();
    // Convenience accessor used by lifecycle stages after logger registration.
    [[nodiscard]] std::shared_ptr<diagnostics::ILogger> Logger() const;

    ApplicationOptions options_;
    ServiceContainer services_;
    ModuleRegistry modules_;
    State state_{State::Constructed};
    bool services_registered_{false};
};
} // namespace epidemic::core
