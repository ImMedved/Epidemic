#pragma once

#include <Epidemic/Core/module_registry.h>
#include <Epidemic/Core/service_container.h>

#include <memory>
#include <string>

namespace epidemic::diagnostics
{
class ILogger;
}

namespace epidemic::core
{
struct ApplicationOptions
{
    std::string application_name = "Epidemic Engine v1.0";
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
    int Tick();
    int Run();
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

    void ValidateCoreServices() const;
    [[nodiscard]] std::shared_ptr<diagnostics::ILogger> Logger() const;

    ApplicationOptions options_;
    ServiceContainer services_;
    ModuleRegistry modules_;
    State state_{State::Constructed};
};
} // namespace epidemic::core
