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
    void Register(std::unique_ptr<IModule> module);

    void BootstrapAll(ServiceContainer &services, diagnostics::ILogger &logger);
    void InitializeAll(ServiceContainer &services, diagnostics::ILogger &logger);
    void ShutdownAll(ServiceContainer &services, diagnostics::ILogger &logger);

    [[nodiscard]] std::size_t Size() const noexcept;

  private:
    enum class LifecycleState
    {
        Empty,
        Registered,
        Bootstrapped,
        Initialized,
        ShutDown,
    };

    void EnsureExecutionPlan(diagnostics::ILogger &logger);

    std::vector<std::unique_ptr<IModule>> modules_;
    std::unordered_map<std::string, std::size_t> module_index_by_id_;
    std::vector<std::size_t> execution_plan_;
    LifecycleState state_{LifecycleState::Empty};
};
} // namespace epidemic::core
