#pragma once

#include "Epidemic/Runtime/Simulation/simulation_runtime.h"

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace epidemic::runtime::simulation
{
// Internal in-memory Simulation runtime. It models scheduling, memory and effect queues without owning world data.

class SimulationRuntime final : public ISimulationRuntime,
                                public IAttentionSystem,
                                public IWorldMemory,
                                public IAbstractFactStore,
                                public ISimulationProposalQueue,
                                public IScheduledSimulationTasks,
                                public IEffectBuffer
{
public:
    explicit SimulationRuntime(SimulationOptions options, SimulationDependencies dependencies = {});

    [[nodiscard]] foundation::Result<SimulationJobId> SubmitJob(const SimulationJobDesc& desc) override;
    [[nodiscard]] foundation::Result<SimulationJobHandle> SubmitJobHandle(const SimulationJobDesc& desc) override;
    [[nodiscard]] foundation::Result<void> CancelJob(SimulationJobId id) override;
    [[nodiscard]] foundation::Result<void> CancelJob(SimulationJobHandle handle) override;
    [[nodiscard]] SimulationJobState GetJobState(SimulationJobId id) const override;
    void SetBudget(const SimulationBudget& budget) override;
    [[nodiscard]] std::size_t Tick() override;

    [[nodiscard]] foundation::Result<void> SetAttention(RuntimeObjectId object, AttentionScore score) override;
    [[nodiscard]] AttentionScore GetAttention(RuntimeObjectId object) const override;
    [[nodiscard]] foundation::Result<void> SetRegionAttention(RegionId region, AttentionScore score) override;
    [[nodiscard]] AttentionScore GetRegionAttention(RegionId region) const override;

    [[nodiscard]] foundation::Result<WorldMemoryEventId> RecordEvent(const WorldMemoryEvent& event) override;
    [[nodiscard]] std::vector<WorldMemoryEvent> QueryEvents(const WorldMemoryQuery& query) const override;
    void ExpireOldEvents(SimulationTime now) override;
    [[nodiscard]] std::size_t ExpireOldEvents(SimulationTime now, std::uint32_t max_events) override;

    [[nodiscard]] foundation::Result<void> Submit(const SimulationEffect& effect) override;
    [[nodiscard]] std::span<const SimulationEffect> Effects() const override;
    void Clear() override;

    [[nodiscard]] foundation::Result<void> RecordFact(const AbstractFact& fact) override;
    [[nodiscard]] std::vector<AbstractFact> QueryFacts(SimulationZoneId zone) const override;
    [[nodiscard]] std::uint64_t Revision() const override;

    [[nodiscard]] foundation::Result<void> Publish(const SimulationProposalBatch& batch) override;
    [[nodiscard]] std::span<const SimulationProposalBatch> PendingBatches() const override;
    [[nodiscard]] foundation::Result<void> CommitNext() override;

    [[nodiscard]] foundation::Result<ScheduledSimulationTaskId> Schedule(ScheduledSimulationTask task) override;
    [[nodiscard]] foundation::Result<void> Cancel(ScheduledSimulationTaskId id) override;
    [[nodiscard]] std::vector<ScheduledSimulationTask> QueryDue(SimulationTime now) const override;
    [[nodiscard]] std::size_t ExecuteDueWithinBudget(SimulationTime now, const SimulationBudget& budget) override;

private:
    struct JobRecord
    {
        SimulationJobDesc desc{};
        SimulationJobHandle handle{};
        SimulationJobState state = SimulationJobState::Pending;
        std::unique_ptr<ISimulationJob> executable{};
    };

    [[nodiscard]] JobRecord* FindJob(SimulationJobId id);
    [[nodiscard]] const JobRecord* FindJob(SimulationJobId id) const;
    [[nodiscard]] std::vector<SimulationJobId> BuildJobWorkList() const;
    [[nodiscard]] std::vector<WorldMemoryEventId> BuildMemoryWorkList() const;
    [[nodiscard]] std::vector<ScheduledSimulationTaskId> BuildTaskWorkList() const;
    [[nodiscard]] bool IsTerminal(SimulationJobState state) const noexcept;
    [[nodiscard]] RuntimeBudget ToRuntimeBudget(std::uint32_t work_units) const noexcept;

    SimulationOptions options_{};
    SimulationDependencies dependencies_{};
    SimulationBudget budget_{1, 1};
    std::uint64_t next_job_value_ = 1;
    std::uint32_t next_job_generation_ = 1;
    std::uint64_t next_memory_value_ = 1;
    std::uint64_t next_task_value_ = 1;
    std::uint64_t fact_revision_ = 0;
    std::unordered_map<SimulationJobId, JobRecord> jobs_;
    std::unordered_map<RuntimeObjectId, AttentionScore> object_attention_;
    std::unordered_map<RegionId, AttentionScore> region_attention_;
    std::unordered_map<WorldMemoryEventId, WorldMemoryEvent> memory_events_;
    std::vector<AbstractFact> facts_;
    std::vector<SimulationProposalBatch> proposal_batches_;
    std::unordered_map<ScheduledSimulationTaskId, ScheduledSimulationTask> scheduled_tasks_;
    std::vector<SimulationEffect> effects_;
};
} // namespace epidemic::runtime::simulation
