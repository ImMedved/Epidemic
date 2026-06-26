#include "core/modules/module_registry.h"

#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace epidemic::core
{
namespace
{
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
    if (manifest.id.empty())
    {
        throw std::runtime_error("Module id must not be empty");
    }

    if (module_index_by_id_.contains(manifest.id))
    {
        throw std::runtime_error("Module id already registered: " + manifest.id);
    }

    module_index_by_id_.emplace(manifest.id, modules_.size());
    modules_.push_back(std::move(module));
    execution_plan_.clear();
    state_ = LifecycleState::Registered;
}

void ModuleRegistry::BootstrapAll(ServiceContainer &services, diagnostics::ILogger &logger)
{
    if (state_ != LifecycleState::Registered && state_ != LifecycleState::Empty)
    {
        throw std::runtime_error("Invalid state for module bootstrap");
    }

    EnsureExecutionPlan(logger);
    for (const auto index : execution_plan_)
    {
        const auto &module = modules_[index];
        logger.Info("Core", BuildLifecycleMessage("Bootstrapping", module->Manifest()));
        module->OnBootstrap(services);
    }

    state_ = LifecycleState::Bootstrapped;
}

void ModuleRegistry::InitializeAll(ServiceContainer &services, diagnostics::ILogger &logger)
{
    if (state_ != LifecycleState::Bootstrapped)
    {
        throw std::runtime_error("Invalid state for module initialization");
    }

    for (const auto index : execution_plan_)
    {
        const auto &module = modules_[index];
        logger.Info("Core", BuildLifecycleMessage("Initializing", module->Manifest()));
        module->OnInitialize(services);
    }

    state_ = LifecycleState::Initialized;
}

void ModuleRegistry::ShutdownAll(ServiceContainer &services, diagnostics::ILogger &logger)
{
    if (state_ != LifecycleState::Bootstrapped && state_ != LifecycleState::Initialized)
    {
        if (state_ == LifecycleState::ShutDown)
        {
            return;
        }

        throw std::runtime_error("Invalid state for module shutdown");
    }

    EnsureExecutionPlan(logger);
    for (auto it = execution_plan_.rbegin(); it != execution_plan_.rend(); ++it)
    {
        const auto &module = modules_[*it];
        logger.Info("Core", BuildLifecycleMessage("Shutting down", module->Manifest()));
        module->OnShutdown(services);
    }

    state_ = LifecycleState::ShutDown;
}

std::size_t ModuleRegistry::Size() const noexcept
{
    return modules_.size();
}

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
        for (const auto &dependency_id : manifest.dependencies)
        {
            const auto dependency_it = module_index_by_id_.find(dependency_id);
            if (dependency_it == module_index_by_id_.end())
            {
                throw std::runtime_error("Missing module dependency '" + dependency_id + "' for module '" + manifest.id + "'");
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
    logger.Info("Core", stream.str());
}
} // namespace epidemic::core
