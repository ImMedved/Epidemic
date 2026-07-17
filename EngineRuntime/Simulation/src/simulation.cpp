#include "simulation_runtime_impl.h"

namespace epidemic::runtime::simulation
{
std::unique_ptr<SimulationRuntime> CreateSimulationRuntime(SimulationOptions options)
{
    return std::make_unique<SimulationRuntime>(options);
}

SimulationServices CreateSimulationServices(SimulationOptions options)
{
    auto runtime = std::make_shared<SimulationRuntime>(options);

    SimulationServices services{};
    services.runtime = runtime;
    services.attention = runtime;
    services.memory = runtime;
    services.effects = runtime;
    return services;
}
} // namespace epidemic::runtime::simulation
