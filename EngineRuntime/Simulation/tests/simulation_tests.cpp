#include "simulation_runtime_impl.h"

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Foundation/string_id.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>
#include <vector>

using epidemic::runtime::GameDuration;
using epidemic::runtime::RegionId;
using epidemic::runtime::RuntimeObjectId;
using epidemic::runtime::SimulationZoneId;
using epidemic::runtime::simulation::AttentionScore;
using epidemic::runtime::simulation::AbstractFact;
using epidemic::runtime::simulation::CreateSimulationServices;
using epidemic::runtime::simulation::IAbstractFactStore;
using epidemic::runtime::simulation::IRelevancePolicy;
using epidemic::runtime::simulation::ISimulationCommitTarget;
using epidemic::runtime::simulation::ISimulationClock;
using epidemic::runtime::simulation::ISimulationJob;
using epidemic::runtime::simulation::MemoryLifetime;
using epidemic::runtime::simulation::ObservationState;
using epidemic::runtime::simulation::ProposalDiscardReason;
using epidemic::runtime::simulation::ScheduledSimulationTask;
using epidemic::runtime::simulation::ScheduledSimulationTaskId;
using epidemic::runtime::simulation::SimulationBudget;
using epidemic::runtime::simulation::SimulationDependencies;
using epidemic::runtime::simulation::SimulationJobDesc;
using epidemic::runtime::simulation::SimulationJobHandle;
using epidemic::runtime::simulation::SimulationJobState;
using epidemic::runtime::simulation::SimulationLane;
using epidemic::runtime::simulation::SimulationOptions;
using epidemic::runtime::simulation::SimulationProposal;
using epidemic::runtime::simulation::SimulationProposalBatch;
using epidemic::runtime::simulation::SimulationRuntime;
using epidemic::runtime::simulation::SimulationStepResult;
using epidemic::runtime::simulation::SimulationTime;
using epidemic::runtime::simulation::SimulationZoneState;
using epidemic::runtime::simulation::WorldMemoryEvent;
using epidemic::runtime::simulation::WorldMemoryEventId;
using epidemic::runtime::simulation::WorldMemoryQuery;

namespace
{
struct TestCommitTarget final : ISimulationCommitTarget
{
    bool fail = false;
    std::vector<SimulationProposalBatch> committed;

    epidemic::foundation::Result<void> Commit(const SimulationProposalBatch& batch) override
    {
        if (fail)
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("simulation.stale_revision", "test stale revision"));
        }
        committed.push_back(batch);
        return epidemic::foundation::Result<void>::Success();
    }
};

struct TestRelevancePolicy final : IRelevancePolicy
{
    SimulationZoneState state = SimulationZoneState::Relevant;
    int calls = 0;

    SimulationZoneState ClassifyZone(RegionId, AttentionScore) const override
    {
        ++const_cast<TestRelevancePolicy*>(this)->calls;
        return state;
    }
};

struct TestClock final : ISimulationClock
{
    SimulationTime now{};

    SimulationTime Now() const override
    {
        return now;
    }
};

struct TestSimulationJob final : ISimulationJob
{
    SimulationJobDesc desc{};
    std::uint32_t pending_work_units = 1;
    bool fail_step = false;
    bool fail_cancel = false;
    bool cancel_called = false;
    bool return_invalid_state = false;
    bool exceed_budget = false;
    bool wait_for_main_thread = false;

    explicit TestSimulationJob(SimulationJobDesc metadata)
        : desc(metadata), pending_work_units(metadata.work_units)
    {
    }

    epidemic::foundation::Result<SimulationStepResult> ExecuteStep(const epidemic::runtime::RuntimeBudget& budget) override
    {
        if (fail_step)
        {
            return epidemic::foundation::Result<SimulationStepResult>::Failure(
                epidemic::foundation::Error::Create("simulation.test_failed", "test job step failed"));
        }

        const std::uint32_t work_budget = budget.max_items == 0 ? pending_work_units : std::min<std::uint32_t>(budget.max_items, pending_work_units);
        pending_work_units -= work_budget;

        SimulationStepResult result{};
        result.consumed_work_units = work_budget;
        if (exceed_budget)
        {
            result.consumed_work_units = budget.max_items + 1;
        }
        result.proposals.zone = desc.zone;
        result.proposals.source_revision = desc.source_revision;
        if (return_invalid_state)
        {
            result.state = SimulationJobState::Scheduled;
            return epidemic::foundation::Result<SimulationStepResult>::Success(std::move(result));
        }
        if (pending_work_units == 0)
        {
            result.state = wait_for_main_thread ? SimulationJobState::WaitingForMainThread : SimulationJobState::Completed;
            result.proposals.proposals.push_back(SimulationProposal{
                epidemic::foundation::StringId::FromString("simulation"),
                epidemic::foundation::StringId::FromString("test"),
                desc.subject,
                epidemic::foundation::StringId::FromString("simulation.test.v1"),
                1,
                {}});
        }
        else
        {
            result.state = SimulationJobState::PartiallyComplete;
        }
        return epidemic::foundation::Result<SimulationStepResult>::Success(std::move(result));
    }

    epidemic::foundation::Result<void> Cancel() override
    {
        cancel_called = true;
        if (fail_cancel)
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("simulation.cancel_failed", "test job cancel failed"));
        }
        pending_work_units = 0;
        return epidemic::foundation::Result<void>::Success();
    }
};

bool Expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }

    return condition;
}

SimulationJobDesc MakeJob(std::uint32_t work_units = 1)
{
    return SimulationJobDesc{SimulationZoneId{5}, RuntimeObjectId{77}, work_units};
}

std::shared_ptr<TestSimulationJob> MakeExecutableJob(const SimulationJobDesc& desc, bool wait_for_main_thread = false)
{
    auto job = std::make_shared<TestSimulationJob>(desc);
    job->wait_for_main_thread = wait_for_main_thread;
    return job;
}

