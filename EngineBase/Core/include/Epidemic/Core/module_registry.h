#pragma once

#include <Epidemic/Core/frame_context.h>
#include <Epidemic/Core/imodule.h>
#include <Epidemic/Diagnostics/logger.h>
#include <Epidemic/Foundation/string_id.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace epidemic::core
{
// This file declares the module registry and dependency planner used by Application.
// ModuleRegistry owns module instances, validates dependency graphs, and executes lifecycle
// callbacks in dependency order with reverse-order shutdown.

class ModuleRegistry
{
  public:
    // Adds a module instance before bootstrap begins.
    void Register(std::unique_ptr<IModule> module);

    // Resolves dependencies and calls Bootstrap on all modules.
    void BootstrapAll(ServiceContainer &services, diagnostics::ILogger &logger);

    // Calls Initialize on all modules in dependency order.
    void InitializeAll(ServiceContainer &services, diagnostics::ILogger &logger);

    // Calls Tick on all initialized modules in dependency order.
    void TickAll(ServiceContainer &services, diagnostics::ILogger &logger, const FrameContext &frame_context);

    // Calls Shutdown on bootstrapped modules in reverse dependency order.
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

    // Builds the dependency-resolved execution plan on first use.
    void EnsureExecutionPlan(diagnostics::ILogger &logger);

    std::vector<std::unique_ptr<IModule>> modules_;
    std::unordered_map<foundation::ModuleId, std::size_t> module_index_by_id_;
    std::vector<std::size_t> execution_plan_;
    std::size_t bootstrapped_count_{0};
    LifecycleState state_{LifecycleState::Empty};
};
} // namespace epidemic::core