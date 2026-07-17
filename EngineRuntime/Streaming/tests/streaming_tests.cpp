#include "Epidemic/Runtime/Streaming/streaming_priority_resolver.h"
#include "Epidemic/Runtime/Streaming/streaming_runtime.h"
#include "Epidemic/Runtime/Streaming/streaming_sources.h"
#include "streaming_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <chrono>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
using epidemic::runtime::ChunkId;
using epidemic::runtime::RegionId;
using epidemic::runtime::streaming::IResidencyController;
using epidemic::runtime::streaming::IStreamingCommitTarget;
using epidemic::runtime::streaming::IStreamingDataSource;
using epidemic::runtime::streaming::IStreamingPersistenceSource;
using epidemic::runtime::streaming::IStreamingPriorityProvider;
using epidemic::runtime::streaming::IStreamingQuery;
using epidemic::runtime::streaming::IStreamingPriorityResolver;
using epidemic::runtime::streaming::IStreamingResourceSource;
using epidemic::runtime::streaming::IStreamingRuntime;
using epidemic::runtime::streaming::IStreamingWorldSource;
using epidemic::runtime::streaming::InMemoryResidencyController;
using epidemic::runtime::streaming::ChunkStreamingTarget;
using epidemic::runtime::streaming::CreateStreamingServices;
using epidemic::runtime::streaming::StreamingRequestHandle;
using epidemic::runtime::streaming::StreamingTarget;
using epidemic::runtime::streaming::StreamingBudget;
using epidemic::runtime::streaming::StreamingPriorityClass;
using epidemic::runtime::streaming::StreamingRequest;
using epidemic::runtime::streaming::StreamingRuntime;
using epidemic::runtime::streaming::StreamingState;

class FixedPriorityResolver final : public IStreamingPriorityResolver
{
  public:
    void SetPriority(ChunkId chunk, StreamingPriorityClass priority)
    {
        priorities_[chunk] = priority;
    }

    [[nodiscard]] StreamingPriorityClass ResolvePriority(ChunkId chunk) const override
    {
        const auto iterator = priorities_.find(chunk);
        if (iterator == priorities_.end())
        {
            return StreamingPriorityClass::Normal;
        }

        return iterator->second;
    }

  private:
    std::unordered_map<ChunkId, StreamingPriorityClass> priorities_;
};

class MockWorldSource final : public IStreamingWorldSource
{
  public:
    void MapChunk(ChunkId chunk, RegionId region)
    {
        regions_[chunk] = region;
    }

    [[nodiscard]] std::optional<RegionId> ResolveRegion(ChunkId chunk) const override
    {
        const auto iterator = regions_.find(chunk);
        if (iterator == regions_.end())
        {
            return std::nullopt;
        }

        return iterator->second;
    }

  private:
    std::unordered_map<ChunkId, RegionId> regions_;
};

class MockPersistenceSource final : public IStreamingPersistenceSource
{
  public:
    void FailChunk(ChunkId chunk)
    {
        failing_chunks_.insert(chunk);
    }

    [[nodiscard]] epidemic::foundation::Result<void> PrepareChunkData(const StreamingRequest& request) override
    {
        prepared_chunks_.push_back(request.chunk);
        if (failing_chunks_.contains(request.chunk))
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("streaming.persistence_failed", "test persistence failure"));
        }

        return epidemic::foundation::Result<void>::Success();
    }

  private:
    std::unordered_set<ChunkId> failing_chunks_;
    std::vector<ChunkId> prepared_chunks_;
};

class MockResourceSource final : public IStreamingResourceSource
{
  public:
    void FailChunk(ChunkId chunk)
    {
        failing_chunks_.insert(chunk);
    }

    [[nodiscard]] epidemic::foundation::Result<void> PrepareChunkResources(const StreamingRequest& request) override
    {
        prepared_chunks_.push_back(request.chunk);
        if (failing_chunks_.contains(request.chunk))
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("streaming.resource_failed", "test resource failure"));
        }

        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] epidemic::foundation::Result<void> ReleaseChunkResources(ChunkId chunk) override
    {
        released_chunks_.push_back(chunk);
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] const std::vector<ChunkId>& released_chunks() const
    {
        return released_chunks_;
    }

  private:
    std::unordered_set<ChunkId> failing_chunks_;
    std::vector<ChunkId> prepared_chunks_;
    std::vector<ChunkId> released_chunks_;
};

