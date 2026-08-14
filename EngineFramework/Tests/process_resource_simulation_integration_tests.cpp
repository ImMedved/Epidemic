#include "Epidemic/GameFramework/ProcessResourceSimulationIntegration/process_resource_simulation_adapters.h"
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::processes;
using namespace epidemic::gameplay::resources;
using namespace epidemic::gameplay::simulation;
using namespace epidemic::gameplay::integration;
int main()
{
    ResourcesProductionService resources;
    ResourceType iron; iron.canonical_name = "test.resource.iron";
    ResourceType sword; sword.canonical_name = "test.resource.sword";
    auto iron_id = resources.RegisterResourceType(iron); auto sword_id = resources.RegisterResourceType(sword); if (!iron_id || !sword_id) return 1;
    GameplayObjectRef owner{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("owner")};
    auto stockpile = resources.CreateStockpile({{}, owner, {}, {}, StockpileState::Active, {}}); if (!stockpile) return 2;
    if (!resources.Add(stockpile.Value(), {iron_id.Value(), 10})) return 11;

    ProcessesService processes;
    ResourceProcessInputProvider in_provider(resources);
    ResourceProcessOutputHandler out_handler(resources);
    processes.SetInputProvider(&in_provider); processes.AddOutputHandler(&out_handler);
    ProcessDefinition def; def.canonical_name = "test.process.craft"; def.kind = ProcessKindId::FromString("test.kind"); def.timing = ProcessTimingPolicy::Timed; def.steps.push_back({ProcessStepId::FromString("test.step"), TypeId::FromString("test.step"), GameplayDuration{5}, {}, {}});
    auto def_id = processes.RegisterDefinition(def); if (!def_id) return 3;
    ProcessResourcePayload iron_payload{stockpile.Value(), iron_id.Value()};
    ProcessResourcePayload sword_payload{stockpile.Value(), sword_id.Value()};
    ProcessRecipe recipe; recipe.canonical_name = "test.recipe.sword"; recipe.process = def_id.Value();
    recipe.inputs.push_back({ProcessInputId::FromString("test.input.iron"), ProcessInputTypeId::FromString("framework.input.resource"), 2, InputConsumptionPolicy::ReserveThenConsume, {}, RegisteredPayload::FromTrivial(ResourceProcessInputProvider::PayloadType(), iron_payload)});
    recipe.outputs.push_back({ProcessOutputId::FromString("test.output.sword"), ResourceProcessOutputHandler::OutputType(), 1, OutputDeliveryPolicy::OnCompletion, RegisteredPayload::FromTrivial(ResourceProcessInputProvider::PayloadType(), sword_payload)});
    auto recipe_id = processes.RegisterRecipe(recipe); if (!recipe_id) return 4;
    auto proc = processes.StartProcess({recipe_id.Value(), owner, {}, {}, GameplayTimePoint{0}, 1, {}}); if (!proc) return 5;

    SimulationService sim;
    ProcessesSimulationLayer process_layer(processes);
    auto region = sim.RegisterRegion({SimulationRegionId::FromString("test.region"), "test.region", owner, SimulationDetailLevel::Abstract, {}, {}, {}}); if (!region) return 6;
    if (!sim.RegisterLayer({ProcessesSimulationLayer::StaticLayer(), "framework.layer.processes", {}, 10, SimulationMaterializationPolicy::AbstractCapable, {}}, &process_layer)) return 7;
    auto summary = sim.SimulateInterval(region.Value(), GameplayTimePoint{0}, GameplayTimePoint{5}); if (!summary) return 8;
    if (resources.GetAmount(stockpile.Value(), iron_id.Value()) != 8 || resources.GetAmount(stockpile.Value(), sword_id.Value()) != 1) return 9;
    for (int i=0;i<100;++i)
    {
        auto summaries = sim.FindSummaries(region.Value());
        if (summaries.empty()) return 10;
    }
    return 0;
}
