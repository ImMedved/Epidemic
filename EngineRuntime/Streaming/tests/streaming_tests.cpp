#include "Epidemic/Runtime/Streaming/streaming_priority_resolver.h"
#include "Epidemic/Runtime/Streaming/streaming_runtime.h"
#include "Epidemic/Runtime/Streaming/streaming_sources.h"
#include "streaming_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <chrono>
#include <cstddef>
#include <limits>
#include <stdexcept>
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
using epidemic::runtime::streaming::IStreamingPersistenceSource;
using epidemic::runtime::streaming::IStreamingQuery;
using epidemic::runtime::streaming::IStreamingResourceSource;
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
using epidemic::runtime::streaming::StreamingStatistics;
using epidemic::runtime::streaming::StreamingStepResult;
using epidemic::runtime::streaming::StreamingTarget;

StreamingDependencies MakeDependencies(
    std::shared_ptr<IStreamingDataSource> data_source = {},
    std::shared_ptr<IStreamingCommitTarget> commit_target = {},
    std::shared_ptr<IStreamingPriorityProvider> priority_provider = {},
    std::shared_ptr<IResidencyController> residency_controller = {},
    std::shared_ptr<IStreamingWorldSource> world_source = {},
    std::shared_ptr<IStreamingPersistenceSource> persistence_source = {},
    std::shared_ptr<IStreamingResourceSource> resource_source = {})
{
    StreamingDependencies dependencies{};
    dependencies.data_source = std::move(data_source);
    dependencies.commit_target = std::move(commit_target);
    dependencies.priority_provider = std::move(priority_provider);
    dependencies.residency_controller = std::move(residency_controller);
    dependencies.world_source = std::move(world_source);
    dependencies.persistence_source = std::move(persistence_source);
    dependencies.resource_source = std::move(resource_source);
    return dependencies;
}

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
        if (throw_on_build)
        {
            throw std::runtime_error("build failure");
        }
        return Result<ProgressiveLoadPlan>::Success(plan);
    }

    Result<StreamingStepResult> ExecuteStep(const StreamingRequest&, StreamingPlanStepRecord step, const epidemic::runtime::RuntimeBudget& budget) override
    {
        if (throw_on_execute) throw std::runtime_error("execute failure");
        executed.push_back(step.step);
        last_budget = budget;
        if (fail_step && step.step == *fail_step)
        {
            return Result<StreamingStepResult>::Failure(epidemic::foundation::Error::Create("streaming.step_failed", "step failed for test"));
        }
        if (respect_budget && budget.max_bytes != 0 && step.estimated_bytes > budget.max_bytes)
        {
            return Result<StreamingStepResult>::Success(StreamingStepResult{static_cast<std::size_t>(budget.max_bytes), false});
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
    bool respect_budget = false;
    bool throw_on_build = false;
    bool throw_on_execute = false;
    epidemic::runtime::RuntimeBudget last_budget{};
    int build_count = 0;
    std::vector<StreamingPlanStep> executed;
};

class CommitTarget final : public IStreamingCommitTarget
{
  public:
    Result<void> Commit(const StreamingRequest&) override
    {
        ++commits;
        if (throw_commit) throw std::runtime_error("commit threw");
        return Result<void>::Success();
    }

    Result<void> Rollback(const StreamingRequest&) override
    {
        ++rollbacks;
        if (throw_rollback)
        {
            throw std::runtime_error("rollback threw");
        }
        if (fail_rollback)
        {
            return Result<void>::Failure(epidemic::foundation::Error::Create("streaming.rollback_failed", "rollback failed for test"));
        }
        return Result<void>::Success();
    }

    int commits = 0;
    int rollbacks = 0;
    bool fail_rollback = false;
    bool throw_commit = false;
    bool throw_rollback = false;
};

class PriorityProvider final : public IStreamingPriorityProvider
{
  public:
    StreamingPriorityClass GetPriority(const StreamingTarget&) const override
    {
        ++const_cast<PriorityProvider*>(this)->calls;
        if (throw_on_get) throw std::runtime_error("priority threw");
        return priority;
    }

    StreamingPriorityClass priority = StreamingPriorityClass::High;
    bool throw_on_get = false;
    int calls = 0;
};

class ResourceTracker final : public IStreamingResourceSource
{
  public:
    Result<void> PrepareChunkResources(const StreamingRequest&) override
    {
        ++prepares;
        if (throw_prepare) throw std::runtime_error("resource prepare threw");
        return Result<void>::Success();
    }

    Result<void> ReleaseChunkResources(ChunkId) override
    {
        ++releases;
        if (throw_release) throw std::runtime_error("resource release threw");
        if (fail_release_once)
        {
            fail_release_once = false;
            return Result<void>::Failure(epidemic::foundation::Error::Create("streaming.release_failed", "release failed once"));
        }
        return Result<void>::Success();
    }

    int prepares = 0;
    int releases = 0;
    bool fail_release_once = false;
    bool throw_release = false;
    bool throw_prepare = false;
};

class WorldTracker final : public IStreamingWorldSource
{
  public:
    std::optional<RegionId> ResolveRegion(ChunkId) const override
    {
        ++const_cast<WorldTracker*>(this)->resolves;
        if (throw_resolve) throw std::runtime_error("world resolve threw");
        return RegionId{77};
    }

    int resolves = 0;
    bool throw_resolve = false;
};

class PersistenceTracker final : public IStreamingPersistenceSource
{
  public:
    Result<void> PrepareChunkData(const StreamingRequest&) override
    {
        ++prepares;
        if (throw_prepare) throw std::runtime_error("persistence prepare threw");
        return Result<void>::Success();
    }

    int prepares = 0;
    bool throw_prepare = false;
};

class ResidencyTracker final : public IResidencyController
{
  public:
    Result<void> ActivateChunk(ChunkId) override
    {
        ++activates;
        if (throw_activate) throw std::runtime_error("activate threw");
        if (fail_activate_once)
        {
            fail_activate_once = false;
            return Result<void>::Failure(epidemic::foundation::Error::Create("streaming.activate_failed", "activate failed once"));
        }
        return Result<void>::Success();
    }

    Result<void> DeactivateChunk(ChunkId) override
    {
        ++deactivates;
        if (throw_deactivate) throw std::runtime_error("deactivate threw");
        if (fail_deactivate_once)
        {
            fail_deactivate_once = false;
            return Result<void>::Failure(epidemic::foundation::Error::Create("streaming.deactivate_failed", "deactivate failed once"));
        }
        return Result<void>::Success();
    }

    Result<void> UnloadChunk(ChunkId) override
    {
        ++unloads;
        if (throw_unload) throw std::runtime_error("unload threw");
        if (fail_unload_once)
        {
            fail_unload_once = false;
            return Result<void>::Failure(epidemic::foundation::Error::Create("streaming.unload_failed", "unload failed once"));
        }
        return Result<void>::Success();
    }

    int activates = 0;
    int deactivates = 0;
    int unloads = 0;
    bool fail_activate_once = false;
    bool fail_deactivate_once = false;
    bool fail_unload_once = false;
    bool throw_activate = false;
    bool throw_deactivate = false;
    bool throw_unload = false;
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
    auto controller = std::make_shared<InMemoryResidencyController>();
    StreamingRuntime runtime(MakeDependencies({}, {}, {}, controller));
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
           controller->GetResidencyState(chunk) == StreamingState::Unloaded;
}

bool TestCancelDuringPartialLoadRollsBack()
{
    auto source = std::make_shared<PlanSource>();
    auto commit = std::make_shared<CommitTarget>();
    StreamingDependencies dependencies = MakeDependencies(source, commit);
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
    StreamingRuntime runtime(MakeDependencies(source, commit));
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
    StreamingRuntime runtime(MakeDependencies(source));
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
    StreamingRuntime runtime(MakeDependencies(source));
    runtime.SetBudget(StreamingBudget{std::chrono::microseconds{100}, 1, 15});
    const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{203}}}, StreamingPriorityClass::Normal);
    if (!demand)
    {
        return false;
    }

    (void)runtime.Tick();
    (void)runtime.Tick();
    const auto first_step = runtime.Tick();
    const auto violation = runtime.Tick();
    const auto progress = runtime.GetProgress(demand.Value().request);
    return first_step.processed_bytes == 10 && violation.failures.size() == 1u &&
           violation.failures.front().error.HasCode("streaming.step_budget_violation") &&
           runtime.GetStatistics().budget_violations == 1u &&
           progress && progress->state == StreamingState::Failed && progress->processed_bytes == 10;
}

