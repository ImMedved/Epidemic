#include "Epidemic/Runtime/Streaming/streaming_priority_resolver.h"
#include "Epidemic/Runtime/Streaming/streaming_runtime.h"
#include "Epidemic/Runtime/Streaming/streaming_sources.h"
#include "streaming_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <chrono>
#include <cstddef>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
using epidemic::foundation::Result;
using epidemic::runtime::ChunkId;
using epidemic::runtime::RegionId;
using epidemic::runtime::RuntimeObjectId;
using epidemic::foundation::StringId;
using epidemic::runtime::streaming::ChunkStreamingTarget;
using epidemic::runtime::streaming::CreateStreamingServices;
using epidemic::runtime::streaming::IResidencyController;
using epidemic::runtime::streaming::IStreamingCommitTarget;
using epidemic::runtime::streaming::IStreamingDataSource;
using epidemic::runtime::streaming::IStreamingPriorityProvider;
using epidemic::runtime::streaming::IStreamingPriorityResolver;
using epidemic::runtime::streaming::IStreamingQuery;
using epidemic::runtime::streaming::IStreamingRuntime;
using epidemic::runtime::streaming::IStreamingWorldSource;
using epidemic::runtime::streaming::InMemoryResidencyController;
using epidemic::runtime::streaming::ObjectStreamingTarget;
using epidemic::runtime::streaming::ProgressiveLoadPlan;
using epidemic::runtime::streaming::RegionStreamingTarget;
using epidemic::runtime::streaming::ResourceGroupStreamingTarget;
using epidemic::runtime::streaming::StreamingBudget;
using epidemic::runtime::streaming::StreamingDemandHandle;
using epidemic::runtime::streaming::StreamingDependencies;
using epidemic::runtime::streaming::StreamingPlanStep;
using epidemic::runtime::streaming::StreamingPlanStepRecord;
using epidemic::runtime::streaming::StreamingPriorityClass;
using epidemic::runtime::streaming::StreamingRequest;
using epidemic::runtime::streaming::StreamingRequestHandle;
using epidemic::runtime::streaming::StreamingRuntime;
using epidemic::runtime::streaming::StreamingState;
using epidemic::runtime::streaming::StreamingStepResult;
using epidemic::runtime::streaming::StreamingTarget;

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
        return iterator == priorities_.end() ? StreamingPriorityClass::Normal : iterator->second;
    }

  private:
    std::unordered_map<ChunkId, StreamingPriorityClass> priorities_;
};

class PlanSource final : public IStreamingDataSource
{
  public:
    Result<ProgressiveLoadPlan> BuildLoadPlan(const StreamingRequest&) override
    {
        ++build_count;
        return Result<ProgressiveLoadPlan>::Success(plan);
    }

    Result<StreamingStepResult> ExecuteStep(const StreamingRequest&, StreamingPlanStepRecord step) override
    {
        executed.push_back(step.step);
        if (fail_step && step.step == *fail_step)
        {
            return Result<StreamingStepResult>::Failure(epidemic::foundation::Error::Create("streaming.step_failed", "step failed for test"));
        }
        if (partial_step && step.step == *partial_step)
        {
            if (partial_remaining > 0)
            {
                --partial_remaining;
                return Result<StreamingStepResult>::Success(StreamingStepResult{partial_processed_bytes, false});
            }
            return Result<StreamingStepResult>::Success(StreamingStepResult{partial_processed_bytes, true});
        }
        return Result<StreamingStepResult>::Success(StreamingStepResult{step.estimated_bytes, true});
    }

    ProgressiveLoadPlan plan{{StreamingPlanStepRecord{StreamingPlanStep::ResolveTarget, 10},
                              StreamingPlanStepRecord{StreamingPlanStep::PrepareData, 20},
                              StreamingPlanStepRecord{StreamingPlanStep::PrepareResources, 30},
                              StreamingPlanStepRecord{StreamingPlanStep::Commit, 40}}};
    std::optional<StreamingPlanStep> fail_step;
    std::optional<StreamingPlanStep> partial_step;
    int partial_remaining = 0;
    std::size_t partial_processed_bytes = 1;
    int build_count = 0;
    std::vector<StreamingPlanStep> executed;
};

class CommitTarget final : public IStreamingCommitTarget
{
  public:
    Result<void> Commit(const StreamingRequest&) override
    {
        ++commits;
        return Result<void>::Success();
    }

