#include "Epidemic/GameFramework/RolesJobs/roles_jobs.h"
#include <cstdlib>
#include <iostream>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::roles_jobs;

namespace
{
void Check(bool value, const char *message)
{
    if (!value)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
GameplayObjectRef Ref(const char *domain, const char *id)
{
    return {GameplayDomainId::FromString(domain), GameplayObjectId::FromString(id)};
}

JobDefinition MakeJob()
{
    JobDefinition job;
    job.id = JobDefinitionId::FromString("job.shopkeeper");
    job.function = RoleFunctionId::FromString("function.trade");
    job.default_tasks.push_back(JobTaskTypeId::FromString("task.open_shop"));
    return job;
}

WorkSchedule MakeSchedule(JobAssignmentId assignment, GameplayTimePoint start, GameplayDuration duration)
{
    WorkSchedule schedule;
    schedule.assignment = assignment;
    schedule.priority = 10;
    WorkShift shift;
    shift.start = start;
    shift.duration = duration;
    shift.task_type = JobTaskTypeId::FromString("task.open_shop");
    shift.target_area = Ref("world.area", "market");
    schedule.shifts.push_back(shift);
    return schedule;
}
} // namespace

int main()
{
    RolesJobsService service;
    auto job = MakeJob();
    Check(static_cast<bool>(service.RegisterJobDefinition(job)), "register job");

    Workplace workplace;
    workplace.area = Ref("world.area", "market");
    workplace.property = Ref("property", "shop");
    auto workplace_id = service.CreateWorkplace(workplace);
    Check(static_cast<bool>(workplace_id), "create workplace");

    JobAssignment assignment;
    assignment.worker = Ref("entities", "npc.shopkeeper");
    assignment.job = job.id;
    assignment.workplace = workplace_id.Value();
    auto assignment_id = service.AssignJob(assignment, {.time = GameplayTimePoint{0}});
    Check(static_cast<bool>(assignment_id), "assign job");

    Check(static_cast<bool>(service.CreateSchedule(MakeSchedule(assignment_id.Value(), GameplayTimePoint{800},
                                                               GameplayDuration{400}))),
          "create schedule");
    Check(service.ActivateDueShifts(GameplayTimePoint{700}).empty(), "not due");
    auto duties = service.ActivateDueShifts(GameplayTimePoint{900});
    Check(duties.size() == 1, "shift due");
    const auto *current = service.GetCurrentDuty(assignment.worker);
    Check(current != nullptr, "current duty");
    Check(current->state == DutyState::Active, "duty active");
    Check(current->tasks.size() == 1 && current->tasks.front() == JobTaskTypeId::FromString("task.open_shop"),
          "default or shift task copied to duty");
    Check(static_cast<bool>(service.CompleteDuty(duties.front())), "complete duty");
    Check(service.FindActiveDuties().empty(), "duty completed");
    Check(!static_cast<bool>(service.CompleteDuty(duties.front())), "terminal duty cannot complete twice");

    Check(static_cast<bool>(service.SetWorkplaceState(workplace_id.Value(), WorkplaceState::Closed)), "close workplace");
    JobAssignment rejected;
    rejected.worker = Ref("entities", "npc.rejected");
    rejected.job = job.id;
    rejected.workplace = workplace_id.Value();
    Check(!static_cast<bool>(service.AssignJob(rejected)), "closed workplace rejects assignment");
    Check(service.GetJobAssignment(assignment_id.Value())->state == AssignmentState::Suspended,
          "closing workplace suspends assignments");
    Check(static_cast<bool>(service.SetWorkplaceState(workplace_id.Value(), WorkplaceState::Active)), "reopen workplace");
    Check(static_cast<bool>(service.ResumeAssignment(assignment_id.Value())), "resume assignment after reopen");

    RolesJobsService recurrence;
    Check(static_cast<bool>(recurrence.RegisterJobDefinition(job)), "register recurrence job");
    workplace.id = {};
    auto recurrence_workplace = recurrence.CreateWorkplace(workplace);
    Check(static_cast<bool>(recurrence_workplace), "create recurrence workplace");
    assignment.id = {};
    assignment.workplace = recurrence_workplace.Value();
    auto recurrence_assignment = recurrence.AssignJob(assignment);
    Check(static_cast<bool>(recurrence_assignment), "assign recurrence job");
    WorkSchedule recurring_schedule;
    recurring_schedule.assignment = recurrence_assignment.Value();
    WorkShift recurring_shift;
    recurring_shift.start = GameplayTimePoint{100};
    recurring_shift.duration = GameplayDuration{20};
    recurring_shift.recurrence = GameplayDuration{100};
    recurring_shift.task_type = JobTaskTypeId::FromString("task.open_shop");
    recurring_schedule.shifts.push_back(recurring_shift);
    Check(static_cast<bool>(recurrence.CreateSchedule(recurring_schedule)), "create recurring schedule");
    auto first_recurring = recurrence.ActivateDueShifts(GameplayTimePoint{95}, GameplayTimePoint{110});
    Check(first_recurring.size() == 1, "first recurring occurrence active");
    Check(recurrence.FindDuty(first_recurring.front())->state == DutyState::Active, "first occurrence active");
    Check(static_cast<bool>(recurrence.CompleteDuty(first_recurring.front())), "complete first occurrence");
    auto second_recurring = recurrence.ActivateDueShifts(GameplayTimePoint{195}, GameplayTimePoint{210});
    Check(second_recurring.size() == 1, "completed duty does not block next recurring occurrence");
    Check(recurrence.FindDuty(second_recurring.front())->scheduled_start == GameplayTimePoint{200},
          "second occurrence key uses occurrence start");
    auto missed = recurrence.ActivateDueShifts(GameplayTimePoint{210}, GameplayTimePoint{330});
    Check(missed.size() == 1, "missed occurrence is recorded");
    Check(recurrence.FindDuty(missed.front())->state == DutyState::Skipped, "missed occurrence skipped");

    RolesJobsService journal;
    Check(static_cast<bool>(journal.RegisterJobDefinition(job)), "journal register job");
    workplace.id = {};
    auto journal_workplace = journal.CreateWorkplace(workplace);
    Check(static_cast<bool>(journal_workplace), "journal create workplace");
    for (int i = 0; i < 4100; ++i)
    {
        Check(static_cast<bool>(journal.SetWorkplaceState(journal_workplace.Value(),
                                                         (i % 2) == 0 ? WorkplaceState::Suspended
                                                                      : WorkplaceState::Active)),
              "journal state change");
    }
    auto stale_batch = journal.ReadChangesSince(0);
    Check(stale_batch.snapshot_required, "bounded journal requires snapshot for stale reader");
    auto latest_batch = journal.ReadChangesSince(journal.LatestChangeSequence());
    Check(!latest_batch.snapshot_required && latest_batch.changes.empty(), "latest journal cursor valid");

    auto snapshot = service.CaptureSnapshot();
    RolesJobsService restored;
    Check(static_cast<bool>(restored.RestoreSnapshot(snapshot)), "restore");
    Check(restored.GetWorkplace(workplace_id.Value())->state == WorkplaceState::Active, "restore workplace state");
    Check(restored.FindJobsOfSubject(assignment.worker).size() == 1, "restore assignment");

    auto corrupt = snapshot;
    corrupt.assignment_ids.next = 1;
    RolesJobsService stable;
    Check(static_cast<bool>(stable.RegisterJobDefinition(job)), "stable register job");
    Workplace stable_workplace;
    stable_workplace.area = Ref("world.area", "stable");
    auto stable_workplace_id = stable.CreateWorkplace(stable_workplace);
    Check(static_cast<bool>(stable_workplace_id), "stable create workplace");
    Check(!static_cast<bool>(stable.RestoreSnapshot(corrupt)), "corrupt generator rejected");
    Check(stable.GetWorkplace(stable_workplace_id.Value()) != nullptr, "failed restore leaves old state intact");

    return 0;
}
