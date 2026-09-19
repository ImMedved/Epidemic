#include <Epidemic/Core/module_registry.h>

#include <Epidemic/Diagnostics/profiling.h>

#if defined(EPIDEMIC_CORE_ENABLE_TEST_HOOKS)
#include "module_registry_test_hooks.h"
#endif

#include <algorithm>
#include <exception>
#include <functional>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>

namespace epidemic::core
{
// This file implements module ownership, dependency ordering, and lifecycle execution.
// Public lifecycle contracts are documented in module_registry.h; helpers below support dependency resolution.

namespace
{
#if defined(EPIDEMIC_CORE_ENABLE_TEST_HOOKS)
thread_local testing::ModuleRegistryFaultPoint g_module_registry_fault_point =
    testing::ModuleRegistryFaultPoint::None;

[[nodiscard]] bool ConsumeModuleRegistryFault(testing::ModuleRegistryFaultPoint fault_point) noexcept
{
    if (g_module_registry_fault_point != fault_point)
    {
        return false;
    }

    g_module_registry_fault_point = testing::ModuleRegistryFaultPoint::None;
    return true;
}
#endif

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

void LogLifecycleNoThrow(diagnostics::ILogger &logger, diagnostics::LogLevel level, std::string_view action,
                         const ModuleManifest &manifest) noexcept
{
    try
    {
        logger.Log(level, "Core", "Lifecycle", BuildLifecycleMessage(action, manifest));
    }
    catch (...)
    {
        // Formatting and logging are observational and cannot stop lifecycle work.
    }
}

void LogModuleCountNoThrow(diagnostics::ILogger &logger, std::size_t module_count) noexcept
{
    try
    {
        logger.Info("Core", "Modules", "Registered modules: " + std::to_string(module_count));
    }
    catch (...)
    {
    }
}

// Keeps diagnostic formatting from masking the cleanup failure being reported.
void LogShutdownFailureNoThrow(diagnostics::ILogger &logger, const ModuleManifest &manifest,
                               std::exception_ptr failure) noexcept
{
    try
    {
        try
        {
            std::rethrow_exception(failure);
        }
        catch (const std::exception &exception)
        {
            logger.Error("Core", "Lifecycle", "Module shutdown failed for '" + manifest.id + "': " + exception.what());
        }
        catch (...)
        {
            logger.Error("Core", "Lifecycle", "Module shutdown failed for '" + manifest.id + "' with unknown exception");
        }
    }
    catch (...)
    {
        // A best-effort diagnostic must never replace the original cleanup failure.
    }
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

    auto candidate_indices = module_index_by_id_;
    auto candidate_names = canonical_module_name_by_id_;
    candidate_indices.emplace(module_id, modules_.size());
    candidate_names.emplace(module_id, manifest.id);

    // Complete every fallible allocation before publishing any part of the registration.
    modules_.reserve(modules_.size() + 1);
    shutdown_completed_.reserve(shutdown_completed_.size() + 1);
#if defined(EPIDEMIC_CORE_ENABLE_TEST_HOOKS)
    if (ConsumeModuleRegistryFault(testing::ModuleRegistryFaultPoint::BeforeRegistrationCommit))
    {
        throw std::bad_alloc{};
    }
#endif
    modules_.push_back(std::move(module));
    shutdown_completed_.push_back(0);
    module_index_by_id_.swap(candidate_indices);
    canonical_module_name_by_id_.swap(candidate_names);
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

    LogModuleCountNoThrow(logger, modules_.size());
    EnsureExecutionPlan(logger);
    bootstrapped_count_ = 0;
    std::fill(shutdown_completed_.begin(), shutdown_completed_.end(), std::uint8_t{0});

    try
    {
        for (const auto index : execution_plan_)
        {
            const auto &module = modules_[index];
            LogLifecycleNoThrow(logger, diagnostics::LogLevel::Info, "Bootstrapping", module->Manifest());
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
            LogLifecycleNoThrow(logger, diagnostics::LogLevel::Info, "Initializing", module->Manifest());
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
            LogLifecycleNoThrow(logger, diagnostics::LogLevel::Debug, "Ticking", module->Manifest());
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
        if (shutdown_completed_[execution_index] != 0)
        {
            continue;
        }

        const auto &module = modules_[execution_index];
        LogLifecycleNoThrow(logger, diagnostics::LogLevel::Info, "Shutting down", module->Manifest());
        try
        {
            module->Shutdown(services);
            shutdown_completed_[execution_index] = 1;
        }
        catch (const std::exception &)
        {
            const auto failure = std::current_exception();
            LogShutdownFailureNoThrow(logger, module->Manifest(), failure);
            if (!first_error)
            {
                first_error = failure;
            }
        }
        catch (...)
        {
            const auto failure = std::current_exception();
            LogShutdownFailureNoThrow(logger, module->Manifest(), failure);
            if (!first_error)
            {
                first_error = failure;
            }
        }
    }

    if (first_error)
    {
        state_ = LifecycleState::Failed;
        std::rethrow_exception(first_error);
    }

    bootstrapped_count_ = 0;
    state_ = LifecycleState::ShutDown;
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
        auto dependencies = manifest.dependencies;
        std::sort(dependencies.begin(), dependencies.end());
        for (const auto &dependency_id_text : dependencies)
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

    std::vector<std::size_t> module_indices(modules_.size());
    for (std::size_t module_index = 0; module_index < modules_.size(); ++module_index)
    {
        module_indices[module_index] = module_index;
    }
    std::sort(module_indices.begin(), module_indices.end(), [this](std::size_t left, std::size_t right) {
        return modules_[left]->Manifest().id < modules_[right]->Manifest().id;
    });
    for (const auto module_index : module_indices)
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
#if defined(EPIDEMIC_CORE_ENABLE_TEST_HOOKS)
void testing::SetModuleRegistryFaultPoint(ModuleRegistryFaultPoint fault_point) noexcept
{
    g_module_registry_fault_point = fault_point;
}

void testing::ClearModuleRegistryFaultPoint() noexcept
{
    g_module_registry_fault_point = ModuleRegistryFaultPoint::None;
}
#endif

} // namespace epidemic::core