bool TestSubmitJobAndCompletion()
{
    SimulationRuntime runtime{{}};
    runtime.SetBudget(SimulationBudget{1, 2});
    const auto desc = MakeJob(2);
    const auto job = runtime.SubmitJob(MakeExecutableJob(desc), desc);
    bool ok = Expect(job.HasValue(), "valid job should submit");
    ok &= Expect(runtime.GetJobState(job.Value()).Value() == SimulationJobState::Pending, "new job should be pending");
    const auto tick = runtime.Tick();
    ok &= Expect(tick.HasValue() && tick.Value().processed_jobs == 1, "tick should advance one job");
    ok &= Expect(runtime.GetJobState(job.Value()).Value() == SimulationJobState::Completed, "job should complete within budget");
    return ok;
}

bool TestPartialCompletionByBudget()
{
    SimulationRuntime runtime{{}};
    runtime.SetBudget(SimulationBudget{1, 1});
    const auto desc = MakeJob(3);
    const auto job = runtime.SubmitJob(MakeExecutableJob(desc), desc);
    bool ok = Expect(job.HasValue(), "valid job should submit");
    ok &= Expect(runtime.Tick().Value().processed_jobs == 1, "first tick should do partial work");
    ok &= Expect(runtime.GetJobState(job.Value()).Value() == SimulationJobState::PartiallyComplete, "job should be partial when budget ends");
    ok &= Expect(runtime.Tick().Value().processed_jobs == 1, "second tick should do more partial work");
    ok &= Expect(runtime.GetJobState(job.Value()).Value() == SimulationJobState::PartiallyComplete, "job should remain partial until final work unit");
    ok &= Expect(runtime.Tick().Value().processed_jobs == 1, "third tick should finish remaining work");
    ok &= Expect(runtime.GetJobState(job.Value()).Value() == SimulationJobState::Completed, "job should complete after enough budget");
    return ok;
}

bool TestJobBudgetOrderIsDeterministic()
{
    SimulationRuntime runtime{{}};
    runtime.SetBudget(SimulationBudget{1, 1});
    const auto first_desc = MakeJob(2);
    const auto second_desc = MakeJob(1);
    const auto first = runtime.SubmitJob(MakeExecutableJob(first_desc), first_desc);
    const auto second = runtime.SubmitJob(MakeExecutableJob(second_desc), second_desc);
    bool ok = Expect(first.HasValue() && second.HasValue(), "jobs should submit");
    ok &= Expect(runtime.Tick().Value().processed_jobs == 1, "budget should advance one job");
    ok &= Expect(runtime.GetJobState(first.Value()).Value() == SimulationJobState::PartiallyComplete, "first job should advance first");
    ok &= Expect(runtime.GetJobState(second.Value()).Value() == SimulationJobState::Pending, "second job should wait");
    return ok;
}

bool TestCancellation()
{
    SimulationRuntime runtime{{}};
    const auto desc = MakeJob(2);
    const auto executable = MakeExecutableJob(desc);
    const auto job = runtime.SubmitJob(executable, desc);
    bool ok = Expect(job.HasValue(), "valid job should submit");
    ok &= Expect(runtime.CancelJob(job.Value()).HasValue(), "pending job should cancel");
    ok &= Expect(executable->cancel_called, "job cancel should call executable cancellation");
    ok &= Expect(runtime.GetJobState(job.Value()).Value() == SimulationJobState::Cancelled, "cancelled job should report cancelled");
    ok &= Expect(runtime.Tick().Value().processed_jobs == 0, "cancelled job should not consume work");
    return ok;
}

bool TestWorldMemoryTtlAndStates()
{
    SimulationRuntime runtime{{}};
    const auto observed = runtime.RecordEvent(WorldMemoryEvent{{}, RegionId{1}, SimulationTime{10}, GameDuration{5}, MemoryLifetime::Temporary, ObservationState::Observed});
    const auto marked = runtime.RecordEvent(WorldMemoryEvent{{}, RegionId{1}, SimulationTime{11}, GameDuration{0}, MemoryLifetime::Persistent, ObservationState::PlayerAffected});
    bool ok = Expect(observed.HasValue() && marked.HasValue(), "memory events should record");
    ok &= Expect(runtime.QueryEvents(WorldMemoryQuery{RegionId{1}, false}).size() == 2, "query should return active events");
    runtime.ExpireOldEvents(SimulationTime{16});
    ok &= Expect(runtime.QueryEvents(WorldMemoryQuery{RegionId{1}, false}).size() == 1, "expired TTL event should be hidden by default");
    ok &= Expect(runtime.QueryEvents(WorldMemoryQuery{RegionId{1}, true}).size() == 2, "include expired should return both events");
    return ok;
}

bool TestWorldMemoryExpirationBudget()
{
    SimulationRuntime runtime{{}};
    const auto first = runtime.RecordEvent(WorldMemoryEvent{{}, RegionId{1}, SimulationTime{10}, GameDuration{1}, MemoryLifetime::Temporary, ObservationState::Unobserved});
    const auto second = runtime.RecordEvent(WorldMemoryEvent{{}, RegionId{1}, SimulationTime{11}, GameDuration{1}, MemoryLifetime::Temporary, ObservationState::Unobserved});
    bool ok = Expect(first.HasValue() && second.HasValue(), "memory events should record");
    ok &= Expect(runtime.ExpireOldEvents(SimulationTime{20}, 1) == 1, "expiration budget should expire one event");
    ok &= Expect(runtime.QueryEvents(WorldMemoryQuery{RegionId{1}, false}).size() == 1, "one event should remain active");
    ok &= Expect(runtime.ExpireOldEvents(SimulationTime{20}, 1) == 1, "second budgeted pass should expire next event");
    ok &= Expect(runtime.QueryEvents(WorldMemoryQuery{RegionId{1}, false}).empty(), "no active events should remain");
    return ok;
}

