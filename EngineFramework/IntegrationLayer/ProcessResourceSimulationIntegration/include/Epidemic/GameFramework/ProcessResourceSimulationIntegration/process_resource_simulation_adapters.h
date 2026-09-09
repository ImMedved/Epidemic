#pragma once

#include "Epidemic/GameFramework/Processes/processes.h"
#include "Epidemic/GameFramework/ResourcesProduction/resources_production.h"
#include "Epidemic/GameFramework/Simulation/simulation.h"

#include <optional>
#include <vector>

namespace epidemic::gameplay::integration
{
struct ProcessResourcePayload
{
    resources::ResourceStockpileId stockpile{};
    resources::ResourceTypeId resource{};
};
struct ProcessResourceReservationPayload
{
    resources::ResourceReservationId reservation{};
};
struct PreparedProcessResourceOutputPayload
{
    resources::ResourceStockpileId stockpile{};
    resources::ResourceTypeId resource{};
    processes::Fixed amount = 0;
};

[[nodiscard]] processes::RegisteredPayload EncodeProcessResourcePayload(ProcessResourcePayload payload);
[[nodiscard]] std::optional<ProcessResourcePayload> DecodeProcessResourcePayload(
    const processes::RegisteredPayload &payload);
[[nodiscard]] processes::RegisteredPayload EncodeProcessResourceReservationPayload(
    ProcessResourceReservationPayload payload);
[[nodiscard]] std::optional<ProcessResourceReservationPayload> DecodeProcessResourceReservationPayload(
    const processes::RegisteredPayload &payload);

class ResourceProcessInputProvider final : public processes::IProcessInputProvider
{
public:
    explicit ResourceProcessInputProvider(resources::ResourcesProductionService& resources) : resources_(resources) {}
    [[nodiscard]] static constexpr TypeId PayloadType() noexcept { return TypeId::FromString("framework.process.resource.payload"); }
    [[nodiscard]] static constexpr TypeId ReservationPayloadType() noexcept { return TypeId::FromString("framework.process.resource.reservation"); }
    [[nodiscard]] foundation::Result<void> Validate(const processes::ProcessInputDefinition& input, const processes::StartProcessRequest& request, processes::ProcessInstanceId instance) override;
    [[nodiscard]] foundation::Result<processes::ReservedProcessInput> Reserve(const processes::ProcessInputDefinition& input, const processes::StartProcessRequest& request, processes::ProcessInstanceId instance) override;
    [[nodiscard]] foundation::Result<void> Consume(const processes::ReservedProcessInput& reservation, GameplayContext context) override;
    [[nodiscard]] foundation::Result<void> Release(const processes::ReservedProcessInput& reservation, GameplayContext context) override;
private:
    resources::ResourcesProductionService& resources_;
};

class ResourceProcessOutputHandler final : public processes::IProcessOutputHandler
{
public:
    explicit ResourceProcessOutputHandler(resources::ResourcesProductionService& resources) : resources_(resources) {}
    [[nodiscard]] static constexpr processes::ProcessOutputTypeId OutputType() noexcept { return processes::ProcessOutputTypeId::FromString("framework.output.resource"); }
    [[nodiscard]] bool Supports(processes::ProcessOutputTypeId type) const noexcept override { return type == OutputType(); }
    [[nodiscard]] foundation::Result<processes::PreparedProcessOutput> Prepare(const processes::ProcessOutputDefinition& output, const processes::ProcessInstance& instance, GameplayContext context) override;
    [[nodiscard]] foundation::Result<void> Commit(const processes::PreparedProcessOutput& output, const processes::ProcessInstance& instance, GameplayContext context) override;
    [[nodiscard]] foundation::Result<void> Cancel(const processes::PreparedProcessOutput& output, const processes::ProcessInstance& instance, GameplayContext context) override;
private:
    resources::ResourcesProductionService& resources_;
};

// Timed production recipes execute through Processes. ResourcesProduction remains the
// authoritative ledger/capability/plan owner; this integration intentionally exposes no
// second aggregate timed-production executor.
class ProcessesSimulationLayer final : public simulation::ISimulationLayerExecutor
{
public:
    explicit ProcessesSimulationLayer(processes::ProcessesService& processes) : processes_(processes) {}
    [[nodiscard]] static constexpr simulation::SimulationLayerId StaticLayer() noexcept { return simulation::SimulationLayerId::FromString("framework.layer.processes"); }
    [[nodiscard]] simulation::SimulationLayerId Layer() const noexcept override { return StaticLayer(); }
    [[nodiscard]] foundation::Result<simulation::SimulationLayerSummary> Prepare(const simulation::SimulationTask& task) override;
    [[nodiscard]] foundation::Result<void> Commit(const simulation::SimulationTask& task, const simulation::SimulationLayerSummary& summary) override;
private:
    processes::ProcessesService& processes_;
};
}
