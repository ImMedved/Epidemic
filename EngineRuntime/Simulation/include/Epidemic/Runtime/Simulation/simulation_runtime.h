#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Simulation/simulation_types.h"

#include <memory>
#include <span>
#include <cstdint>
#include <vector>

namespace epidemic::runtime::simulation
{
// Public Simulation contracts. These interfaces expose scheduling, attention, world-memory and effect buffering
// as infrastructure, leaving authoritative state changes to downstream resolvers/composition code.

class ISimulationJob;

class ISimulationScheduler
{
public:
    virtual ~ISimulationScheduler() = default;

    [[nodiscard]] virtual foundation::Result<SimulationJobHandle> SubmitJob(
        std::shared_ptr<ISimulationJob> job,
        SimulationJobDesc metadata) = 0;

    [[nodiscard]] virtual foundation::Result<void> CancelJob(SimulationJobHandle handle) = 0;

    [[nodiscard]] virtual foundation::Result<SimulationJobState> GetJobState(SimulationJobHandle handle) const = 0;

    virtual void SetBudget(const SimulationBudget& budget) = 0;

    [[nodiscard]] virtual foundation::Result<SimulationTickResult> Tick() = 0;
    [[nodiscard]] virtual foundation::Result<SimulationTickResult> ProcessMainThreadCommits(std::uint32_t max_jobs = 0) = 0;
    [[nodiscard]] virtual foundation::Result<void> Shutdown() = 0;
};

class ISimulationRuntime : public ISimulationScheduler
{
public:
    ~ISimulationRuntime() override = default;
};

class IAttentionSystem
{
public:
    virtual ~IAttentionSystem() = default;

    [[nodiscard]] virtual foundation::Result<void> SetAttention(RuntimeObjectId object, AttentionScore score) = 0;

    [[nodiscard]] virtual AttentionScore GetAttention(RuntimeObjectId object) const = 0;

    [[nodiscard]] virtual foundation::Result<void> SetRegionAttention(RegionId region, AttentionScore score) = 0;

    [[nodiscard]] virtual AttentionScore GetRegionAttention(RegionId region) const = 0;

    [[nodiscard]] virtual SimulationZoneState GetRegionZoneState(RegionId region) const = 0;
};

class IWorldMemory
{
public:
    virtual ~IWorldMemory() = default;

    [[nodiscard]] virtual foundation::Result<WorldMemoryEventId> RecordEvent(const WorldMemoryEvent& event) = 0;

    [[nodiscard]] virtual std::vector<WorldMemoryEvent> QueryEvents(const WorldMemoryQuery& query) const = 0;

    virtual void ExpireOldEvents(SimulationTime now) = 0;

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

    [[nodiscard]] virtual foundation::Result<SimulationStepResult> ExecuteStep(const RuntimeBudget& budget) = 0;
    [[nodiscard]] virtual foundation::Result<void> Cancel() = 0;
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
    [[nodiscard]] virtual std::uint64_t Revision() const = 0;
};

class ISimulationProposalQueue
{
public:
    virtual ~ISimulationProposalQueue() = default;

    [[nodiscard]] virtual foundation::Result<void> Publish(const SimulationProposalBatch& batch) = 0;
    [[nodiscard]] virtual std::span<const SimulationProposalBatch> PendingBatches() const = 0;
    [[nodiscard]] virtual foundation::Result<void> CommitNext() = 0;
    [[nodiscard]] virtual foundation::Result<void> DiscardAll(ProposalDiscardReason reason) = 0;
};

class IScheduledSimulationTasks
{
public:
    virtual ~IScheduledSimulationTasks() = default;

    [[nodiscard]] virtual foundation::Result<ScheduledSimulationTaskId> Schedule(ScheduledSimulationTask task) = 0;
    [[nodiscard]] virtual foundation::Result<void> Cancel(ScheduledSimulationTaskId id) = 0;
    [[nodiscard]] virtual std::vector<ScheduledSimulationTask> QueryDue(SimulationTime now) const = 0;
    [[nodiscard]] virtual ScheduledTaskExecutionResult ExecuteDueWithinBudget(SimulationTime now, const SimulationBudget& budget) = 0;
};

class ISimulationClock
{
public:
    virtual ~ISimulationClock() = default;

    [[nodiscard]] virtual SimulationTime Now() const = 0;
};

struct SimulationDependencies
{
    std::shared_ptr<IRelevancePolicy> relevance;
    std::shared_ptr<ISimulationCommitTarget> commit_target;
    std::shared_ptr<ISimulationClock> clock;
};

struct SimulationServices
{
    std::shared_ptr<ISimulationRuntime> runtime;
    std::shared_ptr<ISimulationScheduler> scheduler;
    std::shared_ptr<IWorldMemory> memory;
    std::shared_ptr<IAbstractFactStore> facts;
    std::shared_ptr<ISimulationProposalQueue> proposals;
    std::shared_ptr<IScheduledSimulationTasks> scheduled_tasks;
    std::shared_ptr<IAttentionSystem> attention;
};

[[nodiscard]] foundation::Result<SimulationServices> CreateSimulationServices(
    SimulationOptions options = {},
    SimulationDependencies dependencies = {});
} // namespace epidemic::runtime::simulation
