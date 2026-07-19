#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Streaming/streaming_sources.h"
#include "Epidemic/Runtime/Streaming/streaming_types.h"

#include <memory>
#include <optional>

namespace epidemic::runtime::streaming
{
// Public Streaming runtime contract. The implementation coordinates chunk residency
// requests and budgeted state transitions without owning world or renderer data.
class IStreamingRuntime
{
  public:
    virtual ~IStreamingRuntime() = default;

    [[nodiscard]] virtual foundation::Result<StreamingDemandHandle> Request(
        const StreamingTarget& target,
        StreamingPriorityClass priority) = 0;
    [[nodiscard]] virtual foundation::Result<void> ReleaseDemand(StreamingDemandHandle demand) = 0;
    [[nodiscard]] virtual foundation::Result<void> CancelRequest(StreamingRequestHandle request) = 0;
    [[nodiscard]] virtual StreamingState GetChunkState(ChunkId chunk) const = 0;
    virtual void SetBudget(const StreamingBudget& budget) = 0;
    [[nodiscard]] virtual StreamingTickResult Tick() = 0;
    [[nodiscard]] virtual std::optional<StreamingProgress> GetProgress(StreamingRequestHandle request) const = 0;
    [[nodiscard]] virtual StreamingStatistics GetStatistics() const = 0;
};

class IStreamingQuery
{
  public:
    virtual ~IStreamingQuery() = default;

    [[nodiscard]] virtual StreamingState GetChunkState(ChunkId chunk) const = 0;
    [[nodiscard]] virtual std::optional<StreamingProgress> GetProgress(StreamingRequestHandle request) const = 0;
    [[nodiscard]] virtual StreamingStatistics GetStatistics() const = 0;
};

struct StreamingServices
{
    std::shared_ptr<IStreamingRuntime> runtime;
    std::shared_ptr<IStreamingQuery> query;
};

struct StreamingDependencies
{
    std::shared_ptr<IStreamingDataSource> data_source;
    std::shared_ptr<IStreamingCommitTarget> commit_target;
    std::shared_ptr<IStreamingPriorityProvider> priority_provider;
};

[[nodiscard]] foundation::Result<StreamingServices> CreateStreamingServices(const StreamingDependencies& dependencies = {});
} // namespace epidemic::runtime::streaming