bool TestIncompleteStepDoesNotAdvanceCursor()
{
    auto source = std::make_shared<PlanSource>();
    source->partial_step = StreamingPlanStep::PrepareData;
    source->partial_remaining = 1;
    source->partial_processed_bytes = 7;
    StreamingRuntime runtime(MakeDependencies(source));
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

bool TestPartialStepUsesRemainingByteBudget()
{
    auto source = std::make_shared<PlanSource>();
    source->partial_step = StreamingPlanStep::PrepareData;
    source->partial_remaining = 1;
    source->partial_processed_bytes = 60;
    StreamingRuntime runtime(MakeDependencies(source));
    runtime.SetBudget(StreamingBudget{std::chrono::microseconds{100}, 1, 100});
    source->plan.steps[1].estimated_bytes = 100;
    const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{205}}}, StreamingPriorityClass::Normal);
    if (!demand)
    {
        return false;
    }

    (void)runtime.Tick();
    (void)runtime.Tick();
    (void)runtime.Tick();
    const auto partial = runtime.Tick();
    runtime.SetBudget(StreamingBudget{std::chrono::microseconds{100}, 1, 50});
    const auto completed = runtime.Tick();
    const auto progress = runtime.GetProgress(demand.Value().request);
    return partial.processed_bytes == 60 && completed.failures.size() == 1u &&
           completed.failures.front().error.HasCode("streaming.step_budget_violation") &&
           runtime.GetStatistics().budget_violations == 1u &&
           progress && progress->state == StreamingState::Failed && progress->processed_bytes == 70;
}

bool TestZeroProcessedIncompleteStepDoesNotAutoComplete()
{
    auto source = std::make_shared<PlanSource>();
    source->partial_step = StreamingPlanStep::PrepareData;
    source->partial_remaining = 1;
    source->partial_processed_bytes = 0;
    StreamingRuntime runtime(MakeDependencies(source));
    const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{206}}}, StreamingPriorityClass::Normal);
    if (!demand)
    {
        return false;
    }

    (void)runtime.Tick();
    (void)runtime.Tick();
    (void)runtime.Tick();
    const auto partial = runtime.Tick();
    const auto progress_after_partial = runtime.GetProgress(demand.Value().request);
    const auto completed = runtime.Tick();
    return partial.processed_bytes == 0 && completed.processed_bytes == 0 &&
           progress_after_partial && progress_after_partial->state == StreamingState::Loading &&
           source->executed.size() == 3u && source->executed[1] == StreamingPlanStep::PrepareData &&
           source->executed[2] == StreamingPlanStep::PrepareData;
}

bool TestCommitRunsOnlyAfterCompletedStepAndOnlyOnce()
{
    auto source = std::make_shared<PlanSource>();
    auto commit = std::make_shared<CommitTarget>();
    source->partial_step = StreamingPlanStep::Commit;
    source->partial_remaining = 1;
    source->partial_processed_bytes = 0;
    StreamingRuntime runtime(MakeDependencies(source, commit));
    const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{207}}}, StreamingPriorityClass::Normal);
    if (!demand)
    {
        return false;
    }

    (void)runtime.Tick();
    (void)runtime.Tick();
    (void)runtime.Tick();
    (void)runtime.Tick();
    (void)runtime.Tick();
    const auto incomplete_commit = runtime.Tick();
    if (commit->commits != 0 || incomplete_commit.processed_bytes != 0)
    {
        return false;
    }
    const auto completed_commit = runtime.Tick();
    const auto after = runtime.Tick();
    return commit->commits == 1 && completed_commit.processed_bytes == 0 && after.processed_requests <= 1;
}