bool TestWorldMemoryIdAndTtlValidation()
{
    SimulationRuntime runtime{{}};
    WorldMemoryEvent explicit_event{WorldMemoryEventId{10}, RegionId{1}, SimulationTime{10}, GameDuration{1}, MemoryLifetime::Temporary, ObservationState::Unobserved};
    const auto first = runtime.RecordEvent(explicit_event);
    const auto duplicate = runtime.RecordEvent(explicit_event);
    const auto generated = runtime.RecordEvent(WorldMemoryEvent{{}, RegionId{1}, SimulationTime{11}, GameDuration{1}, MemoryLifetime::Temporary, ObservationState::Unobserved});
    const auto negative_ttl = runtime.RecordEvent(WorldMemoryEvent{{}, RegionId{1}, SimulationTime{12}, GameDuration{-1}, MemoryLifetime::Temporary, ObservationState::Unobserved});
    const auto negative_time = runtime.RecordEvent(WorldMemoryEvent{{}, RegionId{1}, SimulationTime{-1}, GameDuration{1}, MemoryLifetime::Temporary, ObservationState::Unobserved});
    return first && first.Value().value == 10 &&
           !duplicate && duplicate.GetError().HasCode("simulation.memory_event_already_exists") &&
           generated && generated.Value().value == 11 &&
           !negative_ttl && negative_ttl.GetError().HasCode("simulation.invalid_memory_ttl") &&
           !negative_time && negative_time.GetError().HasCode("simulation.invalid_memory_time");
}

bool TestAttentionAndValidationFailures()
{
    SimulationRuntime runtime{{}};
    bool ok = Expect(runtime.SetAttention(RuntimeObjectId{7}, AttentionScore{0.75f}).HasValue(), "object attention should set");
    ok &= Expect(runtime.GetAttention(RuntimeObjectId{7}).value == 0.75f, "object attention should read");
    ok &= Expect(runtime.SetRegionAttention(RegionId{3}, AttentionScore{0.25f}).HasValue(), "region attention should set");
    ok &= Expect(runtime.GetRegionAttention(RegionId{3}).value == 0.25f, "region attention should read");
    ok &= Expect(!runtime.SubmitJob(MakeExecutableJob(MakeJob()), SimulationJobDesc{}).HasValue(), "invalid job should fail");
    ok &= Expect(!runtime.SubmitJob(nullptr, MakeJob()).HasValue(), "null executable job should fail");
    ok &= Expect(!runtime.RecordEvent(WorldMemoryEvent{}).HasValue(), "invalid memory event should fail");
    ok &= Expect(!runtime.SetAttention(RuntimeObjectId{7}, AttentionScore{std::numeric_limits<float>::quiet_NaN()}).HasValue(),
                 "NaN attention should fail");
    ok &= Expect(!runtime.SetAttention(RuntimeObjectId{7}, AttentionScore{std::numeric_limits<float>::infinity()}).HasValue(),
                 "positive infinity attention should fail");
    ok &= Expect(!runtime.SetRegionAttention(RegionId{3}, AttentionScore{-std::numeric_limits<float>::infinity()}).HasValue(),
                 "negative infinity attention should fail");
    return ok;
}

bool TestBackendOrientedContractsAndServices()
{
    SimulationRuntime runtime{{}};
    const auto desc = SimulationJobDesc{SimulationZoneId{5}, RuntimeObjectId{77}, 2, SimulationLane::Object, 12};
    const auto handle = runtime.SubmitJob(MakeExecutableJob(desc), desc);
    bool ok = Expect(handle.HasValue(), "job handle should submit");
    ok &= Expect(handle.HasValue() && handle.Value().generation != 0, "job handle should carry generation");

    SimulationJobHandle stale = handle.Value();
    ++stale.generation;
    ok &= Expect(!runtime.CancelJob(stale).HasValue(), "stale job handle should fail cancellation");
    ok &= Expect(!runtime.GetJobState(stale).HasValue(), "stale job state query should fail");
    ok &= Expect(runtime.CancelJob(handle.Value()).HasValue(), "fresh job handle should cancel");

    const auto persistent = runtime.RecordEvent(WorldMemoryEvent{
        {},
        RegionId{2},
        SimulationTime{1},
        GameDuration{1},
        MemoryLifetime::Persistent,
        ObservationState::Observed});
    ok &= Expect(persistent.HasValue(), "persistent memory event should record");
    ok &= Expect(runtime.ExpireOldEvents(SimulationTime{10}, 1) == 0, "persistent lifetime should not expire");

    SimulationProposal proposal{
        epidemic::foundation::StringId::FromString("simulation"),
        epidemic::foundation::StringId::FromString("test"),
        RuntimeObjectId{77},
        epidemic::foundation::StringId::FromString("simulation.test.v1"),
        1,
        {}};
    SimulationProposalBatch batch{handle.Value(), desc.zone, desc.source_revision, {proposal}};
    ScheduledSimulationTask task{{}, handle.Value(), SimulationTime{25}, SimulationLane::Background};
    AbstractFact fact{
        epidemic::foundation::StringId::FromString("simulation"),
        epidemic::foundation::StringId::FromString("test"),
        1,
        RuntimeObjectId{77},
        SimulationZoneId{5},
        SimulationTime{25}};
    ok &= Expect(batch.proposals.size() == 1 && task.job == handle.Value() && fact.subject == RuntimeObjectId{77},
                 "proposal, task and fact contracts should be value-stable");

    const auto services = CreateSimulationServices();
    ok &= Expect(services.HasValue(), "simulation services should be created");
    ok &= Expect(services.HasValue() && services.Value().runtime != nullptr && services.Value().scheduler != nullptr &&
                     services.Value().attention != nullptr && services.Value().memory != nullptr &&
                     services.Value().facts != nullptr && services.Value().proposals != nullptr &&
                     services.Value().scheduled_tasks != nullptr,
                 "simulation services should be populated");
    return ok;
}

