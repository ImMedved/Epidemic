#include "simulation_runtime_impl.h"

#include <utility>

namespace epidemic::runtime::simulation
{
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
    return foundation::Result<SimulationServices>::Success(std::move(services));
}
} // namespace epidemic::runtime::simulation
