#include "simulation_runtime_impl.h"

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Foundation/string_id.h"

#include <iostream>
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
using epidemic::runtime::simulation::ISimulationJob;
using epidemic::runtime::simulation::MemoryLifetime;
using epidemic::runtime::simulation::ObservationState;
using epidemic::runtime::simulation::ScheduledSimulationTask;
using epidemic::runtime::simulation::ScheduledSimulationTaskId;
using epidemic::runtime::simulation::SimulationBudget;
using epidemic::runtime::simulation::SimulationDependencies;
using epidemic::runtime::simulation::SimulationEffect;
using epidemic::runtime::simulation::SimulationJobDesc;
using epidemic::runtime::simulation::SimulationJobHandle;
using epidemic::runtime::simulation::SimulationJobState;
using epidemic::runtime::simulation::SimulationLane;
using epidemic::runtime::simulation::SimulationProposal;
using epidemic::runtime::simulation::SimulationProposalBatch;
using epidemic::runtime::simulation::SimulationRuntime;
using epidemic::runtime::simulation::SimulationStepInput;
using epidemic::runtime::simulation::SimulationTime;
using epidemic::runtime::simulation::WorldMemoryEvent;
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
    return SimulationJobDesc{SimulationZoneId{5}, RuntimeObjectId{77}, work_units, false};
}

bool TestSubmitJobAndCompletion()
{
    SimulationRuntime runtime{{}};
    runtime.SetBudget(SimulationBudget{1, 2});
    const auto job = runtime.SubmitJob(MakeJob(2));
    bool ok = Expect(job.HasValue(), "valid job should submit");
    ok &= Expect(runtime.GetJobState(job.Value()) == SimulationJobState::Pending, "new job should be pending");
    ok &= Expect(runtime.Tick() == 1, "tick should advance one job");
    ok &= Expect(runtime.GetJobState(job.Value()) == SimulationJobState::Completed, "job should complete within budget");
    return ok;
}

bool TestPartialCompletionByBudget()
{
    SimulationRuntime runtime{{}};
    runtime.SetBudget(SimulationBudget{1, 1});
    const auto job = runtime.SubmitJob(MakeJob(3));
    bool ok = Expect(job.HasValue(), "valid job should submit");
    ok &= Expect(runtime.Tick() == 1, "first tick should do partial work");
    ok &= Expect(runtime.GetJobState(job.Value()) == SimulationJobState::PartiallyComplete, "job should be partial when budget ends");
    ok &= Expect(runtime.Tick() == 1, "second tick should do more partial work");
    ok &= Expect(runtime.GetJobState(job.Value()) == SimulationJobState::PartiallyComplete, "job should remain partial until final work unit");
    ok &= Expect(runtime.Tick() == 1, "third tick should finish remaining work");
    ok &= Expect(runtime.GetJobState(job.Value()) == SimulationJobState::Completed, "job should complete after enough budget");
    return ok;
}

bool TestJobBudgetOrderIsDeterministic()
{
    SimulationRuntime runtime{{}};
    runtime.SetBudget(SimulationBudget{1, 1});
    const auto first = runtime.SubmitJob(MakeJob(2));
    const auto second = runtime.SubmitJob(MakeJob(1));
    bool ok = Expect(first.HasValue() && second.HasValue(), "jobs should submit");
    ok &= Expect(runtime.Tick() == 1, "budget should advance one job");
    ok &= Expect(runtime.GetJobState(first.Value()) == SimulationJobState::PartiallyComplete, "first job should advance first");
    ok &= Expect(runtime.GetJobState(second.Value()) == SimulationJobState::Pending, "second job should wait");
    return ok;
}