bool TestProposalCommitAtomicityAndFaultIsolation()
{
    auto commit = std::make_shared<TestCommitTarget>();
    SimulationRuntime runtime{{}, SimulationDependencies{{}, commit, {}}};
    runtime.SetBudget(SimulationBudget{1, 1});
    const auto desc = MakeJob(1);
    const auto job = runtime.SubmitJob(MakeExecutableJob(desc), desc);
    bool ok = Expect(job.HasValue(), "job should submit for proposal commit test");
    ok &= Expect(runtime.Tick().Value().processed_jobs == 1, "real ExecuteStep should complete job");
    ok &= Expect(runtime.GetJobState(job.Value()).Value() == SimulationJobState::Completed, "job should complete");
    ok &= Expect(runtime.PendingBatches().size() == 1, "completed job should publish proposal batch");

    commit->fail = true;
    const auto failed_commit = runtime.CommitNext();
    ok &= Expect(!failed_commit.HasValue() && failed_commit.GetError().HasCode("simulation.stale_revision"),
                 "stale revision should reject proposal commit");
    ok &= Expect(runtime.PendingBatches().size() == 1 && commit->committed.empty(),
                 "failed commit should keep batch pending and apply nothing");

    commit->fail = false;
    ok &= Expect(runtime.CommitNext().HasValue(), "valid proposal batch should commit");
    ok &= Expect(runtime.PendingBatches().empty() && commit->committed.size() == 1,
                 "successful commit should remove exactly one batch");

    const auto cancelled_desc = MakeJob(1);
    const auto cancelled = runtime.SubmitJob(MakeExecutableJob(cancelled_desc), cancelled_desc);
    ok &= Expect(cancelled.HasValue(), "job should submit for cancel fault isolation");
    ok &= Expect(runtime.CancelJob(cancelled.Value()).HasValue(), "job should cancel");
    ok &= Expect(runtime.Tick().Value().processed_jobs == 0, "cancelled job should publish no proposal batch");
    ok &= Expect(runtime.PendingBatches().empty(), "cancelled job should leave proposals empty");
    return ok;
}

SimulationProposal MakeValidProposal(RuntimeObjectId target = RuntimeObjectId{77})
{
    return SimulationProposal{
        epidemic::foundation::StringId::FromString("simulation"),
        epidemic::foundation::StringId::FromString("test"),
        target,
        epidemic::foundation::StringId::FromString("simulation.test.v1"),
        1,
        {}};
}

bool TestProposalPublicationStateMatrix()
{
    SimulationRuntime runtime{{}};
    const auto pending_desc = MakeJob(1);
    const auto pending = runtime.SubmitJob(MakeExecutableJob(pending_desc), pending_desc);
    bool ok = Expect(pending.HasValue(), "pending matrix job should submit");
    ok &= Expect(!runtime.Publish(SimulationProposalBatch{pending.Value(), pending_desc.zone, pending_desc.source_revision, {MakeValidProposal()}}).HasValue(),
                 "pending job should not publish proposals");

    const auto scheduled_desc = SimulationJobDesc{SimulationZoneId{5}, RuntimeObjectId{78}, 1};
    const auto scheduled = runtime.SubmitJob(MakeExecutableJob(scheduled_desc), scheduled_desc);
    ok &= Expect(scheduled.HasValue(), "scheduled matrix job should submit");
    ok &= Expect(runtime.Schedule(ScheduledSimulationTask{{}, scheduled.Value(), SimulationTime{10}, SimulationLane::Background}).HasValue(),
                 "scheduled matrix job should schedule");
    ok &= Expect(!runtime.Publish(SimulationProposalBatch{scheduled.Value(), scheduled_desc.zone, scheduled_desc.source_revision, {MakeValidProposal(RuntimeObjectId{78})}}).HasValue(),
                 "scheduled job should not publish proposals");

    const auto running_desc = SimulationJobDesc{SimulationZoneId{5}, RuntimeObjectId{81}, 1};
    const auto running = runtime.SubmitJob(MakeExecutableJob(running_desc), running_desc);
    ok &= Expect(running.HasValue(), "running matrix job should submit");
    runtime.SetJobStateForTesting(running.Value(), SimulationJobState::Running);
    ok &= Expect(!runtime.Publish(SimulationProposalBatch{running.Value(), running_desc.zone, running_desc.source_revision, {MakeValidProposal(RuntimeObjectId{81})}}).HasValue(),
                 "running job should not publish proposals");

    SimulationRuntime failed_runtime{{}};
    const auto failed_desc = SimulationJobDesc{SimulationZoneId{5}, RuntimeObjectId{79}, 1};
    auto failed_job = MakeExecutableJob(failed_desc);
    failed_job->fail_step = true;
    const auto failed = failed_runtime.SubmitJob(failed_job, failed_desc);
    ok &= Expect(failed.HasValue(), "failed matrix job should submit");
    ok &= Expect(failed_runtime.Tick().HasValue(), "failed matrix job should tick");
    ok &= Expect(!failed_runtime.Publish(SimulationProposalBatch{failed.Value(), failed_desc.zone, failed_desc.source_revision, {MakeValidProposal(RuntimeObjectId{79})}}).HasValue(),
                 "failed job should not publish proposals");

    const auto cancelled_desc = SimulationJobDesc{SimulationZoneId{5}, RuntimeObjectId{80}, 1};
    const auto cancelled = runtime.SubmitJob(MakeExecutableJob(cancelled_desc), cancelled_desc);
    ok &= Expect(cancelled.HasValue() && runtime.CancelJob(cancelled.Value()).HasValue(), "cancelled matrix job should cancel");
    ok &= Expect(!runtime.Publish(SimulationProposalBatch{cancelled.Value(), cancelled_desc.zone, cancelled_desc.source_revision, {MakeValidProposal(RuntimeObjectId{80})}}).HasValue(),
                 "cancelled job should not publish proposals");

    SimulationRuntime completed_runtime{{}};
    const auto completed_desc = MakeJob(1);
    const auto completed = completed_runtime.SubmitJob(MakeExecutableJob(completed_desc), completed_desc);
    ok &= Expect(completed.HasValue() && completed_runtime.Tick().HasValue(), "completed matrix job should complete");
    ok &= Expect(completed_runtime.Publish(SimulationProposalBatch{completed.Value(), completed_desc.zone, completed_desc.source_revision, {MakeValidProposal()}}).HasValue(),
                 "completed job should publish proposals");

    SimulationRuntime main_runtime{{}};
    const auto main_desc = MakeJob(1);
    const auto main_job = main_runtime.SubmitJob(MakeExecutableJob(main_desc, true), main_desc);
    ok &= Expect(main_job.HasValue() && main_runtime.Tick().HasValue(), "main-thread matrix job should wait");
    ok &= Expect(main_runtime.Publish(SimulationProposalBatch{main_job.Value(), main_desc.zone, main_desc.source_revision, {MakeValidProposal()}}).HasValue(),
                 "main-thread waiting job should publish proposals");
    return ok;
}

