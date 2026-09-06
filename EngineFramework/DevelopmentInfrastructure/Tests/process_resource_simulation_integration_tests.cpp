#include "Epidemic/GameFramework/ProcessResourceSimulationIntegration/process_resource_simulation_adapters.h"

#include <cstddef>
#include <vector>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::processes;
using namespace epidemic::gameplay::resources;
using namespace epidemic::gameplay::simulation;
using namespace epidemic::gameplay::integration;

namespace
{
[[nodiscard]] GameplayObjectRef Ref(const char *domain, const char *id)
{
    return {GameplayDomainId::FromString(domain), GameplayObjectId::FromString(id)};
}

[[nodiscard]] ProcessRecipe MakeResourceRecipe(const char *name, ProcessDefinitionId definition,
                                                ResourceStockpileId stockpile, ResourceTypeId input,
                                                ResourceTypeId output)
{
    ProcessRecipe recipe;
    recipe.canonical_name = name;
    recipe.process = definition;
    recipe.inputs.push_back({ProcessInputId::FromString("test.input.iron"),
                             ProcessInputTypeId::FromString("framework.input.resource"),
                             2,
                             InputConsumptionPolicy::ReserveThenConsume,
                             {},
                             EncodeProcessResourcePayload({stockpile, input})});
    recipe.outputs.push_back({ProcessOutputId::FromString("test.output.sword"),
                              ResourceProcessOutputHandler::OutputType(),
                              1,
                              OutputDeliveryPolicy::OnCompletion,
                              EncodeProcessResourcePayload({stockpile, output})});
    return recipe;
}

[[nodiscard]] int Require(bool condition, int code)
{
    return condition ? 0 : code;
}
} // namespace

