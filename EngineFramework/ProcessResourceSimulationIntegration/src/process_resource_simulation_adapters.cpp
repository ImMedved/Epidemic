#include "Epidemic/GameFramework/ProcessResourceSimulationIntegration/process_resource_simulation_adapters.h"
#include "Epidemic/Foundation/error.h"

namespace epidemic::gameplay::integration
{
namespace
{
[[nodiscard]] foundation::Error Error(std::string_view code, std::string_view message) { return foundation::Error::Create(code, message); }
}
foundation::Result<processes::ReservedProcessInput> ResourceProcessInputProvider::Reserve(const processes::ProcessInputDefinition& input, const processes::StartProcessRequest& request, processes::ProcessInstanceId instance)
{
    auto payload = input.payload.AsTrivial<ProcessResourcePayload>(PayloadType());
    if (!payload) return foundation::Result<processes::ReservedProcessInput>::Failure(Error("gameplay.integration.resource_payload_invalid", "process input payload is not a resource payload"));
    std::vector<resources::ResourceQuantity> required{{payload->resource, input.amount}};
    auto resource_reservation = resources_.Reserve(payload->stockpile, std::move(required), request.actor, TypeId::FromString("framework.process.input"), request.context);
    if (!resource_reservation)
    {
        return foundation::Result<processes::ReservedProcessInput>::Failure(resource_reservation.GetError());
    }
    processes::ReservedProcessInput reservation;
    reservation.id = processes::ProcessReservationId::FromRaw(instance.value.High(), input.id.value.Raw());
    reservation.input = input.id;
    reservation.type = input.type;
    reservation.amount = input.amount;
    reservation.provider_token = processes::RegisteredPayload::FromTrivial(ReservationPayloadType(), ProcessResourceReservationPayload{resource_reservation.Value()});
    return foundation::Result<processes::ReservedProcessInput>::Success(std::move(reservation));
}
foundation::Result<void> ResourceProcessInputProvider::Consume(const processes::ReservedProcessInput& reservation, GameplayContext context)
{
    auto payload = reservation.provider_token.AsTrivial<ProcessResourceReservationPayload>(ReservationPayloadType());
    if (!payload) return foundation::Result<void>::Failure(Error("gameplay.integration.resource_payload_invalid", "process reservation payload is invalid"));
    return resources_.ConsumeReservation(payload->reservation, context);
}
foundation::Result<void> ResourceProcessInputProvider::Release(const processes::ReservedProcessInput& reservation, GameplayContext context)
{
    auto payload = reservation.provider_token.AsTrivial<ProcessResourceReservationPayload>(ReservationPayloadType());
    if (!payload) return foundation::Result<void>::Failure(Error("gameplay.integration.resource_payload_invalid", "process reservation payload is invalid"));
    const auto* current = resources_.FindReservation(payload->reservation);
    if (!current || current->state != resources::ResourceReservationState::Active) return foundation::Result<void>::Success();
    return resources_.ReleaseReservation(payload->reservation, context);
}
foundation::Result<void> ResourceProcessOutputHandler::Produce(const processes::ProcessOutputDefinition& output, const processes::ProcessInstance&, GameplayContext context)
{
    auto payload = output.payload.AsTrivial<ProcessResourcePayload>(ResourceProcessInputProvider::PayloadType());
    if (!payload) return foundation::Result<void>::Failure(Error("gameplay.integration.resource_payload_invalid", "process output payload is not a resource payload"));
    return resources_.Add(payload->stockpile, {payload->resource, output.amount}, context);
}
foundation::Result<simulation::SimulationLayerSummary> ProductionSimulationLayer::Prepare(const simulation::SimulationTask& task)
{
    const auto snapshot = resources_.CaptureSnapshot();
    simulation::SimulationLayerSummary summary;
    summary.layer = StaticLayer();
    summary.state = simulation::SimulationTaskState::Completed;
    summary.operations = snapshot.plans.size();
    summary.revision = snapshot.revision;
    (void)task;
    return foundation::Result<simulation::SimulationLayerSummary>::Success(summary);
}
foundation::Result<void> ProductionSimulationLayer::Commit(const simulation::SimulationTask& task, const simulation::SimulationLayerSummary&)
{
    // Resource production owns aggregate capacity/plans only. Timed recipe execution is owned by Processes.
    (void)task;
    return foundation::Result<void>::Success();
}
foundation::Result<simulation::SimulationLayerSummary> ProcessesSimulationLayer::Prepare(const simulation::SimulationTask& task)
{
    const auto snapshot = processes_.CaptureSnapshot();
    simulation::SimulationLayerSummary summary;
    summary.layer = StaticLayer();
    summary.state = simulation::SimulationTaskState::Completed;
    summary.operations = snapshot.instances.size();
    summary.revision = snapshot.revision;
    (void)task;
    return foundation::Result<simulation::SimulationLayerSummary>::Success(summary);
}
foundation::Result<void> ProcessesSimulationLayer::Commit(const simulation::SimulationTask& task, const simulation::SimulationLayerSummary&)
{
    auto completed = processes_.CompleteDue(task.to);
    if (!completed) return foundation::Result<void>::Failure(completed.GetError());
    return foundation::Result<void>::Success();
}
}