    Result<void> Rollback(const StreamingRequest&) override
    {
        ++rollbacks;
        if (fail_rollback)
        {
            return Result<void>::Failure(epidemic::foundation::Error::Create("streaming.rollback_failed", "rollback failed for test"));
        }
        return Result<void>::Success();
    }

    int commits = 0;
    int rollbacks = 0;
    bool fail_rollback = false;
};

class PriorityProvider final : public IStreamingPriorityProvider
{
  public:
    StreamingPriorityClass GetPriority(const StreamingTarget&) const override
    {
        return priority;
    }

    StreamingPriorityClass priority = StreamingPriorityClass::High;
};

template <typename T>
concept HasRawCancelRequest = requires(T& runtime, epidemic::runtime::streaming::StreamingRequestId id) {
    runtime.CancelRequest(id);
};

template <typename T>
concept HasRawGetProgress = requires(T& query, epidemic::runtime::streaming::StreamingRequestId id) {
    query.GetProgress(id);
};

bool AdvanceTo(StreamingRuntime& runtime, ChunkId chunk, StreamingState expected, int max_ticks = 16)
{
    for (int i = 0; i < max_ticks; ++i)
    {
        if (runtime.GetChunkState(chunk) == expected)
        {
            return true;
        }
        (void)runtime.Tick();
    }
    return runtime.GetChunkState(chunk) == expected;
}

bool TestTwoConsumersRequestOneTarget()
{
    StreamingRuntime runtime;
    const ChunkId chunk{101};
    const auto first = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Low);
    const auto second = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::High);
    if (!first || !second || first.Value().request != second.Value().request || first.Value() == second.Value())
    {
        return false;
    }
    const auto progress = runtime.GetProgress(first.Value().request);
    return progress && progress->demand_count == 2;
}

bool TestOneConsumerReleaseKeepsLoad()
{
    StreamingRuntime runtime;
    const ChunkId chunk{102};
    const auto first = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    const auto second = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!first || !second)
    {
        return false;
    }
    const auto release = runtime.ReleaseDemand(first.Value());
    const auto progress = runtime.GetProgress(second.Value().request);
    (void)runtime.Tick();
    return release && progress && progress->demand_count == 1 && runtime.GetChunkState(chunk) == StreamingState::Queued;
}

bool TestLastConsumerReleaseCancelsBeforeLoad()
{
    StreamingRuntime runtime;
    const ChunkId chunk{103};
    const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!demand)
    {
        return false;
    }
    const auto release = runtime.ReleaseDemand(demand.Value());
    return release && runtime.GetChunkState(chunk) == StreamingState::Cancelled;
}

bool TestLastConsumerReleaseUnloadsResident()
{
    InMemoryResidencyController controller;
    StreamingRuntime runtime({}, nullptr, &controller);
    const ChunkId chunk{104};
    const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!demand || !AdvanceTo(runtime, chunk, StreamingState::Active))
    {
        return false;
    }
    const auto release = runtime.ReleaseDemand(demand.Value());
    (void)runtime.Tick();
    (void)runtime.Tick();
    return release && runtime.GetChunkState(chunk) == StreamingState::Unloaded &&
           controller.GetResidencyState(chunk) == StreamingState::Unloaded;
}

bool TestCancelDuringPartialLoadRollsBack()
{
    auto source = std::make_shared<PlanSource>();
    auto commit = std::make_shared<CommitTarget>();
    StreamingDependencies dependencies{source, commit, nullptr};
    StreamingRuntime runtime(dependencies);
    const ChunkId chunk{105};
    const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!demand)
    {
        return false;
    }
    (void)runtime.Tick();
    (void)runtime.Tick();
    (void)runtime.Tick();
    const auto cancelled = runtime.ReleaseDemand(demand.Value());
    return cancelled && runtime.GetChunkState(chunk) == StreamingState::Cancelled && commit->rollbacks == 1;
}

bool TestRollbackOnFailedStep()
{
    auto source = std::make_shared<PlanSource>();
    source->fail_step = StreamingPlanStep::PrepareResources;
    auto commit = std::make_shared<CommitTarget>();
    StreamingRuntime runtime(StreamingDependencies{source, commit, nullptr});
    const ChunkId chunk{106};
    const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!demand)
    {
        return false;
    }
    (void)runtime.Tick();
    (void)runtime.Tick();
    (void)runtime.Tick();
    (void)runtime.Tick();
    (void)runtime.Tick();
    return runtime.GetChunkState(chunk) == StreamingState::Failed && commit->rollbacks == 1 &&
           runtime.GetStatistics().failed == 1;
}