int main()
{
    ResourcesProductionService resources;
    ResourceType iron;
    iron.canonical_name = "test.resource.iron";
    ResourceType sword;
    sword.canonical_name = "test.resource.sword";
    auto iron_id = resources.RegisterResourceType(iron);
    auto sword_id = resources.RegisterResourceType(sword);
    if (!iron_id || !sword_id)
        return 1;

    const auto area_a = Ref("test.area", "a");
    const auto area_b = Ref("test.area", "b");
    const auto worker_a = Ref("test.worker", "a");
    const auto worker_b = Ref("test.worker", "b");

    auto stockpile_a = resources.CreateStockpile({{}, worker_a, {}, area_a, StockpileState::Active, {}});
    auto stockpile_b = resources.CreateStockpile({{}, worker_b, {}, area_b, StockpileState::Active, {}});
    if (!stockpile_a || !stockpile_b)
        return 2;
    if (!resources.Add(stockpile_a.Value(), {iron_id.Value(), 10}) ||
        !resources.Add(stockpile_b.Value(), {iron_id.Value(), 10}))
        return 3;

    const auto roundtrip = DecodeProcessResourcePayload(EncodeProcessResourcePayload({stockpile_a.Value(), iron_id.Value()}));
    if (!roundtrip || roundtrip->stockpile != stockpile_a.Value() || roundtrip->resource != iron_id.Value())
        return 4;
    auto invalid_payload = EncodeProcessResourcePayload({stockpile_a.Value(), iron_id.Value()});
    invalid_payload.bytes[0] = static_cast<std::byte>(2);
    if (DecodeProcessResourcePayload(invalid_payload))
        return 5;

    const auto reservation_roundtrip = DecodeProcessResourceReservationPayload(EncodeProcessResourceReservationPayload(
        {ResourceReservationId{GameplayObjectId::FromString("test.reservation")}}));
    if (!reservation_roundtrip || !reservation_roundtrip->reservation.IsValid())
        return 6;

    ProcessesService processes;
    ResourceProcessInputProvider in_provider(resources);
    ResourceProcessOutputHandler out_handler(resources);
    processes.SetInputProvider(&in_provider);
    processes.AddOutputHandler(&out_handler);

    ProcessDefinition def;
    def.canonical_name = "test.process.craft";
    def.kind = ProcessKindId::FromString("test.kind");
    def.timing = ProcessTimingPolicy::Timed;
    def.steps.push_back({ProcessStepId::FromString("test.step"),
                         TypeId::FromString("test.step"),
                         GameplayDuration{5},
                         {},
                         {}});
    auto def_id = processes.RegisterDefinition(def);
    if (!def_id)
        return 7;

    auto recipe_a = processes.RegisterRecipe(
        MakeResourceRecipe("test.recipe.sword_a", def_id.Value(), stockpile_a.Value(), iron_id.Value(), sword_id.Value()));
    auto recipe_b = processes.RegisterRecipe(
        MakeResourceRecipe("test.recipe.sword_b", def_id.Value(), stockpile_b.Value(), iron_id.Value(), sword_id.Value()));
    if (!recipe_a || !recipe_b)
        return 8;
    processes.Freeze(); // freeze process definitions for integration simulation

    StartProcessRequest start_a;
    start_a.recipe = recipe_a.Value();
    start_a.actor = worker_a;
    start_a.now = GameplayTimePoint{0};
    start_a.seed = 1;
    start_a.simulation_area = area_a;
    auto proc_a = processes.StartProcess(start_a);

    StartProcessRequest start_b;
    start_b.recipe = recipe_b.Value();
    start_b.actor = worker_b;
    start_b.now = GameplayTimePoint{0};
    start_b.seed = 2;
    start_b.simulation_area = area_b;
    auto proc_b = processes.StartProcess(start_b);
    if (!proc_a || !proc_b)
        return 9;

    SimulationService sim;
    ProcessesSimulationLayer process_layer(processes);
    ProductionSimulationLayer production_layer(resources);

    SimulationTask production_task;
    production_task.area = area_a;
    auto production_prepare = production_layer.Prepare(production_task);
    if (!production_prepare || production_prepare.Value().state != SimulationTaskState::Skipped ||
        production_prepare.Value().operations != 0)
        return 10;
    if (!production_layer.Commit(production_task, production_prepare.Value()))
        return 11;
    SimulationLayerSummary forged_production;
    forged_production.layer = ProductionSimulationLayer::StaticLayer();
    forged_production.state = SimulationTaskState::Completed;
    forged_production.operations = 1;
    if (production_layer.Commit(production_task, forged_production))
        return 12;

    auto region_a = sim.RegisterRegion({SimulationRegionId::FromString("test.region.a"),
                                        "test.region.a",
                                        area_a,
                                        SimulationDetailLevel::Abstract,
                                        {},
                                        {},
                                        {}});
    auto region_b = sim.RegisterRegion({SimulationRegionId::FromString("test.region.b"),
                                        "test.region.b",
                                        area_b,
                                        SimulationDetailLevel::Abstract,
                                        {},
                                        {},
                                        {}});
    if (!region_a || !region_b)
        return 13;
    if (!sim.RegisterLayer({ProcessesSimulationLayer::StaticLayer(),
                            "framework.layer.processes",
                            {},
                            10,
                            SimulationMaterializationPolicy::AbstractCapable,
                            {}},
                           &process_layer))
        return 14;

    auto summary_a = sim.SimulateInterval(region_a.Value(), GameplayTimePoint{0}, GameplayTimePoint{5});
    if (!summary_a)
        return 15;
    if (const int failure = Require(processes.FindInstance(proc_a.Value())->state == ProcessInstanceState::Completed, 16))
        return failure;
    if (const int failure = Require(processes.FindInstance(proc_b.Value())->state == ProcessInstanceState::Running, 17))
        return failure;
    if (resources.GetAmount(stockpile_a.Value(), iron_id.Value()) != 8 ||
        resources.GetAmount(stockpile_a.Value(), sword_id.Value()) != 1 ||
        resources.GetAmount(stockpile_b.Value(), iron_id.Value()) != 10 ||
        resources.GetAmount(stockpile_b.Value(), sword_id.Value()) != 0)
        return 18;

    auto summary_b = sim.SimulateInterval(region_b.Value(), GameplayTimePoint{0}, GameplayTimePoint{5});
    if (!summary_b)
        return 19;
    if (processes.FindInstance(proc_b.Value())->state != ProcessInstanceState::Completed)
        return 20;

    StartProcessRequest start_a2 = start_a;
    start_a2.now = GameplayTimePoint{5};
    start_a2.seed = 3;
    auto proc_a2 = processes.StartProcess(start_a2);
    if (!proc_a2)
        return 21;

    SimulationTask prepared_task;
    prepared_task.layer = ProcessesSimulationLayer::StaticLayer();
    prepared_task.area = area_a;
    prepared_task.from = GameplayTimePoint{5};
    prepared_task.to = GameplayTimePoint{10};
    auto prepared = process_layer.Prepare(prepared_task);
    if (!prepared || prepared.Value().operations != 1 || prepared.Value().prepared_operations.size() != 1)
        return 22;

    StartProcessRequest start_a3 = start_a;
    start_a3.now = GameplayTimePoint{5};
    start_a3.seed = 4;
    auto proc_a3 = processes.StartProcess(start_a3);
    if (!proc_a3)
        return 23;

    if (!process_layer.Commit(prepared_task, prepared.Value()))
        return 24;
    if (processes.FindInstance(proc_a2.Value())->state != ProcessInstanceState::Completed ||
        processes.FindInstance(proc_a3.Value())->state != ProcessInstanceState::Running)
        return 25;
    if (resources.GetAmount(stockpile_a.Value(), sword_id.Value()) != 2)
        return 26;
    if (!process_layer.Commit(prepared_task, prepared.Value()))
        return 27;
    if (resources.GetAmount(stockpile_a.Value(), sword_id.Value()) != 2)
        return 28;

    SimulationTask no_area_task;
    no_area_task.layer = ProcessesSimulationLayer::StaticLayer();
    no_area_task.from = GameplayTimePoint{5};
    no_area_task.to = GameplayTimePoint{10};
    auto no_area_prepare = process_layer.Prepare(no_area_task);
    if (!no_area_prepare || no_area_prepare.Value().state != SimulationTaskState::Skipped)
        return 29;
    if (!process_layer.Commit(no_area_task, no_area_prepare.Value()))
        return 30;
    if (processes.FindInstance(proc_a3.Value())->state != ProcessInstanceState::Running)
        return 31;

    return 0;
}
