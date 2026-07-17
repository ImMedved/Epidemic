#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Foundation/runtime_time.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace epidemic::runtime::simulation
{
// File note:
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
    Running,
    PartiallyComplete,
    WaitingForMainThread,
    Completed,
    Failed,
    Cancelled
};

enum class WorldMemoryEventState
{
    Disposable,
    Temporary,
    Observed,
    PlayerAffected,
    Persistent,
    Expired
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
    bool wait_for_main_thread = false;
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
    RuntimeObjectId target{};
    SimulationZoneId zone{};
    std::uint64_t proposal_type = 0;
};

struct SimulationProposalBatch
{
    SimulationJobHandle source_job{};
    std::uint64_t source_revision = 0;
    std::vector<SimulationProposal> proposals{};
};

struct WorldMemoryEvent
{
    WorldMemoryEventId id{};
    RegionId region{};
    SimulationTime happened_at{};
    GameDuration ttl{};
    WorldMemoryEventState state = WorldMemoryEventState::Temporary;
    MemoryLifetime lifetime = MemoryLifetime::Temporary;
    ObservationState observation = ObservationState::Unobserved;
};

struct AbstractFact
{
    std::uint64_t fact_type = 0;
    RuntimeObjectId subject{};
    SimulationZoneId zone{};
    SimulationTime observed_at{};
};

struct ScheduledSimulationTask
{
    SimulationJobHandle job{};
    SimulationTime due_at{};
    SimulationLane lane = SimulationLane::Background;
};

struct WorldMemoryQuery
{
    RegionId region{};
    bool include_expired = false;
};

struct SimulationEffect
{
    RuntimeObjectId target{};
    SimulationZoneId zone{};
    std::uint64_t effect_type = 0;
};

struct SimulationOptions
{
    bool enable_budgeted_scheduler = true;
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
} // namespace std