bool TestBudgetsLimitItemsAndBytes()
{
    auto source = std::make_shared<PlanSource>();
    StreamingRuntime runtime(StreamingDependencies{source, nullptr, nullptr});
    runtime.SetBudget(StreamingBudget{std::chrono::microseconds{100}, 1, 1});
    const auto first = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{201}}}, StreamingPriorityClass::Normal);
    const auto second = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{202}}}, StreamingPriorityClass::Normal);
    if (!first || !second)
    {
        return false;
    }
    (void)runtime.Tick();
    if (runtime.GetChunkState(ChunkId{201}) != StreamingState::Queued || runtime.GetChunkState(ChunkId{202}) != StreamingState::Requested)
    {
        return false;
    }
    (void)runtime.Tick();
    return runtime.GetChunkState(ChunkId{201}) == StreamingState::Loading && runtime.GetChunkState(ChunkId{202}) == StreamingState::Requested;
}

bool TestByteBudgetUsesEstimatedBytes()
{
    auto source = std::make_shared<PlanSource>();
    StreamingRuntime runtime(StreamingDependencies{source, nullptr, nullptr});
    runtime.SetBudget(StreamingBudget{std::chrono::microseconds{100}, 1, 15});
    const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{203}}}, StreamingPriorityClass::Normal);
    if (!demand)
    {
        return false;
    }

    (void)runtime.Tick();
    (void)runtime.Tick();
    const auto first_step = runtime.Tick();
    const auto blocked = runtime.Tick();
    runtime.SetBudget(StreamingBudget{std::chrono::microseconds{100}, 1, 20});
    const auto progressed = runtime.Tick();
    const auto progress = runtime.GetProgress(demand.Value().request);
    return first_step.processed_bytes == 10 && blocked.processed_bytes == 0 && progressed.processed_bytes == 20 &&
           progress && progress->processed_bytes == 30;
}

bool TestIncompleteStepDoesNotAdvanceCursor()
{
    auto source = std::make_shared<PlanSource>();
    source->partial_step = StreamingPlanStep::PrepareData;
    source->partial_remaining = 1;
    source->partial_processed_bytes = 7;
    StreamingRuntime runtime(StreamingDependencies{source, nullptr, nullptr});
    const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{204}}}, StreamingPriorityClass::Normal);
    if (!demand)
    {
        return false;
    }

    (void)runtime.Tick(); // Requested -> Queued
    (void)runtime.Tick(); // Queued -> Loading
    const auto first_step = runtime.Tick(); // ResolveTarget completes.
    const auto partial = runtime.Tick(); // PrepareData incomplete.
    const auto completed = runtime.Tick(); // PrepareData completes.
    const auto progress = runtime.GetProgress(demand.Value().request);
    return first_step.processed_bytes == 10 && partial.processed_bytes == 7 && completed.processed_bytes == 7 &&
           source->executed.size() == 3u &&
           source->executed[0] == StreamingPlanStep::ResolveTarget &&
           source->executed[1] == StreamingPlanStep::PrepareData &&
           source->executed[2] == StreamingPlanStep::PrepareData &&
           progress && progress->processed_bytes == 24;
}

bool TestTargetVariantsRejectUnsupportedInReference()
{
    StreamingRuntime runtime;
    const auto region = runtime.Request(StreamingTarget{RegionStreamingTarget{RegionId{1}}}, StreamingPriorityClass::Normal);
    const auto object = runtime.Request(StreamingTarget{ObjectStreamingTarget{RuntimeObjectId{1}}}, StreamingPriorityClass::Normal);
    const auto group = runtime.Request(StreamingTarget{ResourceGroupStreamingTarget{StringId::FromString("group/a")}}, StreamingPriorityClass::Normal);
    return !region && region.GetError().HasCode("streaming.unsupported_target") &&
           !object && object.GetError().HasCode("streaming.unsupported_target") &&
           !group && group.GetError().HasCode("streaming.unsupported_target");
}

