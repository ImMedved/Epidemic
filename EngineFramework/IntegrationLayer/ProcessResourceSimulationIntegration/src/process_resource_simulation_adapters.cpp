#include "Epidemic/GameFramework/ProcessResourceSimulationIntegration/process_resource_simulation_adapters.h"
#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace epidemic::gameplay::integration
{
namespace
{
constexpr std::uint32_t kResourcePayloadVersion = 1;
constexpr std::uint32_t kReservationPayloadVersion = 1;
constexpr std::size_t kResourcePayloadSize = sizeof(std::uint32_t) + sizeof(std::uint64_t) * 3;
constexpr std::size_t kReservationPayloadSize = sizeof(std::uint32_t) + sizeof(std::uint64_t) * 2;
constexpr std::uint32_t kPreparedOutputPayloadVersion = 1;
constexpr std::size_t kPreparedOutputPayloadSize = sizeof(std::uint32_t) + sizeof(std::uint64_t) * 4;

[[nodiscard]] foundation::Error Error(std::string_view code, std::string_view message) { return foundation::Error::Create(code, message); }

void AppendU32(std::vector<std::byte> &bytes, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        bytes.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xffu));
}
void AppendU64(std::vector<std::byte> &bytes, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        bytes.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xffu));
}
[[nodiscard]] std::optional<std::uint32_t> ReadU32(const std::vector<std::byte> &bytes, std::size_t &offset)
{
    if (bytes.size() - offset < 4)
        return std::nullopt;
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset++])) << (i * 8);
    return value;
}
[[nodiscard]] std::optional<std::uint64_t> ReadU64(const std::vector<std::byte> &bytes, std::size_t &offset)
{
    if (bytes.size() - offset < 8)
        return std::nullopt;
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
        value |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(bytes[offset++])) << (i * 8);
    return value;
}
} // namespace

processes::RegisteredPayload EncodeProcessResourcePayload(ProcessResourcePayload payload)
{
    processes::RegisteredPayload encoded;
    encoded.type = ResourceProcessInputProvider::PayloadType();
    encoded.schema_version = kResourcePayloadVersion;
    encoded.portable = true;
    encoded.bytes.reserve(kResourcePayloadSize);
    AppendU32(encoded.bytes, kResourcePayloadVersion);
    AppendU64(encoded.bytes, payload.stockpile.value.High());
    AppendU64(encoded.bytes, payload.stockpile.value.Low());
    AppendU64(encoded.bytes, payload.resource.value.Raw());
    return encoded;
}

std::optional<ProcessResourcePayload> DecodeProcessResourcePayload(const processes::RegisteredPayload &payload)
{
    if (payload.type != ResourceProcessInputProvider::PayloadType())
        return std::nullopt;

    if (payload.bytes.size() == kResourcePayloadSize)
    {
        std::size_t offset = 0;
        const auto version = ReadU32(payload.bytes, offset);
        if (!version || *version != kResourcePayloadVersion)
            return std::nullopt;
        const auto stockpile_high = ReadU64(payload.bytes, offset);
        const auto stockpile_low = ReadU64(payload.bytes, offset);
        const auto resource_raw = ReadU64(payload.bytes, offset);
        if (!stockpile_high || !stockpile_low || !resource_raw || offset != payload.bytes.size())
            return std::nullopt;
        ProcessResourcePayload decoded;
        decoded.stockpile = resources::ResourceStockpileId{GameplayObjectId::FromRaw(*stockpile_high, *stockpile_low)};
        decoded.resource = resources::ResourceTypeId{TypeId::FromRaw(*resource_raw)};
        if (!decoded.stockpile.IsValid() || !decoded.resource.IsValid())
            return std::nullopt;
        return decoded;
    }

    // Process-local backward compatibility for old bootstrap definitions. Durable payloads must use the codec above.
    return payload.AsTrivial<ProcessResourcePayload>(ResourceProcessInputProvider::PayloadType());
}

processes::RegisteredPayload EncodeProcessResourceReservationPayload(ProcessResourceReservationPayload payload)
{
    processes::RegisteredPayload encoded;
    encoded.type = ResourceProcessInputProvider::ReservationPayloadType();
    encoded.schema_version = kReservationPayloadVersion;
    encoded.portable = true;
    encoded.bytes.reserve(kReservationPayloadSize);
    AppendU32(encoded.bytes, kReservationPayloadVersion);
    AppendU64(encoded.bytes, payload.reservation.value.High());
    AppendU64(encoded.bytes, payload.reservation.value.Low());
    return encoded;
}

