#pragma once

#include "core/diagnostics/logger.h"
#include "core/modules/imodule.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace epidemic::core
{
class ModuleRegistry
{
  public:
    // Adds a module to the registry before bootstrap begins.
    void Register(std::unique_ptr<IModule> module);

    // Resolves the execution plan and calls OnBootstrap on each module.
    void BootstrapAll(ServiceContainer &services, diagnostics::ILogger &logger);
    // Calls OnInitialize using the previously resolved execution plan.
    void InitializeAll(ServiceContainer &services, diagnostics::ILogger &logger);
    // Calls OnShutdown in the reverse order of the resolved execution plan.
    void ShutdownAll(ServiceContainer &services, diagnostics::ILogger &logger);

    // Returns the number of registered modules.
    [[nodiscard]] std::size_t Size() const noexcept;

  private:
    enum class LifecycleState
    {
        Empty,
        Registered,
        Bootstrapped,
        Initialized,
        Failed,
        ShutDown,
    };

    // Builds and caches the dependency-aware execution order.
    void EnsureExecutionPlan(diagnostics::ILogger &logger);

    std::vector<std::unique_ptr<IModule>> modules_;
    std::unordered_map<std::string, std::size_t> module_index_by_id_;
    std::vector<std::size_t> execution_plan_;
    std::size_t bootstrapped_count_{0};
    LifecycleState state_{LifecycleState::Empty};
};
} // namespace epidemic::core