bool TestRequestGenerationAndStaleDemand()
{
    StreamingRuntime runtime;
    const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{301}}}, StreamingPriorityClass::Normal);
    if (!demand)
    {
        return false;
    }
    StreamingDemandHandle stale = demand.Value();
    ++stale.generation;
    const auto released = runtime.ReleaseDemand(stale);
    StreamingRequestHandle request{demand.Value().request.id, 999};
    const auto cancelled = runtime.CancelRequest(request);
    const auto active_cancel = runtime.CancelRequest(demand.Value().request);
    return !released && released.GetError().HasCode("streaming.stale_handle") &&
           !active_cancel && active_cancel.GetError().HasCode("streaming.active_demands") &&
           !cancelled && cancelled.GetError().HasCode("streaming.stale_handle");
}

bool TestRecordCleanup()
{
    StreamingRuntime runtime;
    runtime.CleanupCompletedRecords(1);
    const auto first = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{401}}}, StreamingPriorityClass::Normal);
    const auto second = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{402}}}, StreamingPriorityClass::Normal);
    if (!first || !second)
    {
        return false;
    }
    (void)runtime.ReleaseDemand(first.Value());
    (void)runtime.ReleaseDemand(second.Value());
    (void)runtime.Tick();
    return runtime.RecordCount() == 1;
}

bool TestRollbackFailureMarksRollbackFailed()
{
    auto source = std::make_shared<PlanSource>();
    auto commit = std::make_shared<CommitTarget>();
    commit->fail_rollback = true;
    StreamingRuntime runtime(StreamingDependencies{source, commit, nullptr});
    const ChunkId chunk{601};
    const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!demand)
    {
        return false;
    }

    (void)runtime.Tick();
    (void)runtime.Tick();
    (void)runtime.Tick();
    const auto cancelled = runtime.ReleaseDemand(demand.Value());
    return !cancelled && cancelled.GetError().HasCode("streaming.rollback_failed") &&
           runtime.GetChunkState(chunk) == StreamingState::RollbackFailed;
}

bool TestServicesFactoryUsesDependencies()
{
    auto source = std::make_shared<PlanSource>();
    auto commit = std::make_shared<CommitTarget>();
    auto priority = std::make_shared<PriorityProvider>();
    const auto services = CreateStreamingServices(StreamingDependencies{source, commit, priority});
    if (!services || !services.Value().runtime || !services.Value().query)
    {
        return false;
    }
    const auto demand = services.Value().runtime->Request(StreamingTarget{ChunkStreamingTarget{ChunkId{501}}}, StreamingPriorityClass::Normal);
    return demand && source->build_count == 1;
}
} // namespace

int main()
{
    static_assert(std::is_same_v<decltype(StreamingBudget{}.max_requests), std::size_t>);
    static_assert(StreamingBudget{}.IsUnlimited());
    static_assert(std::is_abstract_v<IStreamingRuntime>);
    static_assert(std::is_abstract_v<IStreamingQuery>);
    static_assert(std::is_abstract_v<IResidencyController>);
    static_assert(std::is_abstract_v<IStreamingPriorityResolver>);
    static_assert(std::is_abstract_v<IStreamingDataSource>);
    static_assert(!HasRawCancelRequest<IStreamingRuntime>);
    static_assert(!HasRawGetProgress<IStreamingQuery>);
    static_assert(std::is_abstract_v<IStreamingCommitTarget>);
    static_assert(std::is_abstract_v<IStreamingPriorityProvider>);

    if (!TestTwoConsumersRequestOneTarget()) return 1;
    if (!TestOneConsumerReleaseKeepsLoad()) return 2;
    if (!TestLastConsumerReleaseCancelsBeforeLoad()) return 3;
    if (!TestLastConsumerReleaseUnloadsResident()) return 4;
    if (!TestCancelDuringPartialLoadRollsBack()) return 5;
    if (!TestRollbackOnFailedStep()) return 6;
    if (!TestBudgetsLimitItemsAndBytes()) return 7;
    if (!TestByteBudgetUsesEstimatedBytes()) return 13;
    if (!TestIncompleteStepDoesNotAdvanceCursor()) return 15;
    if (!TestTargetVariantsRejectUnsupportedInReference()) return 8;
    if (!TestRequestGenerationAndStaleDemand()) return 9;
    if (!TestRecordCleanup()) return 10;
    if (!TestRollbackFailureMarksRollbackFailed()) return 14;
    if (!TestServicesFactoryUsesDependencies()) return 11;
    return 0;
}
