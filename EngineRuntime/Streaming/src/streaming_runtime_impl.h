#pragma once

#include "Epidemic/Runtime/Streaming/residency_controller.h"
#include "Epidemic/Runtime/Streaming/streaming_priority_resolver.h"
#include "Epidemic/Runtime/Streaming/streaming_runtime.h"
#include "Epidemic/Runtime/Streaming/streaming_sources.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace epidemic::runtime::streaming
{
class InMemoryResidencyController final : public IResidencyController
{
  public:
    [[nodiscard]] foundation::Result<void> ActivateChunk(ChunkId chunk) override;
    [[nodiscard]] foundation::Result<void> DeactivateChunk(ChunkId chunk) override;
    [[nodiscard]] foundation::Result<void> UnloadChunk(ChunkId chunk) override;

    [[nodiscard]] StreamingState GetResidencyState(ChunkId chunk) const;

  private:
    std::unordered_map<ChunkId, StreamingState> chunk_states_;
};

class StreamingRuntime final : public IStreamingRuntime, public IStreamingQuery, public IStreamingController
{
  public:
    explicit StreamingRuntime(StreamingDependencies dependencies = {},
                              IStreamingPriorityResolver* priority_resolver = nullptr);

    [[nodiscard]] foundation::Result<StreamingDemandHandle> Request(const StreamingTarget& target, StreamingPriorityClass priority) override;
    [[nodiscard]] foundation::Result<void> ReleaseDemand(StreamingDemandHandle demand) override;
    [[nodiscard]] foundation::Result<void> CancelRequest(StreamingRequestHandle request) override;
    [[nodiscard]] foundation::Result<void> Shutdown() override;
    [[nodiscard]] StreamingState GetChunkState(ChunkId chunk) const override;
    void SetBudget(const StreamingBudget& budget) override;
    [[nodiscard]] StreamingTickResult Tick() override;
    [[nodiscard]] std::optional<StreamingProgress> GetProgress(StreamingRequestHandle request) const override;
    [[nodiscard]] StreamingStatistics GetStatistics() const override;

    [[nodiscard]] std::size_t RecordCount() const noexcept;
    void CleanupCompletedRecords(std::size_t max_history);

    // Internal deterministic seams used by the Streaming contract tests.
    void SetNextRequestValueForTesting(std::uint64_t value) noexcept { next_request_value_ = value; }
    void SetNextDemandValueForTesting(std::uint64_t value) noexcept { next_demand_value_ = value; }
    void SetNextRequestGenerationForTesting(std::uint32_t value) noexcept { next_request_generation_ = value; }
    void SetNextDemandGenerationForTesting(std::uint32_t value) noexcept { next_demand_generation_ = value; }
    void SetNextCompletionSequenceForTesting(std::uint64_t value) noexcept { next_completion_sequence_ = value; }
    bool SetRevisionForTesting(StreamingRequestHandle handle, std::uint64_t value) noexcept;
    [[nodiscard]] std::uint64_t NextRequestValueForTesting() const noexcept { return next_request_value_; }
    [[nodiscard]] std::uint64_t NextDemandValueForTesting() const noexcept { return next_demand_value_; }
    [[nodiscard]] bool IsShuttingDownForTesting() const noexcept;
    void FailNextRequestPublicationForTesting() noexcept { fail_next_request_publication_for_testing_ = true; }
    void FailNextDemandPublicationForTesting() noexcept { fail_next_demand_publication_for_testing_ = true; }
    [[nodiscard]] const StreamingBudget& BudgetForTesting() const noexcept { return budget_; }
    void SetStatisticsForTesting(StreamingStatistics value) noexcept { statistics_ = value; }

  private:
    struct DemandRecord
    {
        StreamingDemandHandle handle{};
        StreamingPriorityClass priority = StreamingPriorityClass::Normal;
        bool active = true;
    };

    struct RequestRecord
    {
        StreamingRequest request{};
        StreamingState state = StreamingState::NotRequested;
        float progress = 0.0f;
        std::uint64_t revision = 0;
        std::uint32_t active_demands = 0;
        StreamingPriorityClass max_priority = StreamingPriorityClass::Normal;
        bool commit_completed = false;
        bool rollback_completed = false;
        bool activation_completed = false;
        bool deactivation_completed = false;
        bool resources_released = false;
        bool residency_unloaded = false;
        bool cancellation_counted = false;
        std::uint64_t completion_sequence = 0;
        std::size_t processed_bytes = 0;
        std::optional<StreamingRequestId> predecessor;
        std::optional<StreamingRequestId> successor;
        std::unordered_map<StreamingDemandId, DemandRecord> demands;
    };

    [[nodiscard]] static bool IsTerminal(StreamingState state);
    [[nodiscard]] static bool IsLiveWork(StreamingState state);
    [[nodiscard]] static int PriorityRank(StreamingPriorityClass priority);
    [[nodiscard]] static bool IsValidPriority(StreamingPriorityClass priority) noexcept;
    [[nodiscard]] static bool IsValidPlanStep(StreamingPlanStep step) noexcept;
    [[nodiscard]] static foundation::Result<void> ValidatePlan(const ProgressiveLoadPlan& plan);
    [[nodiscard]] static std::optional<ChunkId> GetChunkTarget(const StreamingTarget& target);
    [[nodiscard]] static foundation::Result<void> ReserveRevision(const RequestRecord& record, std::uint64_t count = 1);
    static void CommitState(RequestRecord& record, StreamingState state, float progress) noexcept;
    static void SaturatingIncrement(std::uint64_t& counter) noexcept;

    [[nodiscard]] RequestRecord* FindRequest(StreamingRequestId id);
    [[nodiscard]] const RequestRecord* FindRequest(StreamingRequestId id) const;
    [[nodiscard]] RequestRecord* FindByDemand(StreamingDemandHandle demand);
    [[nodiscard]] const RequestRecord* FindByDemand(StreamingDemandHandle demand) const;
    [[nodiscard]] std::vector<StreamingRequestId> BuildWorkList() const;
    [[nodiscard]] foundation::Result<void> AdvanceRequest(RequestRecord& record, RuntimeBudget available_budget);
    [[nodiscard]] foundation::Result<void> ExecutePlanStep(RequestRecord& record, RuntimeBudget available_budget);
    [[nodiscard]] foundation::Result<void> Rollback(RequestRecord& record, StreamingState terminal_state);
    [[nodiscard]] foundation::Result<void> ReleaseLastDemand(RequestRecord& record, StreamingDemandHandle demand);
    [[nodiscard]] foundation::Result<void> BeginUnload(RequestRecord& record);
    struct PreparedTerminalTransition
    {
        std::uint64_t completion_sequence = 0;
    };
    [[nodiscard]] foundation::Result<PreparedTerminalTransition> PrepareTerminalTransition(const RequestRecord& record) const;
    void CommitTerminal(RequestRecord& record, StreamingState state, PreparedTerminalTransition prepared) noexcept;
    [[nodiscard]] foundation::Result<void> CancelWaitingForPredecessor(RequestRecord& record);
    void ClearGraphLinks(RequestRecord& record);
    [[nodiscard]] IResidencyController* ResidencyController() const noexcept;
    [[nodiscard]] IStreamingWorldSource* WorldSource() const noexcept;
    [[nodiscard]] IStreamingPersistenceSource* PersistenceSource() const noexcept;
    [[nodiscard]] IStreamingResourceSource* ResourceSource() const noexcept;
    void UpdatePriority(RequestRecord& record) noexcept;
    void MarkCancellationAccepted(RequestRecord& record) noexcept;
    void PublishChunkStateIfCurrent(const RequestRecord& record) noexcept;
    void CleanupHistoryIfNeeded();

    StreamingDependencies dependencies_{};
    IStreamingPriorityResolver* priority_resolver_ = nullptr;

    StreamingBudget budget_{};
    std::unordered_map<StreamingRequestId, RequestRecord> requests_;
    std::unordered_map<ChunkId, StreamingRequestId> chunk_to_request_;
    std::unordered_map<ChunkId, StreamingState> chunk_states_;
    std::uint64_t next_request_value_ = 1;
    std::uint64_t next_demand_value_ = 1;
    std::uint32_t next_request_generation_ = 1;
    std::uint32_t next_demand_generation_ = 1;
    std::uint64_t next_completion_sequence_ = 1;
    std::size_t history_limit_ = 64;
    StreamingStatistics statistics_{};
    enum class Lifecycle
    {
        Running,
        ShuttingDown,
        Shutdown
    };
    Lifecycle lifecycle_ = Lifecycle::Running;
    bool fail_next_request_publication_for_testing_ = false;
    bool fail_next_demand_publication_for_testing_ = false;
};
} // namespace epidemic::runtime::streaming
