#include "simulation_runtime_impl.h"

#include <iostream>
#include <string_view>
#include <type_traits>

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
using epidemic::runtime::simulation::SimulationBudget;
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
using epidemic::runtime::simulation::WorldMemoryEventState;
using epidemic::runtime::simulation::WorldMemoryQuery;

namespace
{
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
    const auto observed = runtime.RecordEvent(WorldMemoryEvent{{}, RegionId{1}, SimulationTime{10}, GameDuration{5}, WorldMemoryEventState::Observed});
    const auto marked = runtime.RecordEvent(WorldMemoryEvent{{}, RegionId{1}, SimulationTime{11}, GameDuration{0}, WorldMemoryEventState::PlayerAffected});
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
    const auto first = runtime.RecordEvent(WorldMemoryEvent{{}, RegionId{1}, SimulationTime{10}, GameDuration{1}, WorldMemoryEventState::Temporary});
    const auto second = runtime.RecordEvent(WorldMemoryEvent{{}, RegionId{1}, SimulationTime{11}, GameDuration{1}, WorldMemoryEventState::Temporary});
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
        WorldMemoryEventState::Temporary,
        MemoryLifetime::Persistent,
        ObservationState::Observed});
    ok &= Expect(persistent.HasValue(), "persistent memory event should record");
    ok &= Expect(runtime.ExpireOldEvents(SimulationTime{10}, 1) == 0, "persistent lifetime should not expire");

    SimulationStepInput input{handle.Value(), SimulationZoneId{5}, RuntimeObjectId{77}, SimulationLane::Object, 1, 12};
    SimulationProposalBatch batch{input.job, input.source_revision, {SimulationProposal{input.subject, input.zone, 42}}};
    ScheduledSimulationTask task{handle.Value(), SimulationTime{25}, SimulationLane::Background};
    AbstractFact fact{1, RuntimeObjectId{77}, SimulationZoneId{5}, SimulationTime{25}};
    ok &= Expect(batch.proposals.size() == 1 && task.job == handle.Value() && fact.subject == RuntimeObjectId{77},
                 "proposal, task and fact contracts should be value-stable");

    const auto services = CreateSimulationServices();
    ok &= Expect(services.runtime != nullptr && services.attention != nullptr && services.memory != nullptr &&
                     services.effects != nullptr,
                 "simulation services should be populated");
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
    return ok ? 0 : 1;
}