bool TestCommitBudgetValidationRunsBeforeExternalCommit()
{
    auto source = std::make_shared<PlanSource>();
    auto commit = std::make_shared<CommitTarget>();
    source->plan.steps = {StreamingPlanStepRecord{StreamingPlanStep::Commit, 100}};
    StreamingRuntime runtime(MakeDependencies(source, commit));
    runtime.SetBudget(StreamingBudget{std::chrono::microseconds{100}, 1, 50});

    const ChunkId chunk{208};
    const auto first = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!first)
    {
        return false;
    }
    (void)runtime.Tick();
    (void)runtime.Tick();
    const auto rejected = runtime.Tick();
    if (rejected.failures.size() != 1u || !rejected.failures.front().error.HasCode("streaming.step_budget_violation") ||
        commit->commits != 0 || runtime.GetChunkState(chunk) != StreamingState::Failed)
    {
        return false;
    }

    runtime.SetBudget(StreamingBudget{std::chrono::microseconds{100}, 1, 100});
    const auto retry = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!retry)
    {
        return false;
    }
    (void)runtime.Tick();
    (void)runtime.Tick();
    const auto committed = runtime.Tick();
    return committed.failures.empty() && commit->commits == 1;
}

bool TestByteCounterOverflowIsRejectedBeforeCommit()
{
    auto source = std::make_shared<PlanSource>();
    auto commit = std::make_shared<CommitTarget>();
    source->plan.steps = {StreamingPlanStepRecord{StreamingPlanStep::Commit, 1}};
    source->partial_step = StreamingPlanStep::Commit;
    source->partial_remaining = 1;
    source->partial_processed_bytes = std::numeric_limits<std::size_t>::max() - 8u;
    StreamingRuntime runtime(MakeDependencies(source, commit));

    const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{209}}}, StreamingPriorityClass::Normal);
    if (!demand)
    {
        return false;
    }
    (void)runtime.Tick();
    (void)runtime.Tick();
    const auto partial = runtime.Tick();
    if (!partial.failures.empty() || commit->commits != 0)
    {
        return false;
    }
    const auto overflow = runtime.Tick();
    return overflow.failures.size() == 1u && overflow.failures.front().error.HasCode("streaming.byte_counter_overflow") &&
           commit->commits == 0;
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
    StreamingRuntime runtime(MakeDependencies(source, commit));
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
    const auto services = CreateStreamingServices(MakeDependencies(source, commit, priority));
    if (!services || !services.Value().runtime || !services.Value().query)
    {
        return false;
    }
    const auto demand = services.Value().runtime->Request(StreamingTarget{ChunkStreamingTarget{ChunkId{501}}}, StreamingPriorityClass::Normal);
    return demand && source->build_count == 1;
}

bool TestServicesFactoryPassesAllDependencies()
{
    auto world = std::make_shared<WorldTracker>();
    auto persistence = std::make_shared<PersistenceTracker>();
    auto resources = std::make_shared<ResourceTracker>();
    auto residency = std::make_shared<ResidencyTracker>();
    auto priority = std::make_shared<PriorityProvider>();
    const auto services = CreateStreamingServices(MakeDependencies({}, {}, priority, residency, world, persistence, resources));
    if (!services || !services.Value().runtime)
    {
        return false;
    }
    const ChunkId chunk{502};
    const auto demand = services.Value().runtime->Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!demand)
    {
        return false;
    }
    for (int i = 0; i < 12 && services.Value().runtime->GetChunkState(chunk) != StreamingState::Active; ++i)
    {
        (void)services.Value().runtime->Tick();
    }
    if (services.Value().runtime->GetChunkState(chunk) != StreamingState::Active)
    {
        return false;
    }
    if (!services.Value().runtime->ReleaseDemand(demand.Value()))
    {
        return false;
    }
    (void)services.Value().runtime->Tick();
    (void)services.Value().runtime->Tick();
    return world->resolves == 1 && persistence->prepares == 1 && resources->prepares == 1 &&
           resources->releases == 1 && residency->activates == 1 && residency->deactivates == 1 &&
           residency->unloads == 1 && priority->calls == 1;
}

bool TestResolvedPriorityPersistsAcrossDemands()
{
    auto source = std::make_shared<PlanSource>();
    auto priority = std::make_shared<PriorityProvider>();
    StreamingRuntime runtime(MakeDependencies(source, {}, priority));
    runtime.SetBudget(StreamingBudget{std::chrono::microseconds{100}, 1, 0});
    priority->priority = StreamingPriorityClass::High;
    const auto first = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{701}}}, StreamingPriorityClass::Normal);
    priority->priority = StreamingPriorityClass::Low;
    const auto second = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{702}}}, StreamingPriorityClass::Normal);
    if (!first || !second)
    {
        return false;
    }
    (void)runtime.Tick();
    return runtime.GetChunkState(ChunkId{701}) == StreamingState::Queued &&
           runtime.GetChunkState(ChunkId{702}) == StreamingState::Requested;
}

bool TestDemandDuringUnloadReactivatesExistingRequest()
{
    auto controller = std::make_shared<InMemoryResidencyController>();
    StreamingRuntime runtime(MakeDependencies({}, {}, {}, controller));
    const ChunkId chunk{702};
    const auto first = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!first || !AdvanceTo(runtime, chunk, StreamingState::Active))
    {
        return false;
    }
    if (!runtime.ReleaseDemand(first.Value()))
    {
        return false;
    }
    const auto second = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    const auto progress = second ? runtime.GetProgress(second.Value().request) : std::nullopt;
    (void)runtime.Tick();
    return second && second.Value().request == first.Value().request && progress && progress->demand_count == 1 &&
           runtime.GetChunkState(chunk) == StreamingState::Active;
}