bool TestCancellation()
{
    SimulationRuntime runtime{{}};
    const auto job = runtime.SubmitJob(MakeJob(2));
    bool ok = Expect(job.HasValue(), "valid job should submit");
    ok &= Expect(runtime.CancelJob(job.Value()).HasValue(), "pending job should cancel");
    ok &= Expect(runtime.GetJobState(job.Value()) == SimulationJobState::Cancelled, "cancelled job should report cancelled");
    ok &= Expect(runtime.Tick() == 0, "cancelled job should not consume work");
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

bool TestEffectBufferOrderAndClear()
{
    SimulationRuntime runtime{{}};
    bool ok = Expect(runtime.Submit(SimulationEffect{RuntimeObjectId{1}, SimulationZoneId{1}, 100}).HasValue(), "first effect should submit");
    ok &= Expect(runtime.Submit(SimulationEffect{RuntimeObjectId{2}, SimulationZoneId{1}, 200}).HasValue(), "second effect should submit");
    ok &= Expect(runtime.Effects().size() == 2, "effects should keep order");
    ok &= Expect(runtime.Effects()[0].effect_type == 100 && runtime.Effects()[1].effect_type == 200, "effect order should match submission order");
    runtime.Clear();
    ok &= Expect(runtime.Effects().empty(), "effects should clear");
    return ok;
}

bool TestAttentionAndValidationFailures()
{
    SimulationRuntime runtime{{}};
    bool ok = Expect(runtime.SetAttention(RuntimeObjectId{7}, AttentionScore{0.75f}).HasValue(), "object attention should set");
    ok &= Expect(runtime.GetAttention(RuntimeObjectId{7}).value == 0.75f, "object attention should read");
    ok &= Expect(runtime.SetRegionAttention(RegionId{3}, AttentionScore{0.25f}).HasValue(), "region attention should set");
    ok &= Expect(runtime.GetRegionAttention(RegionId{3}).value == 0.25f, "region attention should read");
    ok &= Expect(!runtime.SubmitJob(SimulationJobDesc{}).HasValue(), "invalid job should fail");
    ok &= Expect(!runtime.RecordEvent(WorldMemoryEvent{}).HasValue(), "invalid memory event should fail");
    ok &= Expect(!runtime.Submit(SimulationEffect{}).HasValue(), "invalid effect should fail");
    return ok;
}

bool TestBackendOrientedContractsAndServices()
{
    SimulationRuntime runtime{{}};
    const auto handle = runtime.SubmitJobHandle(SimulationJobDesc{SimulationZoneId{5}, RuntimeObjectId{77}, 2, false, SimulationLane::Object, 12});
    bool ok = Expect(handle.HasValue(), "job handle should submit");
    ok &= Expect(handle.HasValue() && handle.Value().generation != 0, "job handle should carry generation");

    SimulationJobHandle stale = handle.Value();
    ++stale.generation;
    ok &= Expect(!runtime.CancelJob(stale).HasValue(), "stale job handle should fail cancellation");
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

    SimulationStepInput input{handle.Value(), SimulationZoneId{5}, RuntimeObjectId{77}, SimulationLane::Object, 1, 12};
    SimulationProposal proposal{
        epidemic::foundation::StringId::FromString("simulation"),
        epidemic::foundation::StringId::FromString("test"),
        input.subject,
        epidemic::foundation::StringId::FromString("simulation.test.v1"),
        1,
        {}};
    SimulationProposalBatch batch{input.job, input.zone, input.source_revision, {proposal}};
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
                     services.Value().scheduled_tasks != nullptr && services.Value().effects != nullptr,
                 "simulation services should be populated");
    return ok;
}

bool TestProposalCommitAtomicityAndFaultIsolation()
{
    auto commit = std::make_shared<TestCommitTarget>();
    SimulationRuntime runtime{{}, SimulationDependencies{{}, commit, {}}};
    runtime.SetBudget(SimulationBudget{1, 1});
    const auto job = runtime.SubmitJobHandle(MakeJob(1));
    bool ok = Expect(job.HasValue(), "job should submit for proposal commit test");
    ok &= Expect(runtime.Tick() == 1, "real ExecuteStep should complete job");
    ok &= Expect(runtime.GetJobState(job.Value().id) == SimulationJobState::Completed, "job should complete");
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

    const auto cancelled = runtime.SubmitJobHandle(MakeJob(1));
    ok &= Expect(cancelled.HasValue(), "job should submit for cancel fault isolation");
    ok &= Expect(runtime.CancelJob(cancelled.Value()).HasValue(), "job should cancel");
    ok &= Expect(runtime.Tick() == 0, "cancelled job should publish no proposal batch");
    ok &= Expect(runtime.PendingBatches().empty(), "cancelled job should leave proposals empty");
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
    const auto first_job = runtime.SubmitJobHandle(MakeJob(1));
    const auto second_job = runtime.SubmitJobHandle(MakeJob(1));
    bool ok = Expect(first_job.HasValue() && second_job.HasValue(), "jobs should submit for scheduled tasks");
    const auto first = runtime.Schedule(ScheduledSimulationTask{{}, first_job.Value(), SimulationTime{10}, SimulationLane::Background});
    const auto second = runtime.Schedule(ScheduledSimulationTask{{}, second_job.Value(), SimulationTime{10}, SimulationLane::Background});
    ok &= Expect(first.HasValue() && second.HasValue(), "scheduled tasks should register");
    ok &= Expect(runtime.QueryDue(SimulationTime{9}).empty(), "tasks should not be due early");
    ok &= Expect(runtime.QueryDue(SimulationTime{10}).size() == 2, "both tasks should be due");
    ok &= Expect(runtime.ExecuteDueWithinBudget(SimulationTime{10}, SimulationBudget{1, 0}) == 1,
                 "scheduled task budget should execute one");
    ok &= Expect(runtime.QueryDue(SimulationTime{10}).size() == 1, "one scheduled task should remain after budgeted execution");
    ok &= Expect(runtime.Cancel(second.Value()).HasValue(), "remaining scheduled task should cancel");
    ok &= Expect(runtime.QueryDue(SimulationTime{10}).empty(), "cancelled scheduled task should disappear");

    ScheduledSimulationTaskId missing{999};
    ok &= Expect(!runtime.Cancel(missing).HasValue(), "unknown scheduled task cancellation should fail");
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
    ok &= TestEffectBufferOrderAndClear();
    ok &= TestAttentionAndValidationFailures();
    ok &= TestBackendOrientedContractsAndServices();
    ok &= TestProposalCommitAtomicityAndFaultIsolation();
    ok &= TestAbstractFactRevision();
    ok &= TestScheduledTaskBudget();
    return ok ? 0 : 1;
}
