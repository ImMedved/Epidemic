#pragma once

#include <Epidemic/Core/imodule.h>
#include <Epidemic/Diagnostics/logger.h>
#include <Epidemic/Foundation/string_id.h>

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
        Failed,
        ShutDown,
    };

    void EnsureExecutionPlan(diagnostics::ILogger &logger);

    std::vector<std::unique_ptr<IModule>> modules_;
    std::unordered_map<foundation::ModuleId, std::size_t> module_index_by_id_;
    std::vector<std::size_t> execution_plan_;
    std::size_t bootstrapped_count_{0};
    LifecycleState state_{LifecycleState::Empty};
};
} // namespace epidemic::core