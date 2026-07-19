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

class StreamingRuntime final : public IStreamingRuntime, public IStreamingQuery
{
  public:
    explicit StreamingRuntime(StreamingDependencies dependencies = {},
                              IStreamingPriorityResolver* priority_resolver = nullptr,
                              IResidencyController* residency_controller = nullptr,
                              IStreamingWorldSource* world_source = nullptr,
                              IStreamingPersistenceSource* persistence_source = nullptr,
                              IStreamingResourceSource* resource_source = nullptr);

    [[nodiscard]] foundation::Result<StreamingDemandHandle> Request(const StreamingTarget& target, StreamingPriorityClass priority) override;
    [[nodiscard]] foundation::Result<void> ReleaseDemand(StreamingDemandHandle demand) override;
    [[nodiscard]] foundation::Result<void> CancelRequest(StreamingRequestHandle request) override;
    [[nodiscard]] StreamingState GetChunkState(ChunkId chunk) const override;
    void SetBudget(const StreamingBudget& budget) override;
    [[nodiscard]] StreamingTickResult Tick() override;
    [[nodiscard]] std::optional<StreamingProgress> GetProgress(StreamingRequestHandle request) const override;
    [[nodiscard]] StreamingStatistics GetStatistics() const override;

    [[nodiscard]] std::size_t RecordCount() const noexcept;
    void CleanupCompletedRecords(std::size_t max_history);

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
        std::uint64_t completion_sequence = 0;
        std::size_t processed_bytes = 0;
        std::unordered_map<StreamingDemandId, DemandRecord> demands;
    };

    [[nodiscard]] static bool IsTerminal(StreamingState state);
    [[nodiscard]] static bool IsLiveWork(StreamingState state);
    [[nodiscard]] static int PriorityRank(StreamingPriorityClass priority);
    [[nodiscard]] static std::optional<ChunkId> GetChunkTarget(const StreamingTarget& target);
    static void SetState(RequestRecord& record, StreamingState state, float progress);

    [[nodiscard]] RequestRecord* FindRequest(StreamingRequestId id);
    [[nodiscard]] const RequestRecord* FindRequest(StreamingRequestId id) const;
    [[nodiscard]] RequestRecord* FindByDemand(StreamingDemandHandle demand);
    [[nodiscard]] const RequestRecord* FindByDemand(StreamingDemandHandle demand) const;
    [[nodiscard]] std::vector<StreamingRequestId> BuildWorkList() const;
    [[nodiscard]] foundation::Result<void> AdvanceRequest(RequestRecord& record);
    [[nodiscard]] foundation::Result<void> ExecutePlanStep(RequestRecord& record);
    [[nodiscard]] foundation::Result<void> Rollback(RequestRecord& record);
    [[nodiscard]] foundation::Result<void> BeginUnload(RequestRecord& record);
    void UpdatePriority(RequestRecord& record);
    void CleanupHistoryIfNeeded();

    StreamingDependencies dependencies_{};
    IStreamingPriorityResolver* priority_resolver_ = nullptr;
    IResidencyController* residency_controller_ = nullptr;
    IStreamingWorldSource* world_source_ = nullptr;
    IStreamingPersistenceSource* persistence_source_ = nullptr;
    IStreamingResourceSource* resource_source_ = nullptr;

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
};
} // namespace epidemic::runtime::streaming
