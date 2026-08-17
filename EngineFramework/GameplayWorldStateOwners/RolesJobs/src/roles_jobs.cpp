#include "Epidemic/GameFramework/RolesJobs/roles_jobs.h"
#include "Epidemic/Foundation/error.h"
#include <algorithm>
#include <iterator>
#include <utility>

namespace epidemic::gameplay::roles_jobs
{
namespace
{
foundation::Error Error(std::string_view c, std::string_view m)
{
    return foundation::Error::Create(c, m);
}
} // namespace
Duty *RolesJobsService::FindMutableDuty(DutyId id) noexcept
{
    auto it = duties_.find(id);
    return it == duties_.end() ? nullptr : &it->second;
}
JobAssignment *RolesJobsService::FindMutableAssignment(JobAssignmentId id) noexcept
{
    auto it = assignments_.find(id);
    return it == assignments_.end() ? nullptr : &it->second;
}
foundation::Result<void> RolesJobsService::RegisterJobDefinition(JobDefinition d)
{
    if (!d.id.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.roles_jobs.invalid_definition", "invalid job definition"));
    if (definitions_.contains(d.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.roles_jobs.duplicate_definition", "duplicate job definition"));
    Bump();
    d.revision = revision_;
    definitions_.emplace(d.id, std::move(d));
    return foundation::Result<void>::Success();
}
foundation::Result<WorkplaceId> RolesJobsService::CreateWorkplace(Workplace w)
{
    if (!w.area.IsValid())
        return foundation::Result<WorkplaceId>::Failure(
            Error("gameplay.roles_jobs.invalid_workplace", "invalid workplace"));
    if (!w.id.IsValid())
        w.id = WorkplaceId{workplace_ids_.Next()};
    if (workplaces_.contains(w.id))
        return foundation::Result<WorkplaceId>::Failure(
            Error("gameplay.roles_jobs.duplicate_workplace", "duplicate workplace"));
    Bump();
    w.revision = revision_;
    auto id = w.id;
    workplaces_.emplace(id, w);
    Record({0, RolesJobsChangeKind::WorkplaceCreated, {}, {}, id, {}, {}, revision_});
    return foundation::Result<WorkplaceId>::Success(id);
}
foundation::Result<void> RolesJobsService::SetWorkplaceState(WorkplaceId id, WorkplaceState s, GameplayContext c)
{
    auto it = workplaces_.find(id);
    if (it == workplaces_.end())
        return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.workplace_missing", "workplace missing"));
    Bump();
    it->second.state = s;
    it->second.revision = revision_;
    Record({0, RolesJobsChangeKind::WorkplaceStateChanged, {}, {}, id, {}, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<JobAssignmentId> RolesJobsService::AssignJob(JobAssignment a, GameplayContext c)
{
    if (!a.worker.IsValid() || !a.job.IsValid() || !definitions_.contains(a.job) || !a.workplace.IsValid() ||
        !workplaces_.contains(a.workplace))
        return foundation::Result<JobAssignmentId>::Failure(
            Error("gameplay.roles_jobs.invalid_assignment", "invalid job assignment"));
    if (!a.id.IsValid())
        a.id = JobAssignmentId{assignment_ids_.Next()};
    if (assignments_.contains(a.id))
        return foundation::Result<JobAssignmentId>::Failure(
            Error("gameplay.roles_jobs.duplicate_assignment", "duplicate job assignment"));
    Bump();
    a.revision = revision_;
    auto id = a.id;
    auto worker = a.worker;
    auto workplace = a.workplace;
    assignments_.emplace(id, a);
    Record({0, RolesJobsChangeKind::JobAssigned, worker, id, workplace, {}, c, revision_});
    return foundation::Result<JobAssignmentId>::Success(id);
}
foundation::Result<void> RolesJobsService::CancelAssignment(JobAssignmentId id, GameplayContext c)
{
    auto *a = FindMutableAssignment(id);
    if (!a)
        return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.assignment_missing", "assignment missing"));
    Bump();
    a->state = AssignmentState::Cancelled;
    a->revision = revision_;
    for (auto &[did, d] : duties_)
    {
        (void)did;
        if (d.assignment == id && d.state == DutyState::Active)
        {
            d.state = DutyState::Cancelled;
            d.revision = revision_;
        }
    }
    Record({0, RolesJobsChangeKind::JobRemoved, a->worker, id, a->workplace, {}, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<WorkScheduleId> RolesJobsService::CreateSchedule(WorkSchedule s)
{
    if (!s.assignment.IsValid() || !assignments_.contains(s.assignment))
        return foundation::Result<WorkScheduleId>::Failure(
            Error("gameplay.roles_jobs.invalid_schedule", "invalid schedule"));
    if (!s.id.IsValid())
        s.id = WorkScheduleId{schedule_ids_.Next()};
    if (schedules_.contains(s.id))
        return foundation::Result<WorkScheduleId>::Failure(
            Error("gameplay.roles_jobs.duplicate_schedule", "duplicate schedule"));
    for (auto &shift : s.shifts)
    {
        if (!shift.id.IsValid())
            shift.id = WorkShiftId{shift_ids_.Next()};
    }
    Bump();
    s.revision = revision_;
    auto id = s.id;
    schedules_.emplace(id, s);
    return foundation::Result<WorkScheduleId>::Success(id);
}
foundation::Result<DutyId> RolesJobsService::CreateDuty(Duty d, GameplayContext c)
{
    if (!d.subject.IsValid())
        return foundation::Result<DutyId>::Failure(Error("gameplay.roles_jobs.invalid_duty", "invalid duty"));
    if (!d.id.IsValid())
        d.id = DutyId{duty_ids_.Next()};
    if (duties_.contains(d.id))
        return foundation::Result<DutyId>::Failure(Error("gameplay.roles_jobs.duplicate_duty", "duplicate duty"));
    Bump();
    d.revision = revision_;
    auto id = d.id;
    auto subject = d.subject;
    duties_.emplace(id, d);
    Record({0, RolesJobsChangeKind::DutyCreated, subject, d.assignment, {}, id, c, revision_});
    return foundation::Result<DutyId>::Success(id);
}
std::vector<DutyId> RolesJobsService::ActivateDueShifts(GameplayTimePoint now, GameplayContext c)
{
    std::vector<DutyId> created;
    std::vector<WorkSchedule> schedules;
    for (const auto &[id, s] : schedules_)
    {
        (void)id;
        schedules.push_back(s);
    }
    std::sort(schedules.begin(), schedules.end(), [](const auto &a, const auto &b) {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        return a.id < b.id;
    });
    for (const auto &schedule : schedules)
    {
        auto ait = assignments_.find(schedule.assignment);
        if (ait == assignments_.end() || ait->second.state != AssignmentState::Active)
            continue;
        for (const auto &shift : schedule.shifts)
        {
            if (shift.start > now)
                continue;
            const auto end = shift.start + shift.duration;
            if (end < now)
                continue;
            bool exists = false;
            for (const auto &[did, d] : duties_)
            {
                (void)did;
                if (d.assignment == schedule.assignment && d.shift == shift.id && d.state != DutyState::Cancelled)
                {
                    exists = true;
                    break;
                }
            }
            if (exists)
                continue;
            Duty duty;
            duty.id = DutyId{duty_ids_.Next()};
            duty.subject = ait->second.worker;
            duty.type = TypeId{shift.task_type.value};
            duty.priority = schedule.priority;
            duty.state = DutyState::Active;
            duty.assignment = schedule.assignment;
            duty.shift = shift.id;
            Bump();
            duty.revision = revision_;
            auto id = duty.id;
            duties_.emplace(id, duty);
            created.push_back(id);
            ++diagnostics_.shift_events;
            Record({0, RolesJobsChangeKind::ShiftStarted, duty.subject, duty.assignment, ait->second.workplace, id, c,
                    revision_});
            Record({0, RolesJobsChangeKind::DutyActivated, duty.subject, duty.assignment, ait->second.workplace, id, c,
                    revision_});
        }
    }
    return created;
}
foundation::Result<void> RolesJobsService::CompleteDuty(DutyId id, GameplayContext c)
{
    auto *d = FindMutableDuty(id);
    if (!d)
        return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.duty_missing", "duty missing"));
    Bump();
    d->state = DutyState::Completed;
    d->revision = revision_;
    Record({0, RolesJobsChangeKind::DutyCompleted, d->subject, d->assignment, {}, id, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> RolesJobsService::FailDuty(DutyId id, GameplayContext c)
{
    auto *d = FindMutableDuty(id);
    if (!d)
        return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.duty_missing", "duty missing"));
    Bump();
    d->state = DutyState::Failed;
    d->revision = revision_;
    ++diagnostics_.duty_failures;
    Record({0, RolesJobsChangeKind::DutyFailed, d->subject, d->assignment, {}, id, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> RolesJobsService::CancelDuty(DutyId id, GameplayContext c)
{
    auto *d = FindMutableDuty(id);
    if (!d)
        return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.duty_missing", "duty missing"));
    Bump();
    d->state = DutyState::Cancelled;
    d->revision = revision_;
    Record({0, RolesJobsChangeKind::DutyCancelled, d->subject, d->assignment, {}, id, c, revision_});
    return foundation::Result<void>::Success();
}
const JobAssignment *RolesJobsService::GetJobAssignment(JobAssignmentId id) const noexcept
{
    auto it = assignments_.find(id);
    return it == assignments_.end() ? nullptr : &it->second;
}
const Workplace *RolesJobsService::GetWorkplace(WorkplaceId id) const noexcept
{
    auto it = workplaces_.find(id);
    return it == workplaces_.end() ? nullptr : &it->second;
}
const Duty *RolesJobsService::GetCurrentDuty(GameplayObjectRef s) const noexcept
{
    const Duty *best = nullptr;
    for (const auto &[id, d] : duties_)
    {
        (void)id;
        if (d.subject == s && d.state == DutyState::Active)
        {
            if (!best || d.priority > best->priority || (d.priority == best->priority && d.id < best->id))
                best = &d;
        }
    }
    return best;
}
std::vector<JobAssignment> RolesJobsService::FindJobsOfSubject(GameplayObjectRef s) const
{
    std::vector<JobAssignment> out;
    for (const auto &[id, a] : assignments_)
    {
        (void)id;
        if (a.worker == s)
            out.push_back(a);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}
std::vector<JobAssignment> RolesJobsService::FindWorkersAtWorkplace(WorkplaceId w) const
{
    std::vector<JobAssignment> out;
    for (const auto &[id, a] : assignments_)
    {
        (void)id;
        if (a.workplace == w)
            out.push_back(a);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        if (a.worker != b.worker)
            return a.worker < b.worker;
        return a.id < b.id;
    });
    return out;
}
std::vector<Workplace> RolesJobsService::FindWorkplacesInArea(GameplayObjectRef a) const
{
    std::vector<Workplace> out;
    for (const auto &[id, w] : workplaces_)
    {
        (void)id;
        if (w.area == a)
            out.push_back(w);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}
std::vector<Duty> RolesJobsService::FindActiveDuties() const
{
    std::vector<Duty> out;
    for (const auto &[id, d] : duties_)
    {
        (void)id;
        if (d.state == DutyState::Active)
            out.push_back(d);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        if (a.subject != b.subject)
            return a.subject < b.subject;
        return a.id < b.id;
    });
    return out;
}
std::vector<RolesJobsChange> RolesJobsService::ChangesSince(std::uint64_t s) const
{
    std::vector<RolesJobsChange> out;
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(out),
                 [s](const auto &c) { return c.sequence > s; });
    return out;
}
RolesJobsSnapshot RolesJobsService::CaptureSnapshot() const
{
    RolesJobsSnapshot s;
    for (const auto &[id, d] : definitions_)
    {
        (void)id;
        s.definitions.push_back(d);
    }
    for (const auto &[id, w] : workplaces_)
    {
        (void)id;
        s.workplaces.push_back(w);
    }
    for (const auto &[id, a] : assignments_)
    {
        (void)id;
        s.assignments.push_back(a);
    }
    for (const auto &[id, ws] : schedules_)
    {
        (void)id;
        s.schedules.push_back(ws);
    }
    for (const auto &[id, d] : duties_)
    {
        (void)id;
        s.duties.push_back(d);
    }
    std::sort(s.definitions.begin(), s.definitions.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.workplaces.begin(), s.workplaces.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.assignments.begin(), s.assignments.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.schedules.begin(), s.schedules.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.duties.begin(), s.duties.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    s.assignment_ids = assignment_ids_.GetSnapshot();
    s.workplace_ids = workplace_ids_.GetSnapshot();
    s.schedule_ids = schedule_ids_.GetSnapshot();
    s.shift_ids = shift_ids_.GetSnapshot();
    s.duty_ids = duty_ids_.GetSnapshot();
    s.revision = revision_;
    return s;
}
foundation::Result<void> RolesJobsService::RestoreSnapshot(RolesJobsSnapshot s)
{
    definitions_.clear();
    workplaces_.clear();
    assignments_.clear();
    schedules_.clear();
    duties_.clear();
    for (auto &d : s.definitions)
    {
        if (!d.id.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.roles_jobs.restore_invalid", "invalid definition"));
        definitions_[d.id] = d;
    }
    for (auto &w : s.workplaces)
    {
        if (!w.id.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.restore_invalid", "invalid workplace"));
        workplaces_[w.id] = w;
    }
    for (auto &a : s.assignments)
    {
        if (!a.id.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.roles_jobs.restore_invalid", "invalid assignment"));
        assignments_[a.id] = a;
    }
    for (auto &ws : s.schedules)
    {
        if (!ws.id.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.restore_invalid", "invalid schedule"));
        schedules_[ws.id] = ws;
    }
    for (auto &d : s.duties)
    {
        if (!d.id.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.restore_invalid", "invalid duty"));
        duties_[d.id] = d;
    }
    assignment_ids_.Restore(s.assignment_ids);
    workplace_ids_.Restore(s.workplace_ids);
    schedule_ids_.Restore(s.schedule_ids);
    shift_ids_.Restore(s.shift_ids);
    duty_ids_.Restore(s.duty_ids);
    revision_ = s.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    return foundation::Result<void>::Success();
}
RolesJobsDiagnostics RolesJobsService::GetDiagnostics() const noexcept
{
    auto d = diagnostics_;
    d.definitions = definitions_.size();
    d.workplaces = workplaces_.size();
    d.assignments = assignments_.size();
    d.schedules = schedules_.size();
    for (const auto &[id, duty] : duties_)
    {
        (void)id;
        if (duty.state == DutyState::Active)
            ++d.active_duties;
    }
    return d;
}
void RolesJobsService::Record(RolesJobsChange c)
{
    c.sequence = next_change_sequence_++;
    changes_.push_back(c);
}
} // namespace epidemic::gameplay::roles_jobs
