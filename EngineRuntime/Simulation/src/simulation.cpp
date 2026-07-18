#include "simulation_runtime_impl.h"

#include <utility>

namespace epidemic::runtime::simulation
{
std::unique_ptr<SimulationRuntime> CreateSimulationRuntime(SimulationOptions options, SimulationDependencies dependencies)
{
    return std::make_unique<SimulationRuntime>(options, std::move(dependencies));
}

foundation::Result<SimulationServices> CreateSimulationServices(SimulationOptions options, SimulationDependencies dependencies)
{
    auto runtime = std::make_shared<SimulationRuntime>(options, std::move(dependencies));

    SimulationServices services{};
    services.runtime = runtime;
    services.scheduler = runtime;
    services.memory = runtime;
    services.facts = runtime;
    services.proposals = runtime;
    services.scheduled_tasks = runtime;
    services.attention = runtime;
    services.effects = runtime;
    return foundation::Result<SimulationServices>::Success(std::move(services));
}
} // namespace epidemic::runtime::simulation