bool TestDemandDuringUnloadingWaitsForPredecessor()
{
    auto source = std::make_shared<PlanSource>();
    auto resources = std::make_shared<ResourceTracker>();
    auto residency = std::make_shared<ResidencyTracker>();
    StreamingRuntime runtime(MakeDependencies(source, {}, {}, residency, {}, {}, resources));
    const ChunkId chunk{214};
    const auto first = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!first || !AdvanceTo(runtime, chunk, StreamingState::Active))
    {
        return false;
    }
    const std::size_t executed_before_release = source->executed.size();
    if (!runtime.ReleaseDemand(first.Value()))
    {
        return false;
    }
    (void)runtime.Tick(); // Deactivating -> Unloading.
    if (runtime.GetChunkState(chunk) != StreamingState::Unloading)
    {
        return false;
    }
    const auto successor = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Critical);
    if (!successor || successor.Value().request == first.Value().request)
    {
        return false;
    }
    (void)runtime.Tick(); // predecessor unloads; successor becomes Requested, but does not load in same tick.
    const auto successor_progress = runtime.GetProgress(successor.Value().request);
    if (resources->releases != 1 || residency->unloads != 1 || !successor_progress ||
        successor_progress->state != StreamingState::Requested || source->executed.size() != executed_before_release)
    {
        return false;
    }
    (void)runtime.Tick();
    return runtime.GetProgress(first.Value().request)->state == StreamingState::Unloaded &&
           runtime.GetProgress(successor.Value().request)->state == StreamingState::Queued;
}

bool TestMultipleDemandsShareOneUnloadingSuccessor()
{
    auto source = std::make_shared<PlanSource>();
    auto resources = std::make_shared<ResourceTracker>();
    auto residency = std::make_shared<ResidencyTracker>();
    StreamingRuntime runtime(MakeDependencies(source, {}, {}, residency, {}, {}, resources));
    const ChunkId chunk{215};
    const auto first = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!first || !AdvanceTo(runtime, chunk, StreamingState::Active) || !runtime.ReleaseDemand(first.Value()))
    {
        return false;
    }
    (void)runtime.Tick();
    if (runtime.GetChunkState(chunk) != StreamingState::Unloading)
    {
        return false;
    }
    const auto consumer_a = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::High);
    const auto consumer_b = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Critical);
    if (!consumer_a || !consumer_b || consumer_a.Value().request != consumer_b.Value().request)
    {
        return false;
    }
    const auto waiting = runtime.GetProgress(consumer_a.Value().request);
    if (!waiting || waiting->state != StreamingState::WaitingForPredecessor || waiting->demand_count != 2)
    {
        return false;
    }
    (void)runtime.Tick();
    const auto successor = runtime.GetProgress(consumer_a.Value().request);
    return resources->releases == 1 && residency->unloads == 1 && successor &&
           successor->state == StreamingState::Requested && successor->demand_count == 2;
}

bool TestLastWaitingDemandCancelsSuccessor()
{
    auto source = std::make_shared<PlanSource>();
    auto resources = std::make_shared<ResourceTracker>();
    auto residency = std::make_shared<ResidencyTracker>();
    StreamingRuntime runtime(MakeDependencies(source, {}, {}, residency, {}, {}, resources));
    const ChunkId chunk{219};
    const auto first = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!first || !AdvanceTo(runtime, chunk, StreamingState::Active) || !runtime.ReleaseDemand(first.Value()))
    {
        return false;
    }
    (void)runtime.Tick();
    const auto successor = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!successor || !runtime.ReleaseDemand(successor.Value()))
    {
        return false;
    }
    const auto cancelled = runtime.GetProgress(successor.Value().request);
    if (!cancelled || cancelled->state != StreamingState::Cancelled)
    {
        return false;
    }
    (void)runtime.Tick();
    const auto after_unload = runtime.GetProgress(successor.Value().request);
    return resources->releases == 1 && residency->unloads == 1 && after_unload &&
           after_unload->state == StreamingState::Cancelled;
}

bool TestHistoryCleanupPreservesSuccessorMapping()
{
    auto source = std::make_shared<PlanSource>();
    auto resources = std::make_shared<ResourceTracker>();
    auto residency = std::make_shared<ResidencyTracker>();
    StreamingRuntime runtime(MakeDependencies(source, {}, {}, residency, {}, {}, resources));
    const ChunkId chunk{220};
    const auto first = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!first || !AdvanceTo(runtime, chunk, StreamingState::Active) || !runtime.ReleaseDemand(first.Value()))
    {
        return false;
    }
    (void)runtime.Tick();
    const auto successor = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!successor)
    {
        return false;
    }
    (void)runtime.Tick();
    if (!AdvanceTo(runtime, chunk, StreamingState::Active))
    {
        return false;
    }
    runtime.CleanupCompletedRecords(1);
    const auto joined = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    const auto progress = joined ? runtime.GetProgress(joined.Value().request) : std::nullopt;
    return joined && joined.Value().request == successor.Value().request &&
           progress && progress->demand_count == 2 && runtime.RecordCount() == 1;
}

bool TestRollbackFailedRequestIsNotReused()
{
    auto source = std::make_shared<PlanSource>();
    auto commit = std::make_shared<CommitTarget>();
    commit->fail_rollback = true;
    StreamingRuntime runtime(MakeDependencies(source, commit));
    const ChunkId chunk{703};
    const auto first = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!first)
    {
        return false;
    }
    (void)runtime.Tick();
    (void)runtime.Tick();
    (void)runtime.Tick();
    (void)runtime.ReleaseDemand(first.Value());
    const auto second = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    return second && second.Value().request != first.Value().request &&
           runtime.GetProgress(first.Value().request)->state == StreamingState::RollbackFailed;
}