bool TestAbstractFactRevision()
{
    SimulationRuntime runtime{{}};
    AbstractFact first{
        epidemic::foundation::StringId::FromString("simulation"),
        epidemic::foundation::StringId::FromString("fact"),
        1,
        RuntimeObjectId{77},
        SimulationZoneId{5},
        SimulationTime{10}};
    AbstractFact second = first;
    second.fact_type = 2;

    bool ok = Expect(runtime.RecordFact(first).HasValue(), "first fact should record");
    ok &= Expect(runtime.RecordFact(second).HasValue(), "second fact should record");
    const auto facts = runtime.QueryFacts(SimulationZoneId{5});
    ok &= Expect(runtime.Revision() == 2, "fact store revision should advance");
    ok &= Expect(facts.size() == 2 && facts[0].revision == 1 && facts[1].revision == 2,
                 "facts should carry publication revisions");
    return ok;
}

bool TestScheduledTaskBudget()
{
    SimulationRuntime runtime{{}};
    const auto first_desc = MakeJob(1);
    const auto second_desc = MakeJob(1);
    const auto first_job = runtime.SubmitJob(MakeExecutableJob(first_desc), first_desc);
    const auto second_job = runtime.SubmitJob(MakeExecutableJob(second_desc), second_desc);
    bool ok = Expect(first_job.HasValue() && second_job.HasValue(), "jobs should submit for scheduled tasks");
    const auto first = runtime.Schedule(ScheduledSimulationTask{{}, first_job.Value(), SimulationTime{10}, SimulationLane::Background});
    const auto second = runtime.Schedule(ScheduledSimulationTask{{}, second_job.Value(), SimulationTime{10}, SimulationLane::Background});
    ok &= Expect(first.HasValue() && second.HasValue(), "scheduled tasks should register");
    ok &= Expect(!runtime.Schedule(ScheduledSimulationTask{{}, first_job.Value(), SimulationTime{11}, SimulationLane::Background}).HasValue(),
                 "duplicate scheduled task for same job should fail");
    ok &= Expect(runtime.QueryDue(SimulationTime{9}).empty(), "tasks should not be due early");
    ok &= Expect(runtime.QueryDue(SimulationTime{10}).size() == 2, "both tasks should be due");
    const auto executed = runtime.ExecuteDueWithinBudget(SimulationTime{10}, SimulationBudget{1, 0});
    ok &= Expect(executed.activated == 1 && executed.failures.empty(),
                 "scheduled task budget should execute one");
    ok &= Expect(runtime.GetJobState(first_job.Value()).Value() == SimulationJobState::Pending,
                 "due scheduled task should make job runnable instead of cancelling it");
    ok &= Expect(runtime.QueryDue(SimulationTime{10}).size() == 1, "one scheduled task should remain after budgeted execution");
    ok &= Expect(runtime.Cancel(second.Value()).HasValue(), "remaining scheduled task should cancel");
    ok &= Expect(runtime.GetJobState(second_job.Value()).Value() == SimulationJobState::Pending,
                 "cancelled scheduled task should return job to pending");
    ok &= Expect(runtime.QueryDue(SimulationTime{10}).empty(), "cancelled scheduled task should disappear");

    ScheduledSimulationTaskId missing{999};
    ok &= Expect(!runtime.Cancel(missing).HasValue(), "unknown scheduled task cancellation should fail");
    return ok;
}

bool TestCancelJobRemovesScheduledTask()
{
    SimulationRuntime runtime{{}};
    const auto desc = MakeJob(1);
    const auto job = runtime.SubmitJob(MakeExecutableJob(desc), desc);
    bool ok = Expect(job.HasValue(), "scheduled cancellation job should submit");
    const auto scheduled = runtime.Schedule(ScheduledSimulationTask{{}, job.Value(), SimulationTime{10}, SimulationLane::Background});
    ok &= Expect(scheduled.HasValue(), "job should schedule before direct cancellation");
    ok &= Expect(runtime.CancelJob(job.Value()).HasValue(), "direct job cancellation should succeed");
    ok &= Expect(runtime.QueryDue(SimulationTime{10}).empty(), "direct job cancellation should remove active schedule");
    ok &= Expect(runtime.GetJobState(job.Value()).Value() == SimulationJobState::Cancelled,
                 "directly cancelled scheduled job should be terminal");
    return ok;
}

