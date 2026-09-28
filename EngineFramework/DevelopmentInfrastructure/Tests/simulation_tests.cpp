#include "../../GameplayWorldStateOwners/Simulation/src/simulation_test_seam.h"
#include "Epidemic/GameFramework/Simulation/simulation.h"
#include "Epidemic/Foundation/error.h"

#include <cstdint>
#include <limits>
#include <utility>

using namespace epidemic;
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::simulation;

namespace
{
foundation::Error TestError(const char *code)
{
    return foundation::Error::Create(code, "test failure");
}

class Exec final : public ISimulationLayerExecutor
{
  public:
    explicit Exec(const char *name, std::uint64_t operations = 3) : id(SimulationLayerId::FromString(name)), operations_(operations)
    {
    }

    int prepares = 0;
    int commits = 0;
    int fail_prepare_count = 0;
    int fail_commit_count = 0;
    SimulationLayerId id{};

    [[nodiscard]] SimulationLayerId Layer() const noexcept override
    {
        return id;
    }

    [[nodiscard]] foundation::Result<SimulationLayerSummary> Prepare(const SimulationTask &) override
    {
        ++prepares;
        if (fail_prepare_count > 0)
        {
            --fail_prepare_count;
            return foundation::Result<SimulationLayerSummary>::Failure(TestError("test.prepare"));
        }
        return foundation::Result<SimulationLayerSummary>::Success(
            {id, SimulationTaskState::Running, operations_, {1}, {}});
    }

    [[nodiscard]] foundation::Result<void> Commit(const SimulationTask &, const SimulationLayerSummary &) override
    {
        ++commits;
        if (fail_commit_count > 0)
        {
            --fail_commit_count;
            return foundation::Result<void>::Failure(TestError("test.commit"));
        }
        return foundation::Result<void>::Success();
    }

  private:
    std::uint64_t operations_ = 0;
};

SimulationRegion Region(const char *name)
{
    SimulationRegion region;
    region.id = SimulationRegionId::FromString(name);
    region.canonical_name = name;
    region.area = {GameplayDomainId::FromString("test"), GameplayObjectId::FromString(name)};
    region.detail_level = SimulationDetailLevel::Abstract;
    return region;
}

SimulationLayerDefinition Layer(const Exec &exec, int order)
{
    return {exec.id, exec.id == SimulationLayerId::FromString("test.layer.a") ? "test.layer.a" :
                     exec.id == SimulationLayerId::FromString("test.layer.b") ? "test.layer.b" : "test.layer.c",
            {}, order, SimulationMaterializationPolicy::AbstractCapable, {}};
}


bool EquivalentMutationState(const SimulationSnapshot &a, const SimulationSnapshot &b)
{
    if (a.revision != b.revision || a.task_ids.scope != b.task_ids.scope || a.task_ids.next != b.task_ids.next ||
        a.summary_ids.scope != b.summary_ids.scope || a.summary_ids.next != b.summary_ids.next ||
        a.next_change_sequence != b.next_change_sequence || a.change_epoch != b.change_epoch ||
        a.regions.size() != b.regions.size() || a.active_intervals.size() != b.active_intervals.size() ||
        a.summaries.size() != b.summaries.size())
        return false;
    for (std::size_t i = 0; i < a.regions.size(); ++i)
        if (a.regions[i].id != b.regions[i].id || a.regions[i].last_simulated_at != b.regions[i].last_simulated_at ||
            a.regions[i].revision != b.regions[i].revision)
            return false;
    for (std::size_t i = 0; i < a.active_intervals.size(); ++i)
    {
        const auto &left = a.active_intervals[i];
        const auto &right = b.active_intervals[i];
        if (left.id != right.id || left.region != right.region || left.state != right.state ||
            left.revision != right.revision || left.layers.size() != right.layers.size())
            return false;
        for (std::size_t j = 0; j < left.layers.size(); ++j)
            if (left.layers[j].task.id != right.layers[j].task.id ||
                left.layers[j].task.state != right.layers[j].task.state ||
                left.layers[j].task.revision != right.layers[j].task.revision ||
                left.layers[j].prepared != right.layers[j].prepared ||
                left.layers[j].prepared_summary.state != right.layers[j].prepared_summary.state ||
                left.layers[j].prepared_summary.revision != right.layers[j].prepared_summary.revision)
                return false;
    }
    return true;
}
} // namespace