bool TestShutdownRetriesRollbackAndUnloadCleanup()
{
    {
        auto source = std::make_shared<PlanSource>();
        auto commit = std::make_shared<CommitTarget>();
        commit->fail_rollback = true;
        StreamingRuntime runtime(MakeDependencies(source, commit));
        const ChunkId chunk{216};
        const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
        if (!demand)
        {
            return false;
        }
        (void)runtime.Tick();
        (void)runtime.Tick();
        (void)runtime.Tick(); // Loading after first load step.
        const auto failed = runtime.Shutdown();
        const int failed_rollback_attempts = commit->rollbacks;
        if (failed || failed_rollback_attempts == 0 || runtime.GetProgress(demand.Value().request)->state != StreamingState::RollbackFailed)
        {
            return false;
        }
        commit->fail_rollback = false;
        const auto retried = runtime.Shutdown();
        if (!retried || commit->rollbacks != failed_rollback_attempts + 1 || runtime.GetProgress(demand.Value().request)->state != StreamingState::Cancelled)
        {
            return false;
        }
        if (runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{217}}}, StreamingPriorityClass::Normal))
        {
            return false;
        }
        if (!runtime.Shutdown())
        {
            return false;
        }
    }
    {
        auto resources = std::make_shared<ResourceTracker>();
        auto residency = std::make_shared<ResidencyTracker>();
        resources->fail_release_once = true;
        StreamingRuntime runtime(MakeDependencies({}, {}, {}, residency, {}, {}, resources));
        const ChunkId chunk{218};
        const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
        if (!demand || !AdvanceTo(runtime, chunk, StreamingState::Active))
        {
            return false;
        }
        const auto failed = runtime.Shutdown();
        if (failed || resources->releases != 1 || runtime.GetChunkState(chunk) != StreamingState::Unloading)
        {
            return false;
        }
        const auto retried = runtime.Shutdown();
        if (!retried || resources->releases != 2 || residency->unloads != 1 || runtime.GetChunkState(chunk) != StreamingState::Unloaded)
        {
            return false;
        }
    }
    return true;
}


bool TestRequestPublicationAndAllocatorAtomicity()
{
    StreamingRuntime runtime;
    runtime.SetNextRequestValueForTesting(100);
    runtime.SetNextDemandValueForTesting(200);
    runtime.FailNextRequestPublicationForTesting();
    const auto failed = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{801}}}, StreamingPriorityClass::Normal);
    if (failed || !failed.GetError().HasCode("streaming.allocation_failed") || runtime.RecordCount() != 0u ||
        runtime.NextRequestValueForTesting() != 100u || runtime.NextDemandValueForTesting() != 200u)
    {
        return false;
    }
    const auto first = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{801}}}, StreamingPriorityClass::Normal);
    if (!first || first.Value().request.id.value != 100u || first.Value().id.value != 200u) return false;
    const auto before = runtime.GetProgress(first.Value().request);
    runtime.FailNextDemandPublicationForTesting();
    const auto second = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{801}}}, StreamingPriorityClass::High);
    const auto after = runtime.GetProgress(first.Value().request);
    return !second && second.GetError().HasCode("streaming.allocation_failed") && before && after &&
           before->demand_count == after->demand_count && runtime.NextDemandValueForTesting() == 201u;
}

bool TestLastReleaseFailureRetainsOwnershipAndRetries()
{
    auto source = std::make_shared<PlanSource>();
    auto commit = std::make_shared<CommitTarget>();
    commit->fail_rollback = true;
    StreamingRuntime runtime(MakeDependencies(source, commit));
    const ChunkId chunk{802};
    const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!demand) return false;
    (void)runtime.Tick();
    (void)runtime.Tick();
    (void)runtime.Tick();
    const auto failed = runtime.ReleaseDemand(demand.Value());
    const auto pending = runtime.GetProgress(demand.Value().request);
    if (failed || !pending || pending->state != StreamingState::RollbackFailed || pending->demand_count != 1u) return false;
    commit->fail_rollback = false;
    const auto retried = runtime.ReleaseDemand(demand.Value());
    const auto completed = runtime.GetProgress(demand.Value().request);
    return retried && completed && completed->state == StreamingState::Cancelled && completed->demand_count == 0u &&
           runtime.GetStatistics().cancelled == 1u;
}

bool TestCompletionExhaustionLeavesLastDemandAndGraphUntouched()
{
    StreamingRuntime runtime;
    const ChunkId chunk{803};
    const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!demand) return false;
    runtime.SetNextCompletionSequenceForTesting(0);
    const auto failed = runtime.ReleaseDemand(demand.Value());
    const auto progress = runtime.GetProgress(demand.Value().request);
    if (failed || !failed.GetError().HasCode("streaming.completion_sequence_overflow") || !progress ||
        progress->state != StreamingState::Requested || progress->demand_count != 1u) return false;
    runtime.SetNextCompletionSequenceForTesting(1);
    return runtime.ReleaseDemand(demand.Value()).HasValue();
}

bool TestUnloadPhasesDoNotRepeatResourceRelease()
{
    auto resources = std::make_shared<ResourceTracker>();
    auto residency = std::make_shared<ResidencyTracker>();
    residency->fail_unload_once = true;
    StreamingRuntime runtime(MakeDependencies({}, {}, {}, residency, {}, {}, resources));
    const ChunkId chunk{804};
    const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!demand || !AdvanceTo(runtime, chunk, StreamingState::Active)) return false;
    if (!runtime.ReleaseDemand(demand.Value())) return false;
    (void)runtime.Tick(); // Deactivating -> Unloading.
    const auto first_unload = runtime.Tick();
    if (first_unload.failures.empty() || resources->releases != 1 || residency->unloads != 1 ||
        runtime.GetChunkState(chunk) != StreamingState::Unloading) return false;
    const auto retried = runtime.Tick();
    return retried.failures.empty() && resources->releases == 1 && residency->unloads == 2 &&
           runtime.GetChunkState(chunk) == StreamingState::Unloaded;
}

