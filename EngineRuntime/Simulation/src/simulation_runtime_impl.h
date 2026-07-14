#pragma once

#include "Epidemic/Runtime/Simulation/simulation_runtime.h"

#include <unordered_map>
#include <vector>

namespace epidemic::runtime::simulation
{
// File note:
// Internal in-memory Simulation runtime. It models scheduling, memory and effect queues without owning world data.

class SimulationRuntime final : public ISimulationRuntime,
                                public IAttentionSystem,
                                public IWorldMemory,
                                public IEffectBuffer
{
public:
    explicit SimulationRuntime(SimulationOptions options);

    [[nodiscard]] foundation::Result<SimulationJobId> SubmitJob(const SimulationJobDesc& desc) override;
    [[nodiscard]] foundation::Result<void> CancelJob(SimulationJobId id) override;
    [[nodiscard]] SimulationJobState GetJobState(SimulationJobId id) const override;
    void SetBudget(const SimulationBudget& budget) override;
    [[nodiscard]] std::size_t Tick() override;

    [[nodiscard]] foundation::Result<void> SetAttention(RuntimeObjectId object, AttentionScore score) override;
    [[nodiscard]] AttentionScore GetAttention(RuntimeObjectId object) const override;
    [[nodiscard]] foundation::Result<void> SetRegionAttention(RegionId region, AttentionScore score) override;
    [[nodiscard]] AttentionScore GetRegionAttention(RegionId region) const override;

    [[nodiscard]] foundation::Result<WorldMemoryEventId> RecordEvent(const WorldMemoryEvent& event) override;
    [[nodiscard]] std::vector<WorldMemoryEvent> QueryEvents(const WorldMemoryQuery& query) const override;
    void ExpireOldEvents(GameTime now) override;

    [[nodiscard]] foundation::Result<void> Submit(const SimulationEffect& effect) override;
    [[nodiscard]] std::span<const SimulationEffect> Effects() const override;
    void Clear() override;

private:
    struct JobRecord
    {
        SimulationJobDesc desc{};
        SimulationJobState state = SimulationJobState::Pending;
        std::uint32_t remaining_work_units = 0;
    };

    [[nodiscard]] JobRecord* FindJob(SimulationJobId id);
    [[nodiscard]] const JobRecord* FindJob(SimulationJobId id) const;
    [[nodiscard]] bool IsTerminal(SimulationJobState state) const noexcept;

    SimulationOptions options_{};
    SimulationBudget budget_{1, 1};
    std::uint64_t next_job_value_ = 1;
    std::uint64_t next_memory_value_ = 1;
    std::unordered_map<SimulationJobId, JobRecord> jobs_;
    std::unordered_map<RuntimeObjectId, AttentionScore> object_attention_;
    std::unordered_map<RegionId, AttentionScore> region_attention_;
    std::unordered_map<WorldMemoryEventId, WorldMemoryEvent> memory_events_;
    std::vector<SimulationEffect> effects_;
};
} // namespace epidemic::runtime::simulation
