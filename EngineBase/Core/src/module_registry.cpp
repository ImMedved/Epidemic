#include <Epidemic/Core/module_registry.h>

#include <Epidemic/Diagnostics/profiling.h>

#include <algorithm>
#include <exception>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace epidemic::core
{
// This file implements module ownership, dependency ordering, and lifecycle execution.
// Public lifecycle contracts are documented in module_registry.h; helpers below support dependency resolution.

namespace
{
// Converts manifest id text into the strongly typed module identifier used by the registry.
[[nodiscard]] foundation::ModuleId ToModuleId(std::string_view id_text)
{
    return foundation::ModuleId::FromString(id_text);
}

// Builds a human-readable lifecycle log line for one module action.
std::string BuildLifecycleMessage(std::string_view action, const ModuleManifest &manifest)
{
    std::string message(action);
    message += " module ";
    message += manifest.id;
    message += " (";
    message += manifest.name;
    message += ")";
    return message;
}
} // namespace

// Adds a module to the registry before bootstrap begins.
void ModuleRegistry::Register(std::unique_ptr<IModule> module)
{
    if (!module)
    {
        throw std::invalid_argument("Module must not be null");
    }

    if (state_ != LifecycleState::Empty && state_ != LifecycleState::Registered)
    {
        throw std::runtime_error("Modules must be registered before bootstrap begins");
    }

    const auto &manifest = module->Manifest();
    const auto module_id = ToModuleId(manifest.id);
    if (!module_id.IsValid())
    {
        throw std::runtime_error("Module id must not be empty");
    }

    if (const auto existing_name = canonical_module_name_by_id_.find(module_id); existing_name != canonical_module_name_by_id_.end())
    {
        if (existing_name->second == manifest.id)
        {
            throw std::runtime_error("Module id already registered: " + manifest.id);
        }

        throw std::runtime_error("Module id hash collision between '" + existing_name->second + "' and '" +
                                 manifest.id + "'");
    }

    module_index_by_id_.emplace(module_id, modules_.size());
    canonical_module_name_by_id_.emplace(module_id, manifest.id);
    modules_.push_back(std::move(module));
    execution_plan_.clear();
    state_ = LifecycleState::Registered;
}

// Resolves execution order and calls Bootstrap on all modules.
void ModuleRegistry::BootstrapAll(ServiceContainer &services, diagnostics::ILogger &logger)
{
    EPIDEMIC_PROFILE_SCOPE("ModuleRegistry::BootstrapAll");
    if (state_ != LifecycleState::Registered && state_ != LifecycleState::Empty)
    {
        throw std::runtime_error("Invalid state for module bootstrap");
    }

    logger.Info("Core", "Modules", "Registered modules: " + std::to_string(modules_.size()));
    EnsureExecutionPlan(logger);
    bootstrapped_count_ = 0;

    try
    {
        for (const auto index : execution_plan_)
        {
            const auto &module = modules_[index];
            logger.Info("Core", "Lifecycle", BuildLifecycleMessage("Bootstrapping", module->Manifest()));
            module->Bootstrap(services);
            ++bootstrapped_count_;
        }

        state_ = LifecycleState::Bootstrapped;
    }
    catch (...)
    {
        state_ = LifecycleState::Failed;
        throw;
    }
}

// Calls Initialize on every bootstrapped module.
void ModuleRegistry::InitializeAll(ServiceContainer &services, diagnostics::ILogger &logger)
{
    EPIDEMIC_PROFILE_SCOPE("ModuleRegistry::InitializeAll");
    if (state_ != LifecycleState::Bootstrapped)
    {
        throw std::runtime_error("Invalid state for module initialization");
    }

    try
    {
        for (const auto index : execution_plan_)
        {
            const auto &module = modules_[index];
            logger.Info("Core", "Lifecycle", BuildLifecycleMessage("Initializing", module->Manifest()));
            module->Initialize(services);
        }

        state_ = LifecycleState::Initialized;
    }
    catch (...)
    {
        state_ = LifecycleState::Failed;
        throw;
    }
}

// Calls Tick on every initialized module in execution order.
void ModuleRegistry::TickAll(ServiceContainer &services, diagnostics::ILogger &logger, const FrameContext &frame_context)
{
    EPIDEMIC_PROFILE_SCOPE("ModuleRegistry::TickAll");
    if (state_ != LifecycleState::Initialized)
    {
        throw std::runtime_error("Invalid state for module tick");
    }

    try
    {
        for (const auto index : execution_plan_)
        {
            const auto &module = modules_[index];
            logger.Debug("Core", "Lifecycle", BuildLifecycleMessage("Ticking", module->Manifest()));
            module->Tick(services, frame_context);
        }
    }
    catch (...)
    {
        state_ = LifecycleState::Failed;
        throw;
    }
}

// Calls Shutdown in reverse execution order for modules that reached bootstrap.
void ModuleRegistry::ShutdownAll(ServiceContainer &services, diagnostics::ILogger &logger)
{
    EPIDEMIC_PROFILE_SCOPE("ModuleRegistry::ShutdownAll");
    if (state_ == LifecycleState::Empty || state_ == LifecycleState::Registered || state_ == LifecycleState::ShutDown)
    {
        return;
    }

    if (state_ != LifecycleState::Bootstrapped && state_ != LifecycleState::Initialized && state_ != LifecycleState::Failed)
    {
        throw std::runtime_error("Invalid state for module shutdown");
    }

    EnsureExecutionPlan(logger);
    const auto shutdown_count = std::min(bootstrapped_count_, execution_plan_.size());

    std::exception_ptr first_error;
    for (std::size_t reverse_index = shutdown_count; reverse_index > 0; --reverse_index)
    {
        const auto execution_index = execution_plan_[reverse_index - 1];
        const auto &module = modules_[execution_index];
        logger.Info("Core", "Lifecycle", BuildLifecycleMessage("Shutting down", module->Manifest()));
        try
        {
            module->Shutdown(services);
        }
        catch (const std::exception &exception)
        {
            logger.Error("Core", "Lifecycle", "Module shutdown failed for '" + module->Manifest().id + "': " +
                                                 std::string(exception.what()));
            if (!first_error)
            {
                first_error = std::current_exception();
            }
        }
        catch (...)
        {
            logger.Error("Core", "Lifecycle", "Module shutdown failed for '" + module->Manifest().id +
                                                 "' with unknown exception");
            if (!first_error)
            {
                first_error = std::current_exception();
            }
        }
    }

    bootstrapped_count_ = 0;
    state_ = LifecycleState::ShutDown;
    if (first_error)
    {
        std::rethrow_exception(first_error);
    }
}

// Returns the number of registered module instances.
std::size_t ModuleRegistry::Size() const noexcept
{
    return modules_.size();
}

// Builds the dependency-resolved execution plan lazily and logs the final order.
void ModuleRegistry::EnsureExecutionPlan(diagnostics::ILogger &logger)
{
    if (!execution_plan_.empty() || modules_.empty())
    {
        return;
    }

    enum class VisitState
    {
        NotVisited,
        Visiting,
        Visited,
    };

    std::vector<VisitState> visit_states(modules_.size(), VisitState::NotVisited);
    std::vector<std::size_t> resolved;
    resolved.reserve(modules_.size());

    std::function<void(std::size_t)> visit = [&](std::size_t module_index) {
        const auto current_state = visit_states[module_index];
        if (current_state == VisitState::Visited)
        {
            return;
        }
        if (current_state == VisitState::Visiting)
        {
            throw std::runtime_error("Circular module dependency detected at " + modules_[module_index]->Manifest().id);
        }

        visit_states[module_index] = VisitState::Visiting;
        const auto &manifest = modules_[module_index]->Manifest();
        for (const auto &dependency_id_text : manifest.dependencies)
        {
            const auto dependency_id = ToModuleId(dependency_id_text);
            if (!dependency_id.IsValid())
            {
                throw std::runtime_error("Module '" + manifest.id + "' contains an empty dependency id");
            }

            const auto dependency_it = module_index_by_id_.find(dependency_id);
            if (dependency_it == module_index_by_id_.end())
            {
                throw std::runtime_error("Missing module dependency '" + dependency_id_text + "' for module '" + manifest.id + "'");
            }

            const auto canonical_dependency = canonical_module_name_by_id_.find(dependency_id);
            if (canonical_dependency == canonical_module_name_by_id_.end() || canonical_dependency->second != dependency_id_text)
            {
                throw std::runtime_error("Module dependency id hash collision for '" + dependency_id_text +
                                         "' in module '" + manifest.id + "'");
            }

            visit(dependency_it->second);
        }

        visit_states[module_index] = VisitState::Visited;
        resolved.push_back(module_index);
    };

    for (std::size_t module_index = 0; module_index < modules_.size(); ++module_index)
    {
        visit(module_index);
    }

    execution_plan_ = std::move(resolved);

    std::ostringstream stream;
    stream << "Resolved module execution order: ";
    for (std::size_t index = 0; index < execution_plan_.size(); ++index)
    {
        if (index > 0)
        {
            stream << " -> ";
        }
        stream << modules_[execution_plan_[index]]->Manifest().id;
    }
    logger.Info("Core", "Modules", stream.str());
}
} // namespace epidemic::core