bool TestRollbackPreflightPreventsExternalCallAtSequenceExhaustion()
{
    auto source = std::make_shared<PlanSource>();
    auto commit = std::make_shared<CommitTarget>();
    StreamingRuntime runtime(MakeDependencies(source, commit));
    const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{805}}}, StreamingPriorityClass::Normal);
    if (!demand) return false;
    (void)runtime.Tick();
    (void)runtime.Tick();
    (void)runtime.Tick();
    runtime.SetNextCompletionSequenceForTesting(0);
    const auto failed = runtime.ReleaseDemand(demand.Value());
    return !failed && failed.GetError().HasCode("streaming.completion_sequence_overflow") && commit->rollbacks == 0 &&
           runtime.GetProgress(demand.Value().request)->demand_count == 1u;
}

bool TestRevisionAndPriorityDomainsAreChecked()
{
    StreamingRuntime runtime;
    const auto invalid = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{806}}}, static_cast<StreamingPriorityClass>(99));
    if (invalid || !invalid.GetError().HasCode("streaming.invalid_priority") || runtime.RecordCount() != 0u) return false;

    auto priority = std::make_shared<PriorityProvider>();
    priority->priority = static_cast<StreamingPriorityClass>(-1);
    StreamingRuntime provider_runtime(MakeDependencies({}, {}, priority));
    const auto provider_invalid = provider_runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{807}}}, StreamingPriorityClass::Normal);
    if (provider_invalid || !provider_invalid.GetError().HasCode("streaming.invalid_priority")) return false;

    StreamingRuntime revision_runtime;
    const auto demand = revision_runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{808}}}, StreamingPriorityClass::Normal);
    if (!demand || !revision_runtime.SetRevisionForTesting(demand.Value().request, std::numeric_limits<std::uint64_t>::max())) return false;
    const auto before = revision_runtime.GetProgress(demand.Value().request);
    const auto tick = revision_runtime.Tick();
    const auto after = revision_runtime.GetProgress(demand.Value().request);
    return before && after && tick.failures.size() == 1u && after->revision == before->revision && after->state == before->state;
}

bool TestMalformedPlansAndBuildExceptionsAreRejectedAtomically()
{
    auto source = std::make_shared<PlanSource>();
    source->plan.cursor = 1;
    StreamingRuntime runtime(MakeDependencies(source));
    const auto invalid_cursor = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{809}}}, StreamingPriorityClass::Normal);
    if (invalid_cursor || !invalid_cursor.GetError().HasCode("streaming.invalid_plan") || runtime.RecordCount() != 0u) return false;
    source->plan.cursor = 0;
    source->plan.steps.front().processed_bytes = 1;
    const auto preprocessed = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{809}}}, StreamingPriorityClass::Normal);
    if (preprocessed || !preprocessed.GetError().HasCode("streaming.invalid_plan")) return false;
    source->plan.steps.front().processed_bytes = 0;
    source->plan.steps.front().step = static_cast<StreamingPlanStep>(99);
    const auto invalid_step = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{809}}}, StreamingPriorityClass::Normal);
    if (invalid_step || !invalid_step.GetError().HasCode("streaming.invalid_plan_step")) return false;
    source->plan.steps.front().step = StreamingPlanStep::ResolveTarget;
    source->throw_on_build = true;
    const auto thrown = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{809}}}, StreamingPriorityClass::Normal);
    return !thrown && thrown.GetError().HasCode("streaming.data_source_exception") && runtime.RecordCount() == 0u;
}

bool TestNegativeBudgetNormalizesAndResidencyLifecycleValidates()
{
    StreamingRuntime runtime;
    runtime.SetBudget(StreamingBudget{std::chrono::microseconds{-1}, 3, 4});
    if (runtime.BudgetForTesting().cpu_budget.count() != 0 || runtime.BudgetForTesting().max_requests != 3u) return false;
    InMemoryResidencyController controller;
    const ChunkId chunk{810};
    const auto bad_deactivate = controller.DeactivateChunk(chunk);
    if (bad_deactivate || !bad_deactivate.GetError().HasCode("streaming.invalid_residency_transition")) return false;
    if (!controller.ActivateChunk(chunk) || !controller.ActivateChunk(chunk) || !controller.DeactivateChunk(chunk) ||
        !controller.DeactivateChunk(chunk) || !controller.UnloadChunk(chunk) || !controller.UnloadChunk(chunk)) return false;
    return controller.GetResidencyState(chunk) == StreamingState::Unloaded;
}

bool TestShutdownLifecycleAndExceptionBoundary()
{
    auto residency = std::make_shared<ResidencyTracker>();
    StreamingRuntime runtime(MakeDependencies({}, {}, {}, residency));
    const ChunkId chunk{811};
    const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!demand || !AdvanceTo(runtime, chunk, StreamingState::Active)) return false;
    residency->throw_deactivate = true;
    const auto failed = runtime.Shutdown();
    if (failed || !runtime.IsShuttingDownForTesting()) return false;
    const auto rejected = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{812}}}, StreamingPriorityClass::Normal);
    if (rejected || !rejected.GetError().HasCode("streaming.shutdown")) return false;
    residency->throw_deactivate = false;
    const auto retried = runtime.Shutdown();
    return retried && runtime.Shutdown();
}