bool TestInvalidDueTaskClearsActiveSchedule()
{
    SimulationRuntime runtime{{}};
    const auto desc = MakeJob(1);
    const auto job = runtime.SubmitJob(MakeExecutableJob(desc), desc);
    bool ok = Expect(job.HasValue(), "invalid due task job should submit");
    const auto scheduled = runtime.Schedule(ScheduledSimulationTask{{}, job.Value(), SimulationTime{10}, SimulationLane::Background});
    ok &= Expect(scheduled.HasValue(), "invalid due task job should schedule");
    runtime.SetJobStateForTesting(job.Value(), SimulationJobState::Pending);

    const auto executed = runtime.ExecuteDueWithinBudget(SimulationTime{10}, SimulationBudget{1, 1});
    ok &= Expect(executed.activated == 0 && executed.failures.size() == 1,
                 "invalid scheduled task should report a failure without activation");
    ok &= Expect(!runtime.ActiveScheduleForTesting(job.Value()).has_value(),
                 "invalid scheduled task should clear active schedule");
    ok &= Expect(runtime.Schedule(ScheduledSimulationTask{{}, job.Value(), SimulationTime{20}, SimulationLane::Background}).HasValue(),
                 "job should be schedulable again after invalid due cleanup");
    return ok;
}

bool TestTickFailuresCancellationAndMainThreadCommits()
{
    SimulationRuntime runtime{{}};
    const auto failing_desc = MakeJob(1);
    auto failing_job = MakeExecutableJob(failing_desc);
    failing_job->fail_step = true;
    const auto failing = runtime.SubmitJob(failing_job, failing_desc);
    bool ok = Expect(failing.HasValue(), "failing job should submit");
    const auto failed_tick = runtime.Tick();
    ok &= Expect(failed_tick.HasValue() && failed_tick.Value().failures.size() == 1,
                 "tick should surface job failures");
    ok &= Expect(failed_tick.Value().failures[0].error.HasCode("simulation.test_failed"),
                 "tick failure should carry executable error");

    const auto invalid_desc = MakeJob(1);
    auto invalid_job = MakeExecutableJob(invalid_desc);
    invalid_job->return_invalid_state = true;
    const auto invalid_handle = runtime.SubmitJob(invalid_job, invalid_desc);
    ok &= Expect(invalid_handle.HasValue(), "invalid-transition job should submit");
    const auto invalid_tick = runtime.Tick();
    ok &= Expect(invalid_tick.HasValue() && !invalid_tick.Value().failures.empty() &&
                     invalid_tick.Value().failures.back().error.HasCode("simulation.invalid_job_transition"),
                 "tick should reject scheduler-owned step states");

    SimulationRuntime budget_runtime{{}};
    budget_runtime.SetBudget(SimulationBudget{1, 1});
    const auto over_desc = MakeJob(1);
    auto over_job = MakeExecutableJob(over_desc);
    over_job->exceed_budget = true;
    const auto over_handle = budget_runtime.SubmitJob(over_job, over_desc);
    ok &= Expect(over_handle.HasValue(), "over-budget job should submit");
    const auto over_tick = budget_runtime.Tick();
    ok &= Expect(over_tick.HasValue() && over_tick.Value().failures.size() == 1 &&
                     over_tick.Value().failures[0].error.HasCode("simulation.work_budget_exceeded"),
                 "tick should reject over-budget work consumption");

    const auto cancel_desc = MakeJob(1);
    auto cancel_job = MakeExecutableJob(cancel_desc);
    cancel_job->fail_cancel = true;
    const auto cancel_handle = runtime.SubmitJob(cancel_job, cancel_desc);
    ok &= Expect(cancel_handle.HasValue(), "cancel-failing job should submit");
    const auto cancel = runtime.CancelJob(cancel_handle.Value());
    ok &= Expect(!cancel.HasValue() && cancel.GetError().HasCode("simulation.cancel_failed"),
                 "cancel failure should propagate");
    ok &= Expect(runtime.GetJobState(cancel_handle.Value()).Value() == SimulationJobState::Pending,
                 "failed cancellation should leave job non-terminal");

    SimulationRuntime main_runtime{{}};
    SimulationJobDesc main_desc = MakeJob(1);
    const auto main_handle = main_runtime.SubmitJob(MakeExecutableJob(main_desc, true), main_desc);
    ok &= Expect(main_handle.HasValue(), "main-thread job should submit");
    ok &= Expect(main_runtime.Tick().Value().processed_jobs == 1, "main-thread job should execute worker phase");
    ok &= Expect(main_runtime.GetJobState(main_handle.Value()).Value() == SimulationJobState::WaitingForMainThread,
                 "main-thread job should wait for commit phase");
    const auto committed = main_runtime.ProcessMainThreadCommits();
    ok &= Expect(committed.HasValue() && committed.Value().processed_jobs == 1 &&
                     committed.Value().proposal_batches.size() == 1,
                 "main-thread commit phase should publish proposal batch");
    ok &= Expect(main_runtime.GetJobState(main_handle.Value()).Value() == SimulationJobState::Completed,
                 "main-thread commit should complete job");
    return ok;
}

