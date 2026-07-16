#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Foundation/runtime_time.h"

#include <cstdint>
#include <functional>
#include <string>

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
};

struct WorldMemoryEvent
{
    WorldMemoryEventId id{};
    RegionId region{};
    SimulationTime happened_at{};
    GameDuration ttl{};
    WorldMemoryEventState state = WorldMemoryEventState::Temporary;
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