bool TestStatisticsSaturateAndStaleDemandChecksRequestGeneration()
{
    StreamingRuntime runtime;
    StreamingStatistics stats{};
    stats.cancelled = std::numeric_limits<std::uint64_t>::max();
    runtime.SetStatisticsForTesting(stats);
    const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{813}}}, StreamingPriorityClass::Normal);
    if (!demand || !runtime.ReleaseDemand(demand.Value()) || runtime.GetStatistics().cancelled != std::numeric_limits<std::uint64_t>::max()) return false;

    StreamingRuntime stale_runtime;
    const auto valid = stale_runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{814}}}, StreamingPriorityClass::Normal);
    if (!valid) return false;
    StreamingDemandHandle stale = valid.Value();
    ++stale.request.generation;
    const auto rejected = stale_runtime.ReleaseDemand(stale);
    const auto progress = stale_runtime.GetProgress(valid.Value().request);
    return !rejected && rejected.GetError().HasCode("streaming.stale_handle") && progress && progress->demand_count == 1u;
}


bool TestWaitingSuccessorCancellationOverflowPreservesGraph()
{
    auto residency = std::make_shared<ResidencyTracker>();
    StreamingRuntime runtime(MakeDependencies({}, {}, {}, residency));
    const ChunkId chunk{815};
    const auto original = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!original || !AdvanceTo(runtime, chunk, StreamingState::Active) || !runtime.ReleaseDemand(original.Value())) return false;
    (void)runtime.Tick(); // Deactivating -> Unloading.
    if (runtime.GetChunkState(chunk) != StreamingState::Unloading) return false;
    const auto successor = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::Normal);
    if (!successor || successor.Value().request == original.Value().request) return false;
    runtime.SetNextCompletionSequenceForTesting(0);
    const auto failed = runtime.ReleaseDemand(successor.Value());
    const auto pending = runtime.GetProgress(successor.Value().request);
    if (failed || !failed.GetError().HasCode("streaming.completion_sequence_overflow") || !pending ||
        pending->state != StreamingState::WaitingForPredecessor || pending->demand_count != 1u) return false;
    const auto joined = runtime.Request(StreamingTarget{ChunkStreamingTarget{chunk}}, StreamingPriorityClass::High);
    const auto after = joined ? runtime.GetProgress(joined.Value().request) : std::nullopt;
    return joined && joined.Value().request == successor.Value().request && after && after->demand_count == 2u;
}

bool TestAllStreamingExtensionExceptionsStayInsideResultBoundary()
{
    {
        auto priority = std::make_shared<PriorityProvider>();
        priority->throw_on_get = true;
        StreamingRuntime runtime(MakeDependencies({}, {}, priority));
        const auto r = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{816}}}, StreamingPriorityClass::Normal);
        if (r || !r.GetError().HasCode("streaming.priority_exception")) return false;
    }
    {
        auto source = std::make_shared<PlanSource>();
        source->throw_on_execute = true;
        StreamingRuntime runtime(MakeDependencies(source));
        const auto d = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{817}}}, StreamingPriorityClass::Normal);
        if (!d) return false;
        (void)runtime.Tick(); (void)runtime.Tick();
        const auto t = runtime.Tick();
        if (t.failures.empty() || runtime.GetProgress(d.Value().request)->state != StreamingState::Failed) return false;
    }
    {
        auto world = std::make_shared<WorldTracker>();
        world->throw_resolve = true;
        StreamingRuntime runtime(MakeDependencies({}, {}, {}, {}, world));
        const auto d = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{818}}}, StreamingPriorityClass::Normal);
        if (!d) return false;
        (void)runtime.Tick(); (void)runtime.Tick();
        const auto t = runtime.Tick();
        if (t.failures.empty()) return false;
    }
    {
        auto persistence = std::make_shared<PersistenceTracker>();
        persistence->throw_prepare = true;
        StreamingRuntime runtime(MakeDependencies({}, {}, {}, {}, {}, persistence));
        const auto d = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{819}}}, StreamingPriorityClass::Normal);
        if (!d) return false;
        (void)runtime.Tick(); (void)runtime.Tick(); (void)runtime.Tick();
        const auto t = runtime.Tick();
        if (t.failures.empty()) return false;
    }
    {
        auto resources = std::make_shared<ResourceTracker>();
        resources->throw_prepare = true;
        StreamingRuntime runtime(MakeDependencies({}, {}, {}, {}, {}, {}, resources));
        const auto d = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{820}}}, StreamingPriorityClass::Normal);
        if (!d) return false;
        (void)runtime.Tick(); (void)runtime.Tick(); (void)runtime.Tick(); (void)runtime.Tick();
        const auto t = runtime.Tick();
        if (t.failures.empty()) return false;
    }
    {
        auto source = std::make_shared<PlanSource>();
        source->plan.steps = {StreamingPlanStepRecord{StreamingPlanStep::Commit, 1}};
        auto commit = std::make_shared<CommitTarget>();
        commit->throw_commit = true;
        StreamingRuntime runtime(MakeDependencies(source, commit));
        const auto d = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{821}}}, StreamingPriorityClass::Normal);
        if (!d) return false;
        (void)runtime.Tick(); (void)runtime.Tick();
        const auto t = runtime.Tick();
        if (t.failures.empty()) return false;
    }
    {
        auto source = std::make_shared<PlanSource>();
        source->fail_step = StreamingPlanStep::ResolveTarget;
        auto commit = std::make_shared<CommitTarget>();
        commit->throw_rollback = true;
        StreamingRuntime runtime(MakeDependencies(source, commit));
        const auto d = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{822}}}, StreamingPriorityClass::Normal);
        if (!d) return false;
        (void)runtime.Tick(); (void)runtime.Tick();
        const auto t = runtime.Tick();
        if (t.failures.empty() || runtime.GetProgress(d.Value().request)->state != StreamingState::RollbackFailed) return false;
    }
    {
        auto residency = std::make_shared<ResidencyTracker>();
        residency->throw_activate = true;
        StreamingRuntime runtime(MakeDependencies({}, {}, {}, residency));
        const auto d = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{823}}}, StreamingPriorityClass::Normal);
        if (!d || !AdvanceTo(runtime, ChunkId{823}, StreamingState::Activating)) return false;
        const auto t = runtime.Tick();
        if (t.failures.empty() || runtime.GetChunkState(ChunkId{823}) != StreamingState::Activating) return false;
        residency->throw_activate = false;
        if (!runtime.Tick().failures.empty() || runtime.GetChunkState(ChunkId{823}) != StreamingState::Resident) return false;
    }
    return true;
}


