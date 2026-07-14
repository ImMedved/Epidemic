#include "simulation_runtime_impl.h"

namespace epidemic::runtime::simulation
{
std::unique_ptr<SimulationRuntime> CreateSimulationRuntime(SimulationOptions options)
{
    return std::make_unique<SimulationRuntime>(options);
}
} // namespace epidemic::runtime::simulation