bool TestRequestAndCancelFlow()
{
    InMemoryResidencyController controller;
    MockPersistenceSource persistence;
    MockResourceSource resources;
    StreamingRuntime runtime(nullptr, &controller, nullptr, &persistence, &resources);

    const ChunkId chunk{101};
    const auto request = runtime.RequestChunk(chunk, StreamingPriorityClass::High);
    if (!request || runtime.GetChunkState(chunk) != StreamingState::Requested)
    {
        return false;
    }

    runtime.SetBudget(StreamingBudget{std::chrono::microseconds{100}, 8, 0});
    runtime.Tick();
    if (runtime.GetChunkState(chunk) != StreamingState::Queued)
    {
        return false;
    }

    const auto cancel_result = runtime.CancelRequest(request.Value());
    if (!cancel_result || runtime.GetChunkState(chunk) != StreamingState::Deactivating)
    {
        return false;
    }

    runtime.Tick();
    if (runtime.GetChunkState(chunk) != StreamingState::Unloading)
    {
        return false;
    }

    runtime.Tick();
    return runtime.GetChunkState(chunk) == StreamingState::Unloaded &&
           controller.GetResidencyState(chunk) == StreamingState::Unloaded &&
           !resources.released_chunks().empty();
}

bool TestProgressRevisionChangesOnlyOnTransitions()
{
    StreamingRuntime runtime;
    runtime.SetBudget(StreamingBudget{std::chrono::microseconds{100}, 1, 0});

    const auto request = runtime.RequestChunk(ChunkId{111}, StreamingPriorityClass::Normal);
    if (!request)
    {
        return false;
    }

    const auto initial = runtime.GetProgress(request.Value());
    runtime.Tick();
    const auto queued = runtime.GetProgress(request.Value());
    const auto cancelled = runtime.CancelRequest(request.Value());
    const auto deactivating = runtime.GetProgress(request.Value());
    const auto duplicate_cancel = runtime.CancelRequest(request.Value());
    const auto still_deactivating = runtime.GetProgress(request.Value());

    return initial.has_value() && queued.has_value() && deactivating.has_value() &&
           still_deactivating.has_value() && cancelled && duplicate_cancel && initial->revision == 1 &&
           queued->revision == 2 && deactivating->revision == 3 &&
           still_deactivating->revision == deactivating->revision;
}

bool TestPriorityOrderingUsesResolvedPriority()
{
    FixedPriorityResolver resolver;
    InMemoryResidencyController controller;
    StreamingRuntime runtime(&resolver, &controller, nullptr, nullptr, nullptr);
    runtime.SetBudget(StreamingBudget{std::chrono::microseconds{100}, 1, 0});

    const ChunkId background_chunk{201};
    const ChunkId critical_chunk{202};
    resolver.SetPriority(critical_chunk, StreamingPriorityClass::Critical);

    const auto first = runtime.RequestChunk(background_chunk, StreamingPriorityClass::Low);
    const auto second = runtime.RequestChunk(critical_chunk, StreamingPriorityClass::Normal);
    if (!first || !second)
    {
        return false;
    }

    runtime.Tick();
    return runtime.GetChunkState(background_chunk) == StreamingState::Requested &&
           runtime.GetChunkState(critical_chunk) == StreamingState::Queued;
}

bool TestBudgetLimitsProcessing()
{
    StreamingRuntime runtime;
    runtime.SetBudget(StreamingBudget{std::chrono::microseconds{100}, 2, 0});

    if (!runtime.RequestChunk(ChunkId{301}, StreamingPriorityClass::Normal) ||
        !runtime.RequestChunk(ChunkId{302}, StreamingPriorityClass::Normal) ||
        !runtime.RequestChunk(ChunkId{303}, StreamingPriorityClass::Normal))
    {
        return false;
    }

    runtime.Tick();
    return runtime.GetChunkState(ChunkId{301}) == StreamingState::Queued &&
           runtime.GetChunkState(ChunkId{302}) == StreamingState::Queued &&
           runtime.GetChunkState(ChunkId{303}) == StreamingState::Requested;
}

bool TestFailedRequestIsReported()
{
    MockPersistenceSource persistence;
    persistence.FailChunk(ChunkId{401});

    StreamingRuntime runtime(nullptr, nullptr, nullptr, &persistence, nullptr);
    runtime.SetBudget(StreamingBudget{std::chrono::microseconds{100}, 1, 0});

    const auto request = runtime.RequestChunk(ChunkId{401}, StreamingPriorityClass::High);
    if (!request)
    {
        return false;
    }

    runtime.Tick();
    if (runtime.GetChunkState(ChunkId{401}) != StreamingState::Queued)
    {
        return false;
    }

    runtime.Tick();
    const auto progress = runtime.GetProgress(request.Value());
    return runtime.GetChunkState(ChunkId{401}) == StreamingState::Failed &&
           progress.has_value() && progress->state == StreamingState::Failed;
}