bool TestStreamingIdentityAllocatorExhaustionBoundaries()
{
    {
        StreamingRuntime runtime;
        runtime.SetNextRequestValueForTesting(0);
        const auto r = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{824}}}, StreamingPriorityClass::Normal);
        if (r || !r.GetError().HasCode("streaming.id_overflow") || runtime.RecordCount() != 0u) return false;
    }
    {
        StreamingRuntime runtime;
        runtime.SetNextDemandValueForTesting(0);
        const auto r = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{825}}}, StreamingPriorityClass::Normal);
        if (r || !r.GetError().HasCode("streaming.id_overflow") || runtime.RecordCount() != 0u) return false;
    }
    {
        StreamingRuntime runtime;
        runtime.SetNextRequestValueForTesting(std::numeric_limits<std::uint64_t>::max());
        const auto first = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{826}}}, StreamingPriorityClass::Normal);
        if (!first || first.Value().request.id.value != std::numeric_limits<std::uint64_t>::max()) return false;
        const auto second = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{827}}}, StreamingPriorityClass::Normal);
        if (second || !second.GetError().HasCode("streaming.id_overflow")) return false;
    }
    {
        StreamingRuntime runtime;
        const auto d = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{828}}}, StreamingPriorityClass::Normal);
        if (!d || !runtime.SetRevisionForTesting(d.Value().request, std::numeric_limits<std::uint64_t>::max() - 1u)) return false;
        const auto first_tick = runtime.Tick();
        const auto p1 = runtime.GetProgress(d.Value().request);
        if (!first_tick.failures.empty() || !p1 || p1->revision != std::numeric_limits<std::uint64_t>::max()) return false;
        const auto second_tick = runtime.Tick();
        const auto p2 = runtime.GetProgress(d.Value().request);
        if (second_tick.failures.empty() || !p2 || p2->revision != std::numeric_limits<std::uint64_t>::max()) return false;
    }
    return true;
}

bool TestLargeStepReceivesAvailableBudget()
{
    auto source = std::make_shared<PlanSource>();
    source->respect_budget = true;
    source->plan.steps = {StreamingPlanStepRecord{StreamingPlanStep::PrepareData, 1000}};
    StreamingRuntime runtime(MakeDependencies(source));
    runtime.SetBudget(StreamingBudget{std::chrono::microseconds{100}, 1, 100});
    const auto demand = runtime.Request(StreamingTarget{ChunkStreamingTarget{ChunkId{704}}}, StreamingPriorityClass::Normal);
    if (!demand)
    {
        return false;
    }
    (void)runtime.Tick();
    (void)runtime.Tick();
    const auto progressed = runtime.Tick();
    const auto progress = runtime.GetProgress(demand.Value().request);
    return progressed.processed_bytes == 100 && source->last_budget.max_bytes == 100 &&
           progress && progress->processed_bytes == 100 && progress->state == StreamingState::Loading;
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
    if (!TestPartialStepUsesRemainingByteBudget()) return 16;
    if (!TestZeroProcessedIncompleteStepDoesNotAutoComplete()) return 17;
    if (!TestCommitRunsOnlyAfterCompletedStepAndOnlyOnce()) return 18;
    if (!TestCommitBudgetValidationRunsBeforeExternalCommit()) return 29;
    if (!TestByteCounterOverflowIsRejectedBeforeCommit()) return 30;
    if (!TestTargetVariantsRejectUnsupportedInReference()) return 8;
    if (!TestRequestGenerationAndStaleDemand()) return 9;
    if (!TestRecordCleanup()) return 10;
    if (!TestRollbackFailureMarksRollbackFailed()) return 14;
    if (!TestServicesFactoryUsesDependencies()) return 11;
    if (!TestServicesFactoryPassesAllDependencies()) return 25;
    if (!TestResolvedPriorityPersistsAcrossDemands()) return 19;
    if (!TestDemandDuringUnloadReactivatesExistingRequest()) return 20;
    if (!TestDemandDuringUnloadingWaitsForPredecessor()) return 23;
    if (!TestMultipleDemandsShareOneUnloadingSuccessor()) return 26;
    if (!TestLastWaitingDemandCancelsSuccessor()) return 27;
    if (!TestHistoryCleanupPreservesSuccessorMapping()) return 28;
    if (!TestRollbackFailedRequestIsNotReused()) return 21;
    if (!TestShutdownRetriesRollbackAndUnloadCleanup()) return 24;
    if (!TestRequestPublicationAndAllocatorAtomicity()) return 31;
    if (!TestLastReleaseFailureRetainsOwnershipAndRetries()) return 32;
    if (!TestCompletionExhaustionLeavesLastDemandAndGraphUntouched()) return 33;
    if (!TestUnloadPhasesDoNotRepeatResourceRelease()) return 34;
    if (!TestRollbackPreflightPreventsExternalCallAtSequenceExhaustion()) return 35;
    if (!TestRevisionAndPriorityDomainsAreChecked()) return 36;
    if (!TestMalformedPlansAndBuildExceptionsAreRejectedAtomically()) return 37;
    if (!TestNegativeBudgetNormalizesAndResidencyLifecycleValidates()) return 38;
    if (!TestShutdownLifecycleAndExceptionBoundary()) return 39;
    if (!TestStatisticsSaturateAndStaleDemandChecksRequestGeneration()) return 40;
    if (!TestWaitingSuccessorCancellationOverflowPreservesGraph()) return 41;
    if (!TestAllStreamingExtensionExceptionsStayInsideResultBoundary()) return 42;
    if (!TestStreamingIdentityAllocatorExhaustionBoundaries()) return 43;
    if (!TestLargeStepReceivesAvailableBudget()) return 22;
    return 0;
}
