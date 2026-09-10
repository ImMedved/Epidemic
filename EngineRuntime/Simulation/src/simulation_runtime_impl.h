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
                                public IScheduledSimulationTasks
{
public:
    explicit SimulationRuntime(SimulationOptions options, SimulationDependencies dependencies = {});

    [[nodiscard]] foundation::Result<SimulationJobHandle> SubmitJob(
        std::shared_ptr<ISimulationJob> job,
        SimulationJobDesc metadata) override;
    [[nodiscard]] foundation::Result<void> CancelJob(SimulationJobHandle handle) override;
    [[nodiscard]] foundation::Result<SimulationJobState> GetJobState(SimulationJobHandle handle) const override;
    void SetBudget(const SimulationBudget& budget) override;
    [[nodiscard]] foundation::Result<SimulationTickResult> Tick() override;
    [[nodiscard]] foundation::Result<SimulationTickResult> ProcessMainThreadCommits(std::uint32_t max_jobs = 0) override;
    [[nodiscard]] foundation::Result<void> Shutdown() override;

    [[nodiscard]] foundation::Result<void> SetAttention(RuntimeObjectId object, AttentionScore score) override;
    [[nodiscard]] AttentionScore GetAttention(RuntimeObjectId object) const override;
    [[nodiscard]] foundation::Result<void> SetRegionAttention(RegionId region, AttentionScore score) override;
    [[nodiscard]] AttentionScore GetRegionAttention(RegionId region) const override;
    [[nodiscard]] SimulationZoneState GetRegionZoneState(RegionId region) const override;

    [[nodiscard]] foundation::Result<WorldMemoryEventId> RecordEvent(const WorldMemoryEvent& event) override;
    [[nodiscard]] std::vector<WorldMemoryEvent> QueryEvents(const WorldMemoryQuery& query) const override;
    void ExpireOldEvents(SimulationTime now) override;
    [[nodiscard]] std::size_t ExpireOldEvents(SimulationTime now, std::uint32_t max_events) override;

    [[nodiscard]] foundation::Result<void> RecordFact(const AbstractFact& fact) override;
    [[nodiscard]] std::vector<AbstractFact> QueryFacts(SimulationZoneId zone) const override;
    [[nodiscard]] std::uint64_t Revision() const override;

    [[nodiscard]] foundation::Result<void> Publish(const SimulationProposalBatch& batch) override;
    [[nodiscard]] std::span<const SimulationProposalBatch> PendingBatches() const override;
    [[nodiscard]] foundation::Result<void> CommitNext() override;
    [[nodiscard]] foundation::Result<void> DiscardAll(ProposalDiscardReason reason) override;

    [[nodiscard]] foundation::Result<ScheduledSimulationTaskId> Schedule(ScheduledSimulationTask task) override;
    [[nodiscard]] foundation::Result<void> Cancel(ScheduledSimulationTaskId id) override;
    [[nodiscard]] std::vector<ScheduledSimulationTask> QueryDue(SimulationTime now) const override;
    [[nodiscard]] ScheduledTaskExecutionResult ExecuteDueWithinBudget(SimulationTime now, const SimulationBudget& budget) override;

    void SetJobStateForTesting(SimulationJobHandle handle, SimulationJobState state);
    [[nodiscard]] std::optional<ScheduledSimulationTaskId> ActiveScheduleForTesting(SimulationJobHandle handle) const;
    void SetNextJobIdentityForTesting(std::uint64_t id, std::uint32_t generation) noexcept;
    void SetNextMemoryIdForTesting(std::uint64_t id) noexcept;
    void SetNextTaskIdForTesting(std::uint64_t id) noexcept;
    void SetFactRevisionForTesting(std::uint64_t revision) noexcept;

private:
    struct RegionAttentionRecord
    {
        AttentionScore score{};
        SimulationZoneState zone_state = SimulationZoneState::Dormant;
    };

    enum class ShutdownState
    {
        Running,
        ShuttingDown,
        Shutdown
    };

    struct JobRecord
    {
        SimulationJobDesc desc{};
        SimulationJobHandle handle{};
        SimulationJobState state = SimulationJobState::Pending;
        std::shared_ptr<ISimulationJob> executable{};
        std::optional<SimulationStepResult> pending_main_thread_result{};
        std::optional<ScheduledSimulationTaskId> active_schedule{};
    };

    [[nodiscard]] JobRecord* FindJob(SimulationJobId id);
    [[nodiscard]] const JobRecord* FindJob(SimulationJobId id) const;
    [[nodiscard]] JobRecord* FindJob(SimulationJobHandle handle);
    [[nodiscard]] const JobRecord* FindJob(SimulationJobHandle handle) const;
    [[nodiscard]] std::vector<SimulationJobId> BuildJobWorkList() const;
    [[nodiscard]] std::vector<SimulationJobId> BuildMainThreadWorkList() const;
    [[nodiscard]] std::vector<WorldMemoryEventId> BuildMemoryWorkList() const;
    [[nodiscard]] std::vector<ScheduledSimulationTaskId> BuildTaskWorkList() const;
    [[nodiscard]] foundation::Result<void> ValidateJobMetadata(const SimulationJobDesc& desc) const;
    [[nodiscard]] foundation::Result<void> ValidateProposalBatch(const SimulationProposalBatch& batch) const;
    [[nodiscard]] foundation::Result<void> ValidateStepResult(const JobRecord& job, const SimulationStepResult& result) const;
    void PruneTerminalJobs();
    void PruneExpiredMemoryEvents();
    [[nodiscard]] bool HasPendingProposal(SimulationJobId id) const;
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
    std::unordered_map<RegionId, RegionAttentionRecord> region_attention_;
    std::unordered_map<WorldMemoryEventId, WorldMemoryEvent> memory_events_;
    std::vector<AbstractFact> facts_;
    std::vector<SimulationProposalBatch> proposal_batches_;
    std::unordered_map<ScheduledSimulationTaskId, ScheduledSimulationTask> scheduled_tasks_;
    ShutdownState shutdown_state_ = ShutdownState::Running;
};
} // namespace epidemic::runtime::simulation