bool TestProposalValidationClockRelevanceAndCleanup()
{
    auto relevance = std::make_shared<TestRelevancePolicy>();
    auto clock = std::make_shared<TestClock>();
    clock->now = SimulationTime{10};
    SimulationOptions options{};
    options.max_terminal_jobs = 1;
    options.max_memory_events = 1;
    options.max_facts = 1;
    options.max_proposal_batches = 1;
    SimulationRuntime runtime{options, SimulationDependencies{relevance, {}, clock}};

    bool ok = Expect(runtime.SetRegionAttention(RegionId{3}, AttentionScore{0.75f}).HasValue(),
                     "region attention should update relevance");
    ok &= Expect(relevance->calls == 1 && runtime.GetRegionZoneState(RegionId{3}) == SimulationZoneState::Relevant,
                 "relevance policy should classify region state");

    const auto scheduled_desc = MakeJob(1);
    const auto scheduled_job = runtime.SubmitJob(MakeExecutableJob(scheduled_desc), scheduled_desc);
    ok &= Expect(scheduled_job.HasValue(), "scheduled job should submit");
    ok &= Expect(runtime.Schedule(ScheduledSimulationTask{{}, scheduled_job.Value(), SimulationTime{10}, SimulationLane::Background}).HasValue(),
                 "scheduled job should register");
    ok &= Expect(runtime.GetJobState(scheduled_job.Value()).Value() == SimulationJobState::Scheduled,
                 "scheduled job should wait until due");
    const auto tick = runtime.Tick();
    ok &= Expect(tick.HasValue() && tick.Value().processed_jobs == 1,
                 "clock-backed tick should activate and execute due job");
    ok &= Expect(runtime.GetJobState(scheduled_job.Value()).Value() == SimulationJobState::Completed,
                 "due scheduled job should complete through normal worker path");

    const auto desc = MakeJob(1);
    const auto job = runtime.SubmitJob(MakeExecutableJob(desc), desc);
    ok &= Expect(job.HasValue(), "proposal validation job should submit");
    SimulationProposal valid{
        epidemic::foundation::StringId::FromString("simulation"),
        epidemic::foundation::StringId::FromString("test"),
        RuntimeObjectId{77},
        epidemic::foundation::StringId::FromString("simulation.test.v1"),
        1,
        {}};
    ok &= Expect(!runtime.Publish(SimulationProposalBatch{job.Value(), SimulationZoneId{99}, desc.source_revision, {valid}}).HasValue(),
                 "proposal zone mismatch should fail");
    SimulationProposal invalid_schema = valid;
    invalid_schema.schema_version = 0;
    ok &= Expect(!runtime.Publish(SimulationProposalBatch{job.Value(), desc.zone, desc.source_revision, {invalid_schema}}).HasValue(),
                 "proposal schema validation should fail");

    SimulationRuntime queue_runtime{SimulationOptions{.max_proposal_batches = 1}};
    const auto queue_desc = MakeJob(1);
    const auto queue_job = queue_runtime.SubmitJob(MakeExecutableJob(queue_desc, true), queue_desc);
    ok &= Expect(queue_job.HasValue(), "queue-full job should submit");
    ok &= Expect(queue_runtime.Tick().HasValue(), "queue-full job should reach publishable state");
    ok &= Expect(queue_runtime.Publish(SimulationProposalBatch{queue_job.Value(), queue_desc.zone, queue_desc.source_revision, {valid}}).HasValue(),
                 "first proposal should fit bounded queue");
    ok &= Expect(!queue_runtime.Publish(SimulationProposalBatch{queue_job.Value(), queue_desc.zone, queue_desc.source_revision, {valid}}).HasValue(),
                 "bounded proposal queue should report overflow instead of discarding");

    ok &= Expect(runtime.RecordFact(AbstractFact{
                     epidemic::foundation::StringId::FromString("simulation"),
                     epidemic::foundation::StringId::FromString("fact"),
                     1,
                     RuntimeObjectId{77},
                     SimulationZoneId{5},
                     SimulationTime{1}})
                     .HasValue(),
                 "first fact should record");
    const auto overflow_fact = runtime.RecordFact(AbstractFact{
        epidemic::foundation::StringId::FromString("simulation"),
        epidemic::foundation::StringId::FromString("fact"),
        2,
        RuntimeObjectId{78},
        SimulationZoneId{5},
        SimulationTime{2}});
    ok &= Expect(!overflow_fact.HasValue() && overflow_fact.GetError().HasCode("simulation.fact_store_full"),
                 "fact store should reject overflow instead of pruning authoritative facts");
    ok &= Expect(runtime.QueryFacts(SimulationZoneId{5}).size() == 1, "fact store should retain existing authoritative fact");

    ok &= Expect(runtime.RecordEvent(WorldMemoryEvent{{}, RegionId{1}, SimulationTime{1}, GameDuration{1}, MemoryLifetime::Temporary, ObservationState::Observed}).HasValue(),
                 "first bounded memory event should record");
    const auto memory_overflow =
        runtime.RecordEvent(WorldMemoryEvent{{}, RegionId{1}, SimulationTime{2}, GameDuration{1}, MemoryLifetime::Temporary, ObservationState::Observed});
    ok &= Expect(!memory_overflow.HasValue() && memory_overflow.GetError().HasCode("simulation.memory_store_full"),
                 "bounded memory event store should reject active overflow");
    runtime.ExpireOldEvents(SimulationTime{10});
    ok &= Expect(runtime.QueryEvents(WorldMemoryQuery{RegionId{1}, true}).size() <= 1,
                 "expired memory storage should prune to configured bound");
    return ok;
}