std::optional<ProcessResourceReservationPayload> DecodeProcessResourceReservationPayload(
    const processes::RegisteredPayload &payload)
{
    if (payload.type != ResourceProcessInputProvider::ReservationPayloadType())
        return std::nullopt;

    if (payload.bytes.size() == kReservationPayloadSize)
    {
        std::size_t offset = 0;
        const auto version = ReadU32(payload.bytes, offset);
        if (!version || *version != kReservationPayloadVersion)
            return std::nullopt;
        const auto high = ReadU64(payload.bytes, offset);
        const auto low = ReadU64(payload.bytes, offset);
        if (!high || !low || offset != payload.bytes.size())
            return std::nullopt;
        ProcessResourceReservationPayload decoded;
        decoded.reservation = resources::ResourceReservationId{GameplayObjectId::FromRaw(*high, *low)};
        if (!decoded.reservation.IsValid())
            return std::nullopt;
        return decoded;
    }

    // Process-local backward compatibility for old non-durable tokens.
    return payload.AsTrivial<ProcessResourceReservationPayload>(ResourceProcessInputProvider::ReservationPayloadType());
}

foundation::Result<void> ResourceProcessInputProvider::Validate(const processes::ProcessInputDefinition& input, const processes::StartProcessRequest&, processes::ProcessInstanceId)
{
    auto payload = DecodeProcessResourcePayload(input.payload);
    if (!payload)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.resource_payload_invalid", "process input payload is not a resource payload"));
    const auto* stockpile = resources_.FindStockpile(payload->stockpile);
    if (!stockpile)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.resource_stockpile_missing", "process resource stockpile is missing"));
    if (!payload->resource.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.resource_payload_invalid", "process resource type is invalid"));
    return foundation::Result<void>::Success();
}

foundation::Result<processes::ReservedProcessInput> ResourceProcessInputProvider::Reserve(const processes::ProcessInputDefinition& input, const processes::StartProcessRequest& request, processes::ProcessInstanceId instance)
{
    auto payload = DecodeProcessResourcePayload(input.payload);
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
    reservation.provider_token = EncodeProcessResourceReservationPayload(ProcessResourceReservationPayload{resource_reservation.Value()});
    return foundation::Result<processes::ReservedProcessInput>::Success(std::move(reservation));
}
foundation::Result<void> ResourceProcessInputProvider::Consume(const processes::ReservedProcessInput& reservation, GameplayContext context)
{
    auto payload = DecodeProcessResourceReservationPayload(reservation.provider_token);
    if (!payload) return foundation::Result<void>::Failure(Error("gameplay.integration.resource_payload_invalid", "process reservation payload is invalid"));
    return resources_.ConsumeReservation(payload->reservation, context);
}
foundation::Result<void> ResourceProcessInputProvider::Release(const processes::ReservedProcessInput& reservation, GameplayContext context)
{
    auto payload = DecodeProcessResourceReservationPayload(reservation.provider_token);
    if (!payload) return foundation::Result<void>::Failure(Error("gameplay.integration.resource_payload_invalid", "process reservation payload is invalid"));
    const auto* current = resources_.FindReservation(payload->reservation);
    if (!current || current->state != resources::ResourceReservationState::Active) return foundation::Result<void>::Success();
    return resources_.ReleaseReservation(payload->reservation, context);
}
namespace
{
[[nodiscard]] TypeId PreparedOutputPayloadType() noexcept
{
    return TypeId::FromString("framework.process.resource.output.prepared");
}

[[nodiscard]] processes::RegisteredPayload EncodePreparedProcessResourceOutputPayload(
    PreparedProcessResourceOutputPayload payload)
{
    std::vector<std::byte> bytes;
    bytes.reserve(kPreparedOutputPayloadSize);
    AppendU32(bytes, kPreparedOutputPayloadVersion);
    AppendU64(bytes, payload.stockpile.value.High());
    AppendU64(bytes, payload.stockpile.value.Low());
    AppendU64(bytes, payload.resource.value.Raw());
    AppendU64(bytes, static_cast<std::uint64_t>(payload.amount));
    return processes::RegisteredPayload::FromVersioned(PreparedOutputPayloadType(), kPreparedOutputPayloadVersion,
                                                       std::move(bytes));
}

[[nodiscard]] std::optional<PreparedProcessResourceOutputPayload> DecodePreparedProcessResourceOutputPayload(
    const processes::RegisteredPayload& payload)
{
    if (payload.type != PreparedOutputPayloadType() || payload.schema_version != kPreparedOutputPayloadVersion ||
        payload.bytes.size() != kPreparedOutputPayloadSize)
        return std::nullopt;
    std::size_t offset = 0;
    const auto version = ReadU32(payload.bytes, offset);
    const auto stockpile_high = ReadU64(payload.bytes, offset);
    const auto stockpile_low = ReadU64(payload.bytes, offset);
    const auto resource_raw = ReadU64(payload.bytes, offset);
    const auto amount = ReadU64(payload.bytes, offset);
    if (!version || *version != kPreparedOutputPayloadVersion || !stockpile_high || !stockpile_low || !resource_raw ||
        !amount || offset != payload.bytes.size())
        return std::nullopt;
    PreparedProcessResourceOutputPayload decoded;
    decoded.stockpile = resources::ResourceStockpileId{GameplayObjectId::FromRaw(*stockpile_high, *stockpile_low)};
    decoded.resource = resources::ResourceTypeId{TypeId::FromRaw(*resource_raw)};
    decoded.amount = static_cast<processes::Fixed>(*amount);
    if (!decoded.stockpile.IsValid() || !decoded.resource.IsValid())
        return std::nullopt;
    return decoded;
}
} // namespace

foundation::Result<processes::PreparedProcessOutput> ResourceProcessOutputHandler::Prepare(
    const processes::ProcessOutputDefinition& output, const processes::ProcessInstance&, GameplayContext)
{
    auto payload = DecodeProcessResourcePayload(output.payload);
    if (!payload || !resources_.FindStockpile(payload->stockpile) || !payload->resource.IsValid())
        return foundation::Result<processes::PreparedProcessOutput>::Failure(
            Error("gameplay.integration.resource_payload_invalid", "process output payload is not a valid resource destination"));
    processes::PreparedProcessOutput prepared;
    prepared.output = output.id;
    prepared.type = output.type;
    prepared.delivery = output.delivery;
    prepared.provider_token = EncodePreparedProcessResourceOutputPayload(
        PreparedProcessResourceOutputPayload{payload->stockpile, payload->resource, output.amount});
    return foundation::Result<processes::PreparedProcessOutput>::Success(std::move(prepared));
}

foundation::Result<void> ResourceProcessOutputHandler::Commit(const processes::PreparedProcessOutput& output,
                                                              const processes::ProcessInstance&, GameplayContext context)
{
    auto payload = DecodePreparedProcessResourceOutputPayload(output.provider_token);
    if (!payload)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.resource_payload_invalid", "prepared process resource output token is invalid"));
    return resources_.Add(payload->stockpile, {payload->resource, payload->amount}, context);
}

foundation::Result<void> ResourceProcessOutputHandler::Cancel(const processes::PreparedProcessOutput&,
                                                              const processes::ProcessInstance&, GameplayContext)
{
    return foundation::Result<void>::Success();
}
foundation::Result<simulation::SimulationLayerSummary> ProcessesSimulationLayer::Prepare(const simulation::SimulationTask& task)
{
    simulation::SimulationLayerSummary summary;
    summary.layer = StaticLayer();
    summary.revision = {};
    if (!task.area.IsValid())
    {
        summary.state = simulation::SimulationTaskState::Skipped;
        return foundation::Result<simulation::SimulationLayerSummary>::Success(std::move(summary));
    }

    const auto due = processes_.FindDueProcessesForSimulation(task.area, task.from, task.to);
    summary.state = simulation::SimulationTaskState::Completed;
    summary.operations = due.size();
    summary.prepared_operations.reserve(due.size());
    for (const auto &process : due)
    {
        summary.prepared_operations.push_back({process.id.value, process.revision});
        if (summary.revision.value < process.revision.value)
            summary.revision = process.revision;
    }
    return foundation::Result<simulation::SimulationLayerSummary>::Success(std::move(summary));
}
foundation::Result<void> ProcessesSimulationLayer::Commit(const simulation::SimulationTask& task, const simulation::SimulationLayerSummary& summary)
{
    if (summary.state == simulation::SimulationTaskState::Skipped)
        return foundation::Result<void>::Success();
    if (!task.area.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.integration.process_region_missing", "simulation task area is required to commit processes"));
    if (summary.prepared_operations.size() != summary.operations)
        return foundation::Result<void>::Failure(Error("gameplay.integration.process_prepared_token_invalid", "process layer summary has an invalid prepared operation token"));

    std::vector<processes::ProcessInstanceId> process_ids;
    process_ids.reserve(summary.prepared_operations.size());
    for (const auto &operation : summary.prepared_operations)
    {
        if (!operation.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.integration.process_prepared_token_invalid", "process layer prepared operation is invalid"));
        const processes::ProcessInstanceId process_id{operation.operation};
        const auto *process = processes_.FindInstance(process_id);
        if (!process)
            return foundation::Result<void>::Failure(Error("gameplay.integration.process_missing", "prepared process no longer exists"));
        if (process->revision != operation.expected_revision && process->state != processes::ProcessInstanceState::Completed)
            return foundation::Result<void>::Failure(Error("gameplay.integration.process_revision_mismatch", "prepared process revision changed before simulation commit"));
        process_ids.push_back(process_id);
    }

    GameplayContext context;
    context.time = task.to;
    auto completed = processes_.CompletePreparedDueForSimulation(task.area, process_ids, task.to, context);
    if (!completed) return foundation::Result<void>::Failure(completed.GetError());
    return foundation::Result<void>::Success();
}
}
