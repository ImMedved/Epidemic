#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Streaming/streaming_types.h"

#include <optional>

namespace epidemic::runtime::streaming
{
// Adapter interfaces that let Streaming request chunk-related data from other majors
// through stable contracts, while still being able to run in mock mode when absent.
class IStreamingWorldSource
{
  public:
    virtual ~IStreamingWorldSource() = default;

    [[nodiscard]] virtual std::optional<RegionId> ResolveRegion(ChunkId chunk) const = 0;
};

class IStreamingPersistenceSource
{
  public:
    virtual ~IStreamingPersistenceSource() = default;

    [[nodiscard]] virtual foundation::Result<void> PrepareChunkData(const StreamingRequest& request) = 0;
};

class IStreamingResourceSource
{
  public:
    // ReleaseChunkResources may be retried only when the previous call did not report
    // success. Streaming itself never repeats a successfully completed release phase.
    virtual ~IStreamingResourceSource() = default;

    [[nodiscard]] virtual foundation::Result<void> PrepareChunkResources(const StreamingRequest& request) = 0;
    [[nodiscard]] virtual foundation::Result<void> ReleaseChunkResources(ChunkId chunk) = 0;
};

class IStreamingDataSource
{
  public:
    virtual ~IStreamingDataSource() = default;

    [[nodiscard]] virtual foundation::Result<ProgressiveLoadPlan> BuildLoadPlan(const StreamingRequest& request) = 0;
    [[nodiscard]] virtual foundation::Result<StreamingStepResult> ExecuteStep(
        const StreamingRequest& request,
        StreamingPlanStepRecord step,
        const RuntimeBudget& available_budget) = 0;
};

class IStreamingCommitTarget
{
  public:
    // request.handle is the stable operation identity. Implementations that can have
    // ambiguous transport/backend failures must use it as an idempotency key so a
    // retry cannot commit/rollback the same request twice.
    virtual ~IStreamingCommitTarget() = default;

    [[nodiscard]] virtual foundation::Result<void> Commit(const StreamingRequest& request) = 0;
    [[nodiscard]] virtual foundation::Result<void> Rollback(const StreamingRequest& request) = 0;
};

class IStreamingPriorityProvider
{
  public:
    virtual ~IStreamingPriorityProvider() = default;

    [[nodiscard]] virtual StreamingPriorityClass GetPriority(const StreamingTarget& target) const = 0;
};
} // namespace epidemic::runtime::streaming