bool TestMemoryFactShutdownAndProposalCleanupEdges()
{
    bool ok = true;
    {
        SimulationRuntime runtime{SimulationOptions{.max_memory_events = 1}};
        ok &= Expect(runtime.RecordEvent(WorldMemoryEvent{{}, RegionId{1}, SimulationTime{1}, GameDuration{0}, MemoryLifetime::Persistent, ObservationState::Observed}).HasValue(),
                     "persistent memory event should record into bounded store");
        const auto full = runtime.RecordEvent(WorldMemoryEvent{{}, RegionId{1}, SimulationTime{2}, GameDuration{0}, MemoryLifetime::Persistent, ObservationState::Observed});
        ok &= Expect(!full.HasValue() && full.GetError().HasCode("simulation.memory_store_full"),
                     "bounded memory store should not evict persistent events silently");
    }
    {
        SimulationRuntime runtime{{}};
        AbstractFact valid{
            epidemic::foundation::StringId::FromString("simulation"),
            epidemic::foundation::StringId::FromString("fact"),
            1,
            RuntimeObjectId{77},
            SimulationZoneId{5},
            SimulationTime{1}};
        AbstractFact invalid = valid;
        invalid.domain = {};
        ok &= Expect(!runtime.RecordFact(invalid).HasValue(), "invalid fact domain should fail");
        invalid = valid;
        invalid.kind = {};
        ok &= Expect(!runtime.RecordFact(invalid).HasValue(), "invalid fact kind should fail");
        invalid = valid;
        invalid.fact_type = 0;
        ok &= Expect(!runtime.RecordFact(invalid).HasValue(), "invalid fact type should fail");
        invalid = valid;
        invalid.observed_at = SimulationTime{-1};
        ok &= Expect(!runtime.RecordFact(invalid).HasValue(), "negative fact observation time should fail");
    }
    {
        SimulationRuntime runtime{{}};
        const auto desc = MakeJob(1);
        auto executable = MakeExecutableJob(desc);
        executable->fail_cancel = true;
        const auto job = runtime.SubmitJob(executable, desc);
        ok &= Expect(job.HasValue(), "shutdown retry job should submit");
        const auto failed = runtime.Shutdown();
        ok &= Expect(!failed.HasValue() && failed.GetError().HasCode("simulation.cancel_failed"),
                     "shutdown should report failed executable cancellation");
        executable->fail_cancel = false;
        ok &= Expect(runtime.Shutdown().HasValue(), "shutdown should retry failed cancellation on next call");
    }
    {
        SimulationRuntime runtime{SimulationOptions{.max_terminal_jobs = 1}};
        runtime.SetBudget(SimulationBudget{2, 2});
        const auto first_desc = MakeJob(1);
        const auto second_desc = SimulationJobDesc{SimulationZoneId{5}, RuntimeObjectId{78}, 1};
        const auto first = runtime.SubmitJob(MakeExecutableJob(first_desc), first_desc);
        const auto second = runtime.SubmitJob(MakeExecutableJob(second_desc), second_desc);
        ok &= Expect(first.HasValue() && second.HasValue(), "proposal retention jobs should submit");
        ok &= Expect(runtime.Tick().HasValue(), "first retention tick should complete jobs");
        ok &= Expect(runtime.PendingBatches().size() == 2, "terminal jobs should retain pending proposal batches");
        ok &= Expect(runtime.GetJobState(first.Value()).HasValue(), "terminal job with pending proposal should not prune");
        ok &= Expect(runtime.DiscardAll(ProposalDiscardReason::Shutdown).HasValue(), "administrative discard should clear proposals");
        ok &= Expect(!runtime.GetJobState(first.Value()).HasValue() || !runtime.GetJobState(second.Value()).HasValue(),
                     "discarded proposal should immediately allow terminal job pruning");
    }
    {
        auto target = std::make_shared<TestCommitTarget>();
        SimulationRuntime runtime{SimulationOptions{.max_terminal_jobs = 1}, SimulationDependencies{.relevance = {}, .commit_target = target, .clock = {}}};
        runtime.SetBudget(SimulationBudget{2, 2});
        const auto first_desc = MakeJob(1);
        const auto second_desc = SimulationJobDesc{SimulationZoneId{5}, RuntimeObjectId{79}, 1};
        const auto first = runtime.SubmitJob(MakeExecutableJob(first_desc), first_desc);
        const auto second = runtime.SubmitJob(MakeExecutableJob(second_desc), second_desc);
        ok &= Expect(first.HasValue() && second.HasValue(), "commit-prune jobs should submit");
        ok &= Expect(runtime.Tick().HasValue(), "commit-prune tick should complete jobs");
        ok &= Expect(runtime.CommitNext().HasValue(), "first commit should succeed");
        ok &= Expect(runtime.GetJobState(first.Value()).HasValue(), "first committed terminal job should remain within cap");
        ok &= Expect(runtime.CommitNext().HasValue(), "second commit should prune immediately");
        ok &= Expect(!runtime.GetJobState(first.Value()).HasValue(), "commit should immediately allow terminal job pruning");
    }
    return ok;
}
} // namespace

int main()
{
    static_assert(std::is_abstract_v<IRelevancePolicy>);
    static_assert(std::is_abstract_v<ISimulationJob>);
    static_assert(std::is_abstract_v<ISimulationCommitTarget>);
    static_assert(std::is_abstract_v<IAbstractFactStore>);

    bool ok = true;
    ok &= TestSubmitJobAndCompletion();
    ok &= TestPartialCompletionByBudget();
    ok &= TestJobBudgetOrderIsDeterministic();
    ok &= TestCancellation();
    ok &= TestWorldMemoryTtlAndStates();
    ok &= TestWorldMemoryExpirationBudget();
    ok &= TestWorldMemoryIdAndTtlValidation();
    ok &= TestAttentionAndValidationFailures();
    ok &= TestBackendOrientedContractsAndServices();
    ok &= TestProposalCommitAtomicityAndFaultIsolation();
    ok &= TestProposalPublicationStateMatrix();
    ok &= TestAbstractFactRevision();
    ok &= TestScheduledTaskBudget();
    ok &= TestCancelJobRemovesScheduledTask();
    ok &= TestInvalidDueTaskClearsActiveSchedule();
    ok &= TestTickFailuresCancellationAndMainThreadCommits();
    ok &= TestProposalValidationClockRelevanceAndCleanup();
    ok &= TestMemoryFactShutdownAndProposalCleanupEdges();
    return ok ? 0 : 1;
}