int main()
{
    // Basic success, exact replay idempotency and strict interval chaining.
    SimulationService basic;
    Exec basic_exec("test.layer.a");
    auto region = basic.RegisterRegion(Region("test.region"));
    if (!region)
        return 1;
    if (!basic.RegisterLayer(Layer(basic_exec, 10), &basic_exec))
        return 2;
    basic.FreezeDefinitions();
    auto first = basic.SimulateInterval(region.Value(), GameplayTimePoint{0}, GameplayTimePoint{10});
    if (!first || basic_exec.commits != 1)
        return 3;
    auto replay = basic.SimulateInterval(region.Value(), GameplayTimePoint{0}, GameplayTimePoint{10});
    if (!replay || replay.Value() != first.Value() || basic_exec.commits != 1)
        return 4;
    if (basic.SimulateInterval(region.Value(), GameplayTimePoint{5}, GameplayTimePoint{15}))
        return 5;
    auto next = basic.SimulateInterval(region.Value(), GameplayTimePoint{10}, GameplayTimePoint{20});
    if (!next || basic_exec.commits != 2)
        return 6;

    // H43: regions are runtime state and remain creatable after definition freeze.
    auto runtime_region = basic.RegisterRegion(Region("test.runtime_region"));
    if (!runtime_region)
        return 7;
    Exec late_layer("test.layer.b");
    if (basic.RegisterLayer(Layer(late_layer, 20), &late_layer))
        return 8;

    // C06/H42: late commit failure keeps the region cursor unchanged and retry does not repeat
    // an already committed earlier layer.
    SimulationService retry;
    Exec layer_a("test.layer.a", 1);
    Exec layer_b("test.layer.b", 2);
    layer_b.fail_commit_count = 1;
    auto retry_region = retry.RegisterRegion(Region("test.retry_region"));
    if (!retry_region || !retry.RegisterLayer(Layer(layer_a, 10), &layer_a) ||
        !retry.RegisterLayer(Layer(layer_b, 20), &layer_b))
        return 9;
    retry.Freeze();
    auto failed = retry.SimulateInterval(retry_region.Value(), GameplayTimePoint{0}, GameplayTimePoint{100});
    if (failed || layer_a.commits != 1 || layer_b.commits != 1)
        return 10;
    const auto *retry_state = retry.FindRegion(retry_region.Value());
    if (!retry_state || retry_state->last_simulated_at.ticks != 0 || retry.FindActiveInterval(retry_region.Value()) == nullptr)
        return 11;
    auto resumed = retry.SimulateInterval(retry_region.Value(), GameplayTimePoint{0}, GameplayTimePoint{100});
    if (!resumed || layer_a.commits != 1 || layer_b.commits != 2)
        return 12;
    retry_state = retry.FindRegion(retry_region.Value());
    if (!retry_state || retry_state->last_simulated_at.ticks != 100 || retry.FindActiveInterval(retry_region.Value()) != nullptr)
        return 13;

    // Budget exhaustion pauses a durable execution and resumes it without replaying committed legs.
    SimulationService budgeted;
    Exec budget_a("test.layer.a", 1);
    Exec budget_b("test.layer.b", 1);
    auto budget_region = budgeted.RegisterRegion(Region("test.budget_region"));
    if (!budget_region || !budgeted.RegisterLayer(Layer(budget_a, 10), &budget_a) ||
        !budgeted.RegisterLayer(Layer(budget_b, 20), &budget_b))
        return 14;
    budgeted.Freeze();
    budgeted.SetBudget({8, 1, 1'000});
    if (budgeted.SimulateInterval(budget_region.Value(), GameplayTimePoint{0}, GameplayTimePoint{10}))
        return 15;
    if (budget_a.commits != 1 || budget_b.commits != 0 ||
        budgeted.FindRegion(budget_region.Value())->last_simulated_at.ticks != 0)
        return 16;
    auto budget_resume = budgeted.SimulateInterval(budget_region.Value(), GameplayTimePoint{0}, GameplayTimePoint{10});
    if (!budget_resume || budget_a.commits != 1 || budget_b.commits != 1 ||
        budgeted.FindRegion(budget_region.Value())->last_simulated_at.ticks != 10)
        return 17;

    // Active interval state survives save/load. Committed legs remain terminal and are not rerun.
    SimulationService before_save;
    Exec save_a("test.layer.a", 1);
    Exec save_b("test.layer.b", 1);
    auto save_region = before_save.RegisterRegion(Region("test.save_region"));
    if (!save_region || !before_save.RegisterLayer(Layer(save_a, 10), &save_a) ||
        !before_save.RegisterLayer(Layer(save_b, 20), &save_b))
        return 18;
    before_save.Freeze();
    before_save.SetBudget({8, 1, 1'000});
    if (before_save.SimulateInterval(save_region.Value(), GameplayTimePoint{0}, GameplayTimePoint{10}))
        return 19;
    const auto pre_restore_cursor = before_save.ReadChangesSince(ChangeCursor{}).latest_cursor;
    if (pre_restore_cursor.sequence < 2)
        return 39;
    auto pending_snapshot = before_save.CaptureSnapshot();

    SimulationService restored;
    Exec restored_a("test.layer.a", 1);
    Exec restored_b("test.layer.b", 1);
    if (!restored.RegisterLayer(Layer(restored_a, 10), &restored_a) ||
        !restored.RegisterLayer(Layer(restored_b, 20), &restored_b))
        return 20;
    restored.Freeze();
    restored.SetBudget({8, 1, 1'000});
    if (!restored.RestoreSnapshot(pending_snapshot))
        return 21;
    if (!restored.ReadChangesSince(pre_restore_cursor).snapshot_required ||
        !restored.ReadChangesSince(restored.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required)
        return 34;
    auto restored_result = restored.SimulateInterval(save_region.Value(), GameplayTimePoint{0}, GameplayTimePoint{10});
    if (!restored_result || restored_a.commits != 0 || restored_b.commits != 1)
        return 22;
    if (!restored.ReadChangesSince(pre_restore_cursor).snapshot_required)
        return 35;
    const auto simulation_epoch = restored.ReadChangesSince(ChangeCursor{});
    if (simulation_epoch.snapshot_required || simulation_epoch.changes.empty())
        return 36;
    const auto simulation_current = restored.ReadChangesSince(simulation_epoch.latest_cursor);
    if (simulation_current.snapshot_required || !simulation_current.changes.empty())
        return 37;

    // Restore is transactional and validates semantic references plus generator position.
    SimulationService target;
    Exec target_a("test.layer.a", 1);
    Exec target_b("test.layer.b", 1);
    if (!target.RegisterLayer(Layer(target_a, 10), &target_a) || !target.RegisterLayer(Layer(target_b, 20), &target_b))
        return 23;
    auto old_region = target.RegisterRegion(Region("test.old_region"));
    if (!old_region)
        return 24;
    auto corrupt = pending_snapshot;
    if (corrupt.active_intervals.empty() || corrupt.active_intervals[0].layers.empty())
        return 25;
    corrupt.active_intervals[0].layers[0].task.layer = SimulationLayerId::FromString("test.unknown_layer");
    if (target.RestoreSnapshot(corrupt))
        return 26;
    if (target.FindRegion(old_region.Value()) == nullptr)
        return 27;

    auto stale_generator = pending_snapshot;
    std::uint64_t max_task_low = 0;
    for (const auto &interval : stale_generator.active_intervals)
        for (const auto &entry : interval.layers)
            if (entry.task.id.value.Low() > max_task_low)
                max_task_low = entry.task.id.value.Low();
    stale_generator.task_ids.next = max_task_low;
    if (target.RestoreSnapshot(stale_generator))
        return 28;
    if (target.FindRegion(old_region.Value()) == nullptr)
        return 29;

    // Bounded summary history and journal gap reporting.
    SimulationService retained;
    Exec retained_exec("test.layer.a", 1);
    auto retained_region = retained.RegisterRegion(Region("test.retained_region"));
    if (!retained_region || !retained.RegisterLayer(Layer(retained_exec, 10), &retained_exec))
        return 30;
    retained.Freeze();
    retained.SetRetentionPolicy({2, 3});
    if (!retained.SimulateInterval(retained_region.Value(), GameplayTimePoint{0}, GameplayTimePoint{1}) ||
        !retained.SimulateInterval(retained_region.Value(), GameplayTimePoint{1}, GameplayTimePoint{2}) ||
        !retained.SimulateInterval(retained_region.Value(), GameplayTimePoint{2}, GameplayTimePoint{3}))
        return 31;
    if (retained.FindSummaries(retained_region.Value()).size() != 2)
        return 32;
    auto gap = retained.ReadChangesSince(ChangeCursor{});
    if (!gap.snapshot_required)
        return 33;
    if (!retained.ReadChangesSince(retained.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required)
        return 38;

    // Goal 4: restore allocation evidence uses narrow module-local fault seams.
    const auto allocation_before = restored.CaptureSnapshot();
    using SimFaultPoint = epidemic::gameplay::simulation::internal_test::AllocationFaultPoint;
    for (const auto point : {SimFaultPoint::RestoreRegions, SimFaultPoint::RestoreSummaries,
                             SimFaultPoint::RestoreIntervals})
    {
        auto target_snapshot = allocation_before;
        epidemic::gameplay::simulation::internal_test::ArmAllocationFault(point);
        const auto result = restored.RestoreSnapshot(std::move(target_snapshot));
        epidemic::gameplay::simulation::internal_test::ResetAllocationFault();
        if (result || !EquivalentMutationState(allocation_before, restored.CaptureSnapshot()))
            return 945;
    }

    // G4-SIM-001: restored max revision must reject before accepted external Commit.
    auto max_revision_snapshot = pending_snapshot;
    max_revision_snapshot.revision.value = std::numeric_limits<std::uint64_t>::max();
    SimulationService max_revision_service;
    Exec max_a("test.layer.a", 1);
    Exec max_b("test.layer.b", 1);
    if (!max_revision_service.RegisterLayer(Layer(max_a, 10), &max_a) ||
        !max_revision_service.RegisterLayer(Layer(max_b, 20), &max_b))
        return 946;
    max_revision_service.Freeze();
    max_revision_service.SetBudget({8, 8, 1'000});
    if (!max_revision_service.RestoreSnapshot(max_revision_snapshot))
        return 947;
    const auto max_before = max_revision_service.CaptureSnapshot();
    const auto max_result = max_revision_service.SimulateInterval(save_region.Value(), GameplayTimePoint{0},
                                                                   GameplayTimePoint{10});
    if (max_result || !max_result.GetError().HasCode("gameplay.simulation.revision_exhausted") ||
        max_b.commits != 0 || !EquivalentMutationState(max_before, max_revision_service.CaptureSnapshot()))
        return 948;

    // G4-SIM-001: the final external Commit also needs one more revision and journal slot for
    // terminal interval summary publication. Exhaustion must therefore be detected before Commit.
    auto one_revision_snapshot = pending_snapshot;
    one_revision_snapshot.revision.value = std::numeric_limits<std::uint64_t>::max() - 1;
    SimulationService one_revision_service;
    Exec one_revision_a("test.layer.a", 1);
    Exec one_revision_b("test.layer.b", 1);
    if (!one_revision_service.RegisterLayer(Layer(one_revision_a, 10), &one_revision_a) ||
        !one_revision_service.RegisterLayer(Layer(one_revision_b, 20), &one_revision_b))
        return 956;
    one_revision_service.Freeze();
    one_revision_service.SetBudget({8, 8, 1'000});
    if (!one_revision_service.RestoreSnapshot(one_revision_snapshot))
        return 957;
    const auto one_revision_before = one_revision_service.CaptureSnapshot();
    const auto one_revision_result = one_revision_service.SimulateInterval(
        save_region.Value(), GameplayTimePoint{0}, GameplayTimePoint{10});
    if (one_revision_result ||
        !one_revision_result.GetError().HasCode("gameplay.simulation.revision_exhausted") ||
        one_revision_b.commits != 0 ||
        !EquivalentMutationState(one_revision_before, one_revision_service.CaptureSnapshot()))
        return 958;

    auto one_sequence_snapshot = pending_snapshot;
    one_sequence_snapshot.next_change_sequence = std::numeric_limits<std::uint64_t>::max();
    SimulationService one_sequence_service;
    Exec one_sequence_a("test.layer.a", 1);
    Exec one_sequence_b("test.layer.b", 1);
    if (!one_sequence_service.RegisterLayer(Layer(one_sequence_a, 10), &one_sequence_a) ||
        !one_sequence_service.RegisterLayer(Layer(one_sequence_b, 20), &one_sequence_b))
        return 959;
    one_sequence_service.Freeze();
    one_sequence_service.SetBudget({8, 8, 1'000});
    if (!one_sequence_service.RestoreSnapshot(one_sequence_snapshot))
        return 960;
    const auto one_sequence_before = one_sequence_service.CaptureSnapshot();
    const auto one_sequence_result = one_sequence_service.SimulateInterval(
        save_region.Value(), GameplayTimePoint{0}, GameplayTimePoint{10});
    if (one_sequence_result ||
        !one_sequence_result.GetError().HasCode("gameplay.simulation.journal_exhausted") ||
        one_sequence_b.commits != 0 ||
        !EquivalentMutationState(one_sequence_before, one_sequence_service.CaptureSnapshot()))
        return 961;

    // Final summary storage is also staged before accepted external work.
    SimulationService summary_preflight_service;
    Exec summary_preflight_a("test.layer.a", 1);
    Exec summary_preflight_b("test.layer.b", 1);
    if (!summary_preflight_service.RegisterLayer(Layer(summary_preflight_a, 10), &summary_preflight_a) ||
        !summary_preflight_service.RegisterLayer(Layer(summary_preflight_b, 20), &summary_preflight_b))
        return 962;
    summary_preflight_service.Freeze();
    summary_preflight_service.SetBudget({8, 8, 1'000});
    if (!summary_preflight_service.RestoreSnapshot(pending_snapshot))
        return 963;
    const auto summary_preflight_before = summary_preflight_service.CaptureSnapshot();
    epidemic::gameplay::simulation::internal_test::ArmAllocationFault(SimFaultPoint::SummaryPublication);
    const auto summary_preflight_result = summary_preflight_service.SimulateInterval(
        save_region.Value(), GameplayTimePoint{0}, GameplayTimePoint{10});
    epidemic::gameplay::simulation::internal_test::ResetAllocationFault();
    if (summary_preflight_result ||
        !summary_preflight_result.GetError().HasCode("gameplay.simulation.allocation_failed") ||
        summary_preflight_b.commits != 0 ||
        !EquivalentMutationState(summary_preflight_before, summary_preflight_service.CaptureSnapshot()))
        return 964;
    const auto summary_preflight_retry = summary_preflight_service.SimulateInterval(
        save_region.Value(), GameplayTimePoint{0}, GameplayTimePoint{10});
    if (!summary_preflight_retry || summary_preflight_b.commits != 1)
        return 965;

    // G4-SIM-002: interval primary publication and journal publication fail before authoritative state changes.
    for (const auto point : {SimFaultPoint::IntervalPublication, SimFaultPoint::JournalPublication})
    {
        SimulationService atomic_interval;
        Exec atomic_exec("test.layer.a", 1);
        auto atomic_region = atomic_interval.RegisterRegion(Region("test.atomic_interval_region"));
        if (!atomic_region || !atomic_interval.RegisterLayer(Layer(atomic_exec, 10), &atomic_exec))
            return 949;
        atomic_interval.Freeze();
        const auto before = atomic_interval.CaptureSnapshot();
        epidemic::gameplay::simulation::internal_test::ArmAllocationFault(point);
        const auto result = atomic_interval.SimulateInterval(atomic_region.Value(), GameplayTimePoint{0},
                                                              GameplayTimePoint{10});
        epidemic::gameplay::simulation::internal_test::ResetAllocationFault();
        if (result || !result.GetError().HasCode("gameplay.simulation.allocation_failed") ||
            !EquivalentMutationState(before, atomic_interval.CaptureSnapshot()) || atomic_exec.commits != 0)
            return 950;
    }

    // Isolate final summary publication with an already-completed restored interval.
    auto completed_pending = pending_snapshot;
    if (completed_pending.active_intervals.size() != 1 || completed_pending.active_intervals.front().layers.size() != 2)
        return 951;
    for (auto &layer_execution : completed_pending.active_intervals.front().layers)
    {
        layer_execution.task.state = SimulationTaskState::Completed;
        layer_execution.prepared = true;
        layer_execution.prepared_summary.state = SimulationTaskState::Completed;
    }
    for (const auto point : {SimFaultPoint::SummaryPublication, SimFaultPoint::JournalPublication})
    {
        SimulationService atomic_summary;
        Exec summary_a("test.layer.a", 1);
        Exec summary_b("test.layer.b", 1);
        if (!atomic_summary.RegisterLayer(Layer(summary_a, 10), &summary_a) ||
            !atomic_summary.RegisterLayer(Layer(summary_b, 20), &summary_b))
            return 952;
        atomic_summary.Freeze();
        if (!atomic_summary.RestoreSnapshot(completed_pending))
            return 953;
        const auto before = atomic_summary.CaptureSnapshot();
        epidemic::gameplay::simulation::internal_test::ArmAllocationFault(point);
        const auto result = atomic_summary.SimulateInterval(save_region.Value(), GameplayTimePoint{0},
                                                             GameplayTimePoint{10});
        epidemic::gameplay::simulation::internal_test::ResetAllocationFault();
        if (result || !result.GetError().HasCode("gameplay.simulation.allocation_failed") ||
            !EquivalentMutationState(before, atomic_summary.CaptureSnapshot()) || summary_a.commits != 0 ||
            summary_b.commits != 0)
            return 954;
        auto retry_summary = atomic_summary.SimulateInterval(save_region.Value(), GameplayTimePoint{0},
                                                              GameplayTimePoint{10});
        if (!retry_summary)
            return 955;
    }


    // Goal 4 empty-state evidence: a fresh service snapshots no semantic runtime state.
    SimulationService empty_api_evidence;
    const auto empty_api_snapshot = empty_api_evidence.CaptureSnapshot();
    if (!empty_api_snapshot.regions.empty() || !empty_api_snapshot.active_intervals.empty() ||
        !empty_api_snapshot.summaries.empty())
        return 1090;

    // Goal 4 exact public-API evidence: virtual executor destruction through the public interface.
    ISimulationLayerExecutor *simulation_destructor_probe = new Exec{"test.destructor"};
    delete simulation_destructor_probe;

    return 0;
}
