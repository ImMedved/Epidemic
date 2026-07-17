#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Simulation/simulation_types.h"

#include <memory>
#include <span>
#include <cstdint>
#include <vector>

namespace epidemic::runtime::simulation
{
// File note:
// Public Simulation contracts. These interfaces expose scheduling, attention, world-memory and effect buffering
// as infrastructure, leaving authoritative state changes to downstream resolvers/composition code.

class ISimulationRuntime
{
public:
    virtual ~ISimulationRuntime() = default;

    // Function note: Submits a generic simulation job.
    // Inputs: zone/subject/work descriptor; outputs: job id or validation error.
    // Relations: Tick advances job state under SimulationBudget.
    [[nodiscard]] virtual foundation::Result<SimulationJobId> SubmitJob(const SimulationJobDesc& desc) = 0;
    [[nodiscard]] virtual foundation::Result<SimulationJobHandle> SubmitJobHandle(const SimulationJobDesc& desc) = 0;

    // Function note: Cancels a non-terminal job.
    // Inputs: job id; outputs: success/failure Result.
    // Relations: cancelled jobs remain queryable through GetJobState.
    [[nodiscard]] virtual foundation::Result<void> CancelJob(SimulationJobId id) = 0;
    [[nodiscard]] virtual foundation::Result<void> CancelJob(SimulationJobHandle handle) = 0;

    // Function note: Reads the lifecycle state of a simulation job.
    // Inputs: job id; outputs: Failed for unknown ids.
    // Relations: status query for scheduler callers.
    [[nodiscard]] virtual SimulationJobState GetJobState(SimulationJobId id) const = 0;

    // Function note: Updates scheduler budget.
    // Inputs: job and work-unit limits; outputs: none.
    // Relations: Tick consumes this budget and avoids blocking the frame.
    virtual void SetBudget(const SimulationBudget& budget) = 0;

    // Function note: Advances queued jobs according to the current budget.
    // Inputs: none; outputs: number of jobs that changed state.
    // Relations: transitions jobs through Running, PartiallyComplete, WaitingForMainThread or Completed.
    [[nodiscard]] virtual std::size_t Tick() = 0;
};

class IAttentionSystem
{
public:
    virtual ~IAttentionSystem() = default;

    // Function note: Writes relevance score for an object.
    // Inputs: object id and score; outputs: success/failure Result.
    // Relations: generic relevance data that other systems can read without owning scheduler state.
    [[nodiscard]] virtual foundation::Result<void> SetAttention(RuntimeObjectId object, AttentionScore score) = 0;

    // Function note: Reads relevance score for an object.
    // Inputs: object id; outputs: score, default zero when absent.
    // Relations: pairs with SetAttention for scheduler heuristics.
    [[nodiscard]] virtual AttentionScore GetAttention(RuntimeObjectId object) const = 0;

    // Function note: Writes relevance score for a region.
    // Inputs: region id and score; outputs: success/failure Result.
    // Relations: allows budget decisions per area without owning region data.
    [[nodiscard]] virtual foundation::Result<void> SetRegionAttention(RegionId region, AttentionScore score) = 0;

    // Function note: Reads relevance score for a region.
    // Inputs: region id; outputs: score, default zero when absent.
    // Relations: pairs with SetRegionAttention.
    [[nodiscard]] virtual AttentionScore GetRegionAttention(RegionId region) const = 0;
};

class IWorldMemory
{
public:
    virtual ~IWorldMemory() = default;

    // Function note: Records a memory event snapshot.
    // Inputs: event with region/time/TTL/state; outputs: event id or validation error.
    // Relations: QueryEvents and ExpireOldEvents observe and update this in-memory store.
    [[nodiscard]] virtual foundation::Result<WorldMemoryEventId> RecordEvent(const WorldMemoryEvent& event) = 0;

    // Function note: Queries memory events by region and expiration policy.
    // Inputs: query; outputs: matching memory event snapshots.
    // Relations: does not mutate stored events.
    [[nodiscard]] virtual std::vector<WorldMemoryEvent> QueryEvents(const WorldMemoryQuery& query) const = 0;

    // Function note: Expires events whose TTL elapsed.
    // Inputs: current game time; outputs: none.
    // Relations: mutates event state to Expired but keeps records inspectable.
    virtual void ExpireOldEvents(SimulationTime now) = 0;

    // Function note: Expires at most max_events matching memory records.
    // Inputs: current game time and item budget; outputs: number of records expired.
    // Relations: keeps memory expiration budgeted for frame orchestration.
    [[nodiscard]] virtual std::size_t ExpireOldEvents(SimulationTime now, std::uint32_t max_events) = 0;
};

class IRelevancePolicy
{
public:
    virtual ~IRelevancePolicy() = default;

    [[nodiscard]] virtual SimulationZoneState ClassifyZone(RegionId region, AttentionScore attention) const = 0;
};

class ISimulationJob
{
public:
    virtual ~ISimulationJob() = default;

    [[nodiscard]] virtual foundation::Result<SimulationProposalBatch> ExecuteStep(const SimulationStepInput& input) = 0;
};

class ISimulationCommitTarget
{
public:
    virtual ~ISimulationCommitTarget() = default;

    [[nodiscard]] virtual foundation::Result<void> Commit(const SimulationProposalBatch& batch) = 0;
};

class IAbstractFactStore
{
public:
    virtual ~IAbstractFactStore() = default;

    [[nodiscard]] virtual foundation::Result<void> RecordFact(const AbstractFact& fact) = 0;
    [[nodiscard]] virtual std::vector<AbstractFact> QueryFacts(SimulationZoneId zone) const = 0;
};

class IEffectBuffer
{
public:
    virtual ~IEffectBuffer() = default;

    // Function note: Appends an opaque simulation effect.
    // Inputs: target/zone/type payload; outputs: success/failure Result.
    // Relations: consumers resolve effects later in explicit order.
    [[nodiscard]] virtual foundation::Result<void> Submit(const SimulationEffect& effect) = 0;

    // Function note: Exposes effects in submission order.
    // Inputs: none; outputs: immutable span over buffered effects.
    // Relations: keeps effect application outside Simulation ownership.
    [[nodiscard]] virtual std::span<const SimulationEffect> Effects() const = 0;

    // Function note: Clears submitted effects after resolution.
    // Inputs: none; outputs: none.
    // Relations: called by composition/resolver code after processing.
    virtual void Clear() = 0;
};

[[nodiscard]] std::unique_ptr<class SimulationRuntime> CreateSimulationRuntime(SimulationOptions options = {});

struct SimulationServices
{
    std::shared_ptr<ISimulationRuntime> runtime;
    std::shared_ptr<IAttentionSystem> attention;
    std::shared_ptr<IWorldMemory> memory;
    std::shared_ptr<IEffectBuffer> effects;
};

[[nodiscard]] SimulationServices CreateSimulationServices(SimulationOptions options = {});
} // namespace epidemic::runtime::simulation
