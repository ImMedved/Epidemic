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
// File note:
// In-memory Streaming implementation used for deterministic tests and early runtime
// integration. It advances requests one state at a time under a configurable budget.

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
    explicit StreamingRuntime(
        IStreamingPriorityResolver* priority_resolver = nullptr,
        IResidencyController* residency_controller = nullptr,
        IStreamingWorldSource* world_source = nullptr,
        IStreamingPersistenceSource* persistence_source = nullptr,
        IStreamingResourceSource* resource_source = nullptr);

    [[nodiscard]] foundation::Result<StreamingRequestId> RequestChunk(
        ChunkId chunk,
        StreamingPriorityClass priority) override;
    [[nodiscard]] foundation::Result<StreamingRequestHandle> RequestTarget(
        const StreamingTarget& target,
        StreamingPriorityClass priority,
        std::uint32_t demand_count) override;
    [[nodiscard]] foundation::Result<void> CancelRequest(StreamingRequestId request) override;
    [[nodiscard]] foundation::Result<void> CancelRequest(StreamingRequestHandle request) override;
    [[nodiscard]] StreamingState GetChunkState(ChunkId chunk) const override;
    void SetBudget(const StreamingBudget& budget) override;
    void Tick() override;
    [[nodiscard]] std::optional<StreamingProgress> GetProgress(StreamingRequestId request) const override;
    [[nodiscard]] std::optional<StreamingProgress> GetProgress(StreamingRequestHandle request) const override;
    [[nodiscard]] StreamingStatistics GetStatistics() const override;

  private:
    struct RequestRecord
    {
        StreamingRequest request{};
        StreamingState state = StreamingState::NotRequested;
        float progress = 0.0f;
        std::uint64_t revision = 0;
    };

    [[nodiscard]] static bool IsTerminal(StreamingState state);
    [[nodiscard]] static int PriorityRank(StreamingPriorityClass priority);
    [[nodiscard]] static std::optional<ChunkId> GetChunkTarget(const StreamingTarget& target);
    static void SetState(RequestRecord& record, StreamingState state, float progress);

    [[nodiscard]] RequestRecord* FindRequest(StreamingRequestId id);
    [[nodiscard]] const RequestRecord* FindRequest(StreamingRequestId id) const;
    [[nodiscard]] std::vector<StreamingRequestId> BuildWorkList() const;
    [[nodiscard]] foundation::Result<void> AdvanceRequest(RequestRecord& record);
    [[nodiscard]] foundation::Result<void> AdvanceActivation(RequestRecord& record);
    [[nodiscard]] foundation::Result<void> AdvanceUnload(RequestRecord& record);

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
    std::uint32_t next_generation_ = 1;
    StreamingStatistics statistics_{};
};
} // namespace epidemic::runtime::streaming