bool TestMockModeCanReachActiveWithoutExternalMajors()
{
    StreamingRuntime runtime;
    runtime.SetBudget(StreamingBudget{std::chrono::microseconds{100}, 4, 0});

    const auto request = runtime.RequestChunk(ChunkId{501}, StreamingPriorityClass::Normal);
    if (!request)
    {
        return false;
    }

    runtime.Tick();
    runtime.Tick();
    runtime.Tick();
    runtime.Tick();
    runtime.Tick();

    const auto progress = runtime.GetProgress(request.Value());
    return runtime.GetChunkState(ChunkId{501}) == StreamingState::Active &&
           progress.has_value() && progress->progress == 1.0f;
}

bool TestTargetHandleDemandAndStatistics()
{
    StreamingRuntime runtime;
    runtime.SetBudget(StreamingBudget{std::chrono::microseconds{100}, 8, 0});

    const auto first = runtime.RequestTarget(StreamingTarget{ChunkStreamingTarget{ChunkId{601}}},
                                             StreamingPriorityClass::Normal,
                                             2);
    const auto duplicate = runtime.RequestTarget(StreamingTarget{ChunkStreamingTarget{ChunkId{601}}},
                                                 StreamingPriorityClass::High,
                                                 3);
    if (!first || !duplicate || first.Value() != duplicate.Value())
    {
        return false;
    }

    const auto progress = runtime.GetProgress(first.Value());
    const auto stats = runtime.GetStatistics();
    if (!progress || progress->handle != first.Value() || stats.requested != 1)
    {
        return false;
    }

    runtime.Tick();
    runtime.Tick();
    runtime.Tick();
    runtime.Tick();
    runtime.Tick();

    return runtime.GetStatistics().committed == 1 &&
           runtime.GetChunkState(ChunkId{601}) == StreamingState::Active;
}

bool TestStaleHandleCancellationIsRejected()
{
    StreamingRuntime runtime;
    const auto handle = runtime.RequestTarget(StreamingTarget{ChunkStreamingTarget{ChunkId{701}}},
                                             StreamingPriorityClass::Normal,
                                             1);
    if (!handle)
    {
        return false;
    }

    StreamingRequestHandle stale = handle.Value();
    ++stale.generation;
    const auto cancelled = runtime.CancelRequest(stale);

    return !cancelled && cancelled.GetError().HasCode("streaming.stale_handle") &&
           runtime.GetProgress(stale) == std::nullopt;
}

bool TestStreamingServicesFactory()
{
    const auto services = CreateStreamingServices();
    return services.runtime != nullptr && services.query != nullptr;
}
} // namespace

int main()
{
    static_assert(std::is_same_v<decltype(StreamingBudget{}.max_requests), std::size_t>);
    static_assert(std::is_abstract_v<IStreamingRuntime>);
    static_assert(std::is_abstract_v<IStreamingQuery>);
    static_assert(std::is_abstract_v<IResidencyController>);
    static_assert(std::is_abstract_v<IStreamingPriorityResolver>);
    static_assert(std::is_abstract_v<IStreamingDataSource>);
    static_assert(std::is_abstract_v<IStreamingCommitTarget>);
    static_assert(std::is_abstract_v<IStreamingPriorityProvider>);
    static_assert(std::is_abstract_v<IStreamingWorldSource>);
    static_assert(std::is_abstract_v<IStreamingPersistenceSource>);
    static_assert(std::is_abstract_v<IStreamingResourceSource>);

    if (!TestRequestAndCancelFlow())
    {
        return 1;
    }

    if (!TestPriorityOrderingUsesResolvedPriority())
    {
        return 2;
    }

    if (!TestProgressRevisionChangesOnlyOnTransitions())
    {
        return 3;
    }

    if (!TestBudgetLimitsProcessing())
    {
        return 4;
    }

    if (!TestFailedRequestIsReported())
    {
        return 5;
    }

    if (!TestMockModeCanReachActiveWithoutExternalMajors())
    {
        return 6;
    }
    if (!TestTargetHandleDemandAndStatistics())
    {
        return 7;
    }
    if (!TestStaleHandleCancellationIsRejected())
    {
        return 8;
    }
    if (!TestStreamingServicesFactory())
    {
        return 9;
    }

    return 0;
}

