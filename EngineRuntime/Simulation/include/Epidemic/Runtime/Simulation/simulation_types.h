#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Foundation/runtime_budget.h"
#include "Epidemic/Runtime/Foundation/runtime_time.h"
#include "Epidemic/Foundation/error.h"
#include "Epidemic/Foundation/string_id.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace epidemic::runtime::simulation
{
// Public value types for the Simulation major. They describe generic scheduling, relevance,
// memory and effect infrastructure without owning authoritative world state.

struct SimulationJobId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const SimulationJobId&) const noexcept = default;
};

struct WorldMemoryEventId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const WorldMemoryEventId&) const noexcept = default;
};

struct ScheduledSimulationTaskId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const ScheduledSimulationTaskId&) const noexcept = default;
};

struct SimulationJobHandle
{
    SimulationJobId id{};
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return id.IsValid() && generation != 0; }
    [[nodiscard]] constexpr bool operator==(const SimulationJobHandle&) const noexcept = default;
};

enum class SimulationLane
{
    Background,
    Zone,
    Object,
    MainThread
};

using SimulationTime = GameTimePoint;

enum class SimulationZoneState
{
    Dormant,
    Abstract,
    Scheduled,
    Relevant,
    Observed,
    Active
};

enum class SimulationJobState
{
    Pending,
    Scheduled,
    Running,
    PartiallyComplete,
    WaitingForMainThread,
    Completed,
    Failed,
    Cancelled
};

enum class MemoryLifetime
{
    Disposable,
    Temporary,
    Persistent
};

enum class ObservationState
{
    Unobserved,
    Observed,
    PlayerAffected
};

struct AttentionScore
{
    float value = 0.0f;
};

struct SimulationBudget
{
    std::uint32_t max_jobs = 0;
    std::uint32_t max_work_units = 0;
};

struct SimulationJobDesc
{
    SimulationZoneId zone{};
    RuntimeObjectId subject{};
    std::uint32_t work_units = 1;
    SimulationLane lane = SimulationLane::Zone;
    std::uint64_t source_revision = 0;
};

struct SimulationStepInput
{
    SimulationJobHandle job{};
    SimulationZoneId zone{};
    RuntimeObjectId subject{};
    SimulationLane lane = SimulationLane::Zone;
    std::uint32_t work_units = 0;
    std::uint64_t source_revision = 0;
};

struct SimulationProposal
{
    foundation::StringId domain{};
    foundation::StringId kind{};
    RuntimeObjectId target{};
    foundation::StringId schema_id{};
    std::uint32_t schema_version = 0;
    std::vector<std::byte> payload{};
};

struct SimulationProposalBatch
{
    SimulationJobHandle job{};
    SimulationZoneId zone{};
    std::uint64_t source_revision = 0;
    std::vector<SimulationProposal> proposals{};
};

struct SimulationStepResult
{
    SimulationJobState state = SimulationJobState::PartiallyComplete;
    std::uint32_t consumed_work_units = 0;
    SimulationProposalBatch proposals{};
};

struct SimulationTickFailure
{
    SimulationJobHandle job{};
    foundation::Error error{};
};

struct SimulationTickResult
{
    std::size_t processed_jobs = 0;
    std::vector<SimulationTickFailure> failures{};
    std::vector<SimulationProposalBatch> proposal_batches{};
};

struct WorldMemoryEvent
{
    WorldMemoryEventId id{};
    RegionId region{};
    SimulationTime happened_at{};
    GameDuration ttl{};
    MemoryLifetime lifetime = MemoryLifetime::Temporary;
    ObservationState observation = ObservationState::Unobserved;
    bool expired = false;
};

struct AbstractFact
{
    foundation::StringId domain{};
    foundation::StringId kind{};
    std::uint64_t fact_type = 0;
    RuntimeObjectId subject{};
    SimulationZoneId zone{};
    SimulationTime observed_at{};
    std::uint64_t revision = 0;
};

struct ScheduledSimulationTask
{
    ScheduledSimulationTaskId id{};
    SimulationJobHandle job{};
    SimulationTime due_at{};
    SimulationLane lane = SimulationLane::Background;
};

struct ScheduledTaskFailure
{
    ScheduledSimulationTaskId task{};
    SimulationJobHandle job{};
    foundation::Error error{};
};

struct ScheduledTaskExecutionResult
{
    std::size_t activated = 0;
    std::vector<ScheduledTaskFailure> failures{};
};

enum class ProposalDiscardReason
{
    Shutdown,
    AdministrativeReset
};

struct WorldMemoryQuery
{
    RegionId region{};
    bool include_expired = false;
};

struct SimulationOptions
{
    bool enable_budgeted_scheduler = true;
    std::uint32_t max_terminal_jobs = 256;
    std::uint32_t max_memory_events = 1024;
    std::uint32_t max_facts = 1024;
    std::uint32_t max_proposal_batches = 256;
};
} // namespace epidemic::runtime::simulation

namespace std
{
template <> struct hash<epidemic::runtime::simulation::SimulationJobId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::simulation::SimulationJobId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};

template <> struct hash<epidemic::runtime::simulation::WorldMemoryEventId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::simulation::WorldMemoryEventId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};

template <> struct hash<epidemic::runtime::simulation::ScheduledSimulationTaskId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::simulation::ScheduledSimulationTaskId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};
} // namespace std
