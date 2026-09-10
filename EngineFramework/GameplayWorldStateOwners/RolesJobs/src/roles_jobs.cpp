#include "Epidemic/GameFramework/RolesJobs/roles_jobs.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_set>
#include <utility>

namespace epidemic::gameplay::roles_jobs
{
namespace
{
foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}

template <class TId> void AdvanceGeneratorPastAcceptedId(MonotonicIdGenerator<GameplayObjectId> &generator, TId id) noexcept
{
    if (!id.IsValid())
        return;

    auto snapshot = generator.GetSnapshot();
    if (id.value.High() != snapshot.scope || snapshot.next == 0 || id.value.Low() < snapshot.next)
        return;

    snapshot.next = id.value.Low() == std::numeric_limits<std::uint64_t>::max() ? 0 : id.value.Low() + 1;
    generator.Restore(snapshot);
}

template <class TId> [[nodiscard]] std::uint64_t MaxLowPart(const std::vector<TId> &ids) noexcept
{
    std::uint64_t result = 0;
    for (const auto id : ids)
    {
        if (id.IsValid())
            result = std::max(result, id.value.Low());
    }
    return result;
}


[[nodiscard]] bool IsValidWorkplaceState(WorkplaceState state) noexcept
{
    switch (state)
    {
    case WorkplaceState::Active:
    case WorkplaceState::Closed:
    case WorkplaceState::Destroyed:
    case WorkplaceState::Unavailable:
    case WorkplaceState::Seasonal:
    case WorkplaceState::Suspended:
        return true;
    }
    return false;
}

[[nodiscard]] bool IsValidAssignmentState(AssignmentState state) noexcept
{
    switch (state)
    {
    case AssignmentState::Active:
    case AssignmentState::Paused:
    case AssignmentState::Suspended:
    case AssignmentState::Completed:
    case AssignmentState::Cancelled:
    case AssignmentState::Invalid:
        return true;
    }
    return false;
}

[[nodiscard]] bool IsValidDutyState(DutyState state) noexcept
{
    switch (state)
    {
    case DutyState::Scheduled:
    case DutyState::Available:
    case DutyState::Active:
    case DutyState::Completed:
    case DutyState::Failed:
    case DutyState::Skipped:
    case DutyState::Cancelled:
        return true;
    }
    return false;
}

[[nodiscard]] bool IsValidRolesJobsChangeKind(RolesJobsChangeKind kind) noexcept
{
    switch (kind)
    {
    case RolesJobsChangeKind::JobAssigned:
    case RolesJobsChangeKind::JobRemoved:
    case RolesJobsChangeKind::JobPaused:
    case RolesJobsChangeKind::JobResumed:
    case RolesJobsChangeKind::WorkplaceCreated:
    case RolesJobsChangeKind::WorkplaceStateChanged:
    case RolesJobsChangeKind::ShiftScheduled:
    case RolesJobsChangeKind::ShiftStarted:
    case RolesJobsChangeKind::ShiftEnded:
    case RolesJobsChangeKind::DutyCreated:
    case RolesJobsChangeKind::DutyActivated:
    case RolesJobsChangeKind::DutyCompleted:
    case RolesJobsChangeKind::DutyFailed:
    case RolesJobsChangeKind::DutySkipped:
    case RolesJobsChangeKind::DutyCancelled:
        return true;
    }
    return false;
}

[[nodiscard]] bool IsAllowedAssignmentTransition(AssignmentState from, AssignmentState to) noexcept
{
    if (!IsValidAssignmentState(from) || !IsValidAssignmentState(to))
        return false;
    if (from == to)
        return true;
    switch (from)
    {
    case AssignmentState::Active:
        return to == AssignmentState::Paused || to == AssignmentState::Suspended ||
               to == AssignmentState::Completed || to == AssignmentState::Cancelled || to == AssignmentState::Invalid;
    case AssignmentState::Paused:
        return to == AssignmentState::Active || to == AssignmentState::Suspended || to == AssignmentState::Cancelled;
    case AssignmentState::Suspended:
        return to == AssignmentState::Active || to == AssignmentState::Cancelled;
    case AssignmentState::Completed:
    case AssignmentState::Cancelled:
    case AssignmentState::Invalid:
        return false;
    }
    return false;
}

[[nodiscard]] bool IsAllowedDutyTransition(DutyState from, DutyState to) noexcept
{
    if (!IsValidDutyState(from) || !IsValidDutyState(to))
        return false;
    if (from == to)
        return true;
    switch (from)
    {
    case DutyState::Scheduled:
        return to == DutyState::Available || to == DutyState::Active || to == DutyState::Skipped || to == DutyState::Cancelled;
    case DutyState::Available:
        return to == DutyState::Active || to == DutyState::Skipped || to == DutyState::Cancelled;
    case DutyState::Active:
        return to == DutyState::Completed || to == DutyState::Failed || to == DutyState::Cancelled;
    case DutyState::Completed:
    case DutyState::Failed:
    case DutyState::Skipped:
    case DutyState::Cancelled:
        return false;
    }
    return false;
}

[[nodiscard]] bool IsPositiveDuration(GameplayDuration duration) noexcept
{
    return duration.ticks > 0;
}

[[nodiscard]] bool IsNonNegativeDuration(GameplayDuration duration) noexcept
{
    return duration.ticks >= 0;
}

[[nodiscard]] std::size_t HashCombine(std::size_t seed, std::size_t value) noexcept
{
    return seed ^ (value + 0x9E3779B97F4A7C15ull + (seed << 6u) + (seed >> 2u));
}

[[nodiscard]] GameplayTimePoint OneTickBefore(GameplayTimePoint value) noexcept
{
    if (value.ticks == std::numeric_limits<std::int64_t>::min())
        return value;
    return GameplayTimePoint{value.ticks - 1};
}

[[nodiscard]] bool IntersectsOpenClosed(GameplayTimePoint from, GameplayTimePoint to, GameplayTimePoint start,
                                         GameplayTimePoint end) noexcept
{
    // Shift occurrence interval is [start,end). Simulation activation interval is (from,to].
    return end.ticks > from.ticks && start.ticks <= to.ticks;
}

[[nodiscard]] bool IsMissedAt(GameplayTimePoint to, GameplayTimePoint end) noexcept
{
    return end.ticks <= to.ticks;
}
} // namespace

std::size_t RolesJobsService::DutyOccurrenceKeyHash::operator()(const DutyOccurrenceKey &key) const noexcept
{
    std::size_t hash = IdHash{}(key.assignment);
    hash = HashCombine(hash, IdHash{}(key.shift));
    hash = HashCombine(hash, std::hash<std::int64_t>{}(key.start.ticks));
    return hash;
}

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

foundation::Result<Revision> RolesJobsService::NextRevision() const
{
    const auto next = CheckedNext(revision_);
    if (!next)
        return foundation::Result<Revision>::Failure(
            Error("gameplay.roles_jobs.revision_exhausted", "roles/jobs revision is exhausted"));
    return foundation::Result<Revision>::Success(*next);
}

bool RolesJobsService::IsWorkplaceStateOperational(WorkplaceState state) const noexcept
{
    return state == WorkplaceState::Active || state == WorkplaceState::Seasonal;
}

bool RolesJobsService::IsWorkplaceOperational(WorkplaceId id) const noexcept
{
    const auto it = workplaces_.find(id);
    return it != workplaces_.end() && IsWorkplaceStateOperational(it->second.state);
}

bool RolesJobsService::IsTerminalAssignment(AssignmentState state) const noexcept
{
    return state == AssignmentState::Completed || state == AssignmentState::Cancelled || state == AssignmentState::Invalid;
}

bool RolesJobsService::IsTerminalDuty(DutyState state) const noexcept
{
    return state == DutyState::Completed || state == DutyState::Failed || state == DutyState::Skipped ||
           state == DutyState::Cancelled;
}

bool RolesJobsService::IsLiveDuty(DutyState state) const noexcept
{
    return state == DutyState::Scheduled || state == DutyState::Available || state == DutyState::Active;
}

bool RolesJobsService::HasDutyOccurrence(JobAssignmentId assignment, WorkShiftId shift, GameplayTimePoint start) const
{
    for (const auto &[id, duty] : duties_)
    {
        (void)id;
        if (duty.assignment == assignment && duty.shift == shift && duty.scheduled_start == start)
            return true;
    }
    return false;
}

void RolesJobsService::RebuildIndexes()
{
    std::unordered_map<GameplayObjectRef, std::vector<JobAssignmentId>> assignments_by_worker;
    std::unordered_map<WorkplaceId, std::vector<JobAssignmentId>, IdHash> assignments_by_workplace;
    std::unordered_map<JobAssignmentId, std::vector<WorkScheduleId>, IdHash> schedules_by_assignment;
    std::unordered_map<JobAssignmentId, std::vector<DutyId>, IdHash> duties_by_assignment;
    std::unordered_map<GameplayObjectRef, std::vector<DutyId>> duties_by_subject;

    assignments_by_worker.reserve(assignments_.size());
    assignments_by_workplace.reserve(assignments_.size());
    schedules_by_assignment.reserve(schedules_.size());
    duties_by_assignment.reserve(duties_.size());
    duties_by_subject.reserve(duties_.size());

    for (const auto &[id, assignment] : assignments_)
    {
        assignments_by_worker[assignment.worker].push_back(id);
        assignments_by_workplace[assignment.workplace].push_back(id);
    }
    for (const auto &[id, schedule] : schedules_)
    {
        schedules_by_assignment[schedule.assignment].push_back(id);
    }
    for (const auto &[id, duty] : duties_)
    {
        duties_by_assignment[duty.assignment].push_back(id);
        duties_by_subject[duty.subject].push_back(id);
    }

    auto sort_ids = [](auto &map) {
        for (auto &[key, ids] : map)
        {
            (void)key;
            std::sort(ids.begin(), ids.end());
        }
    };
    sort_ids(assignments_by_worker);
    sort_ids(assignments_by_workplace);
    sort_ids(schedules_by_assignment);
    sort_ids(duties_by_assignment);
    sort_ids(duties_by_subject);

    assignments_by_worker_.swap(assignments_by_worker);
    assignments_by_workplace_.swap(assignments_by_workplace);
    schedules_by_assignment_.swap(schedules_by_assignment);
    duties_by_assignment_.swap(duties_by_assignment);
    duties_by_subject_.swap(duties_by_subject);
}

void RolesJobsService::PruneTerminalDuties()
{
    if (terminal_duty_retention_capacity_ == 0)
        return;

    std::vector<DutyId> terminal;
    terminal.reserve(duties_.size());
    for (const auto &[id, duty] : duties_)
    {
        if (IsValidDutyState(duty.state) && IsTerminalDuty(duty.state))
            terminal.push_back(id);
    }
    if (terminal.size() <= terminal_duty_retention_capacity_)
        return;

    std::sort(terminal.begin(), terminal.end(), [this](DutyId lhs, DutyId rhs) {
        const auto lit = duties_.find(lhs);
        const auto rit = duties_.find(rhs);
        if (lit != duties_.end() && rit != duties_.end() && lit->second.revision != rit->second.revision)
            return lit->second.revision < rit->second.revision;
        return lhs < rhs;
    });
    const auto erase_count = terminal.size() - terminal_duty_retention_capacity_;
    for (std::size_t i = 0; i < erase_count; ++i)
        duties_.erase(terminal[i]);
    RebuildIndexes();
}

foundation::Result<void> RolesJobsService::RegisterJobDefinition(JobDefinition definition)
{
    if (!definition.id.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.roles_jobs.invalid_definition", "invalid job definition"));
    if (definitions_.contains(definition.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.roles_jobs.duplicate_definition", "duplicate job definition"));
    for (const auto task : definition.default_tasks)
    {
        if (!task.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.roles_jobs.invalid_definition", "invalid default job task"));
    }
    auto next_revision = NextRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());
    Bump(next_revision.Value());
    definition.revision = revision_;
    definitions_.emplace(definition.id, std::move(definition));
    return foundation::Result<void>::Success();
}

foundation::Result<WorkplaceId> RolesJobsService::CreateWorkplace(Workplace workplace)
{
    if (!workplace.area.IsValid() || !IsValidWorkplaceState(workplace.state))
        return foundation::Result<WorkplaceId>::Failure(
            Error("gameplay.roles_jobs.invalid_workplace", "invalid workplace"));

    auto staged_ids = workplace_ids_;
    if (!workplace.id.IsValid())
        workplace.id = WorkplaceId{staged_ids.Next()};
    else
        AdvanceGeneratorPastAcceptedId(staged_ids, workplace.id);
    if (!workplace.id.IsValid())
        return foundation::Result<WorkplaceId>::Failure(
            Error("gameplay.roles_jobs.id_exhausted", "workplace id is exhausted"));
    if (workplaces_.contains(workplace.id))
        return foundation::Result<WorkplaceId>::Failure(
            Error("gameplay.roles_jobs.duplicate_workplace", "duplicate workplace"));

    auto next_revision = NextRevision();
    if (!next_revision)
        return foundation::Result<WorkplaceId>::Failure(next_revision.GetError());

    workplace_ids_ = staged_ids;
    Bump(next_revision.Value());
    workplace.revision = revision_;
    const auto id = workplace.id;
    workplaces_.emplace(id, std::move(workplace));
    Record({0, RolesJobsChangeKind::WorkplaceCreated, {}, {}, id, {}, {}, revision_});
    return foundation::Result<WorkplaceId>::Success(id);
}

foundation::Result<void> RolesJobsService::SetWorkplaceState(WorkplaceId id, WorkplaceState state, GameplayContext context)
{
    auto it = workplaces_.find(id);
    if (it == workplaces_.end())
        return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.workplace_missing", "workplace missing"));

    if (it->second.state == state)
        return foundation::Result<void>::Success();

    auto next_revision = NextRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());

    Bump(next_revision.Value());
    it->second.state = state;
    it->second.revision = revision_;
    Record({0, RolesJobsChangeKind::WorkplaceStateChanged, {}, {}, id, {}, context, revision_});

    if (!IsWorkplaceStateOperational(state))
    {
        std::vector<JobAssignmentId> affected_assignments;
        if (const auto idx = assignments_by_workplace_.find(id); idx != assignments_by_workplace_.end())
            affected_assignments = idx->second;
        for (const auto assignment_id : affected_assignments)
        {
            auto *assignment = FindMutableAssignment(assignment_id);
            if (assignment != nullptr && assignment->state == AssignmentState::Active)
            {
                assignment->state = AssignmentState::Suspended;
                assignment->revision = revision_;
                Record({0, RolesJobsChangeKind::JobPaused, assignment->worker, assignment_id, id, {}, context, revision_});
            }
        }

        std::vector<DutyId> duties_to_cancel;
        for (const auto &[duty_id, duty] : duties_)
        {
            const auto assignment = assignments_.find(duty.assignment);
            if (assignment != assignments_.end() && assignment->second.workplace == id && IsLiveDuty(duty.state))
                duties_to_cancel.push_back(duty_id);
        }
        std::sort(duties_to_cancel.begin(), duties_to_cancel.end());
        for (const auto duty_id : duties_to_cancel)
        {
            auto *duty = FindMutableDuty(duty_id);
            if (duty == nullptr)
                continue;
            duty->state = DutyState::Cancelled;
            duty->revision = revision_;
            Record({0, RolesJobsChangeKind::DutyCancelled, duty->subject, duty->assignment, id, duty_id, context,
                    revision_});
            Record({0, RolesJobsChangeKind::ShiftEnded, duty->subject, duty->assignment, id, duty_id, context,
                    revision_});
        }
    }

    RebuildIndexes();
    PruneTerminalDuties();
    return foundation::Result<void>::Success();
}

foundation::Result<JobAssignmentId> RolesJobsService::AssignJob(JobAssignment assignment, GameplayContext context)
{
    if (!assignment.worker.IsValid() || !assignment.job.IsValid() || !definitions_.contains(assignment.job) ||
        !assignment.workplace.IsValid() || !workplaces_.contains(assignment.workplace))
        return foundation::Result<JobAssignmentId>::Failure(
            Error("gameplay.roles_jobs.invalid_assignment", "invalid job assignment"));
    if (!IsWorkplaceOperational(assignment.workplace))
        return foundation::Result<JobAssignmentId>::Failure(
            Error("gameplay.roles_jobs.workplace_not_operational", "job assignment requires operational workplace"));
    if (!IsValidAssignmentState(assignment.state) || assignment.state != AssignmentState::Active)
        return foundation::Result<JobAssignmentId>::Failure(
            Error("gameplay.roles_jobs.invalid_assignment_state", "new assignment must start active"));

    auto staged_ids = assignment_ids_;
    if (!assignment.id.IsValid())
        assignment.id = JobAssignmentId{staged_ids.Next()};
    else
        AdvanceGeneratorPastAcceptedId(staged_ids, assignment.id);
    if (!assignment.id.IsValid())
        return foundation::Result<JobAssignmentId>::Failure(
            Error("gameplay.roles_jobs.id_exhausted", "assignment id is exhausted"));
    if (assignments_.contains(assignment.id))
        return foundation::Result<JobAssignmentId>::Failure(
            Error("gameplay.roles_jobs.duplicate_assignment", "duplicate job assignment"));

    auto next_revision = NextRevision();
    if (!next_revision)
        return foundation::Result<JobAssignmentId>::Failure(next_revision.GetError());

    assignment_ids_ = staged_ids;
    Bump(next_revision.Value());
    assignment.revision = revision_;
    const auto id = assignment.id;
    const auto worker = assignment.worker;
    const auto workplace = assignment.workplace;
    assignments_.emplace(id, std::move(assignment));
    RebuildIndexes();
    Record({0, RolesJobsChangeKind::JobAssigned, worker, id, workplace, {}, context, revision_});
    return foundation::Result<JobAssignmentId>::Success(id);
}

foundation::Result<void> RolesJobsService::TransitionAssignment(JobAssignmentId id, AssignmentState state,
                                                                RolesJobsChangeKind change, GameplayContext context)
{
    auto *assignment = FindMutableAssignment(id);
    if (assignment == nullptr)
        return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.assignment_missing", "assignment missing"));
    if (!IsValidAssignmentState(assignment->state))
        return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.invalid_assignment_state", "invalid assignment state"));
    if (!IsAllowedAssignmentTransition(assignment->state, AssignmentState::Cancelled))
        return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.invalid_assignment_transition", "invalid assignment transition"));
    if (IsTerminalAssignment(assignment->state))
        return foundation::Result<void>::Failure(
            Error("gameplay.roles_jobs.assignment_terminal", "terminal assignment cannot transition"));
    if (state == AssignmentState::Active && !IsWorkplaceOperational(assignment->workplace))
        return foundation::Result<void>::Failure(
            Error("gameplay.roles_jobs.workplace_not_operational", "assignment requires operational workplace"));
    if (assignment->state == state)
        return foundation::Result<void>::Success();

    auto next_revision = NextRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());

    Bump(next_revision.Value());
    assignment->state = state;
    assignment->revision = revision_;
    Record({0, change, assignment->worker, id, assignment->workplace, {}, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> RolesJobsService::PauseAssignment(JobAssignmentId id, GameplayContext context)
{
    return TransitionAssignment(id, AssignmentState::Paused, RolesJobsChangeKind::JobPaused, context);
}

foundation::Result<void> RolesJobsService::ResumeAssignment(JobAssignmentId id, GameplayContext context)
{
    return TransitionAssignment(id, AssignmentState::Active, RolesJobsChangeKind::JobResumed, context);
}

foundation::Result<void> RolesJobsService::SuspendAssignment(JobAssignmentId id, GameplayContext context)
{
    return TransitionAssignment(id, AssignmentState::Suspended, RolesJobsChangeKind::JobPaused, context);
}

foundation::Result<void> RolesJobsService::CompleteAssignment(JobAssignmentId id, GameplayContext context)
{
    return TransitionAssignment(id, AssignmentState::Completed, RolesJobsChangeKind::JobRemoved, context);
}

foundation::Result<void> RolesJobsService::CancelAssignment(JobAssignmentId id, GameplayContext context)
{
    auto *assignment = FindMutableAssignment(id);
    if (assignment == nullptr)
        return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.assignment_missing", "assignment missing"));
    if (assignment->state == AssignmentState::Cancelled)
        return foundation::Result<void>::Success();
    if (!IsValidAssignmentState(assignment->state))
        return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.invalid_assignment_state", "invalid assignment state"));
    if (!IsAllowedAssignmentTransition(assignment->state, AssignmentState::Cancelled))
        return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.invalid_assignment_transition", "invalid assignment transition"));
    if (IsTerminalAssignment(assignment->state))
        return foundation::Result<void>::Failure(
            Error("gameplay.roles_jobs.assignment_terminal", "terminal assignment cannot be cancelled"));

    auto next_revision = NextRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());

    Bump(next_revision.Value());
    assignment->state = AssignmentState::Cancelled;
    assignment->revision = revision_;

    std::vector<DutyId> affected_duties;
    if (const auto idx = duties_by_assignment_.find(id); idx != duties_by_assignment_.end())
        affected_duties = idx->second;
    std::sort(affected_duties.begin(), affected_duties.end());
    for (const auto duty_id : affected_duties)
    {
        auto *duty = FindMutableDuty(duty_id);
        if (duty != nullptr && IsLiveDuty(duty->state))
        {
            duty->state = DutyState::Cancelled;
            duty->revision = revision_;
            Record({0, RolesJobsChangeKind::DutyCancelled, duty->subject, id, assignment->workplace, duty_id, context,
                    revision_});
            Record({0, RolesJobsChangeKind::ShiftEnded, duty->subject, id, assignment->workplace, duty_id, context,
                    revision_});
        }
    }
    Record({0, RolesJobsChangeKind::JobRemoved, assignment->worker, id, assignment->workplace, {}, context, revision_});
    RebuildIndexes();
    PruneTerminalDuties();
    return foundation::Result<void>::Success();
}

foundation::Result<WorkScheduleId> RolesJobsService::CreateSchedule(WorkSchedule schedule)
{
    if (!schedule.assignment.IsValid() || !assignments_.contains(schedule.assignment))
        return foundation::Result<WorkScheduleId>::Failure(
            Error("gameplay.roles_jobs.invalid_schedule", "invalid schedule"));
    const auto assignment = assignments_.find(schedule.assignment);
    if (assignment == assignments_.end() || IsTerminalAssignment(assignment->second.state))
        return foundation::Result<WorkScheduleId>::Failure(
            Error("gameplay.roles_jobs.invalid_schedule", "schedule requires live assignment"));

    const auto definition = definitions_.find(assignment->second.job);
    if (definition == definitions_.end())
        return foundation::Result<WorkScheduleId>::Failure(
            Error("gameplay.roles_jobs.invalid_schedule", "assignment job definition missing"));

    auto staged_schedule_ids = schedule_ids_;
    auto staged_shift_ids = shift_ids_;
    if (!schedule.id.IsValid())
        schedule.id = WorkScheduleId{staged_schedule_ids.Next()};
    else
        AdvanceGeneratorPastAcceptedId(staged_schedule_ids, schedule.id);
    if (!schedule.id.IsValid())
        return foundation::Result<WorkScheduleId>::Failure(
            Error("gameplay.roles_jobs.id_exhausted", "schedule id is exhausted"));
    if (schedules_.contains(schedule.id))
        return foundation::Result<WorkScheduleId>::Failure(
            Error("gameplay.roles_jobs.duplicate_schedule", "duplicate schedule"));

    std::unordered_set<WorkShiftId, IdHash> local_shift_ids;
    for (auto &shift : schedule.shifts)
    {
        if (!IsPositiveDuration(shift.duration) || !IsNonNegativeDuration(shift.recurrence))
            return foundation::Result<WorkScheduleId>::Failure(
                Error("gameplay.roles_jobs.invalid_shift", "shift duration or recurrence is invalid"));
        if (shift.recurrence.ticks > 0 && shift.recurrence.ticks < shift.duration.ticks)
            return foundation::Result<WorkScheduleId>::Failure(
                Error("gameplay.roles_jobs.invalid_shift", "shift recurrence cannot be shorter than duration"));
        if (!shift.task_type.IsValid() && definition->second.default_tasks.empty())
            return foundation::Result<WorkScheduleId>::Failure(
                Error("gameplay.roles_jobs.invalid_shift", "shift requires a task or default job tasks"));
        if (!shift.id.IsValid())
            shift.id = WorkShiftId{staged_shift_ids.Next()};
        else
            AdvanceGeneratorPastAcceptedId(staged_shift_ids, shift.id);
        if (!shift.id.IsValid())
            return foundation::Result<WorkScheduleId>::Failure(
                Error("gameplay.roles_jobs.id_exhausted", "shift id is exhausted"));
        if (!local_shift_ids.insert(shift.id).second)
            return foundation::Result<WorkScheduleId>::Failure(
                Error("gameplay.roles_jobs.duplicate_shift", "duplicate shift in schedule"));
    }

    auto next_revision = NextRevision();
    if (!next_revision)
        return foundation::Result<WorkScheduleId>::Failure(next_revision.GetError());

    schedule_ids_ = staged_schedule_ids;
    shift_ids_ = staged_shift_ids;
    Bump(next_revision.Value());
    schedule.revision = revision_;
    const auto id = schedule.id;
    schedules_.emplace(id, std::move(schedule));
    RebuildIndexes();
    return foundation::Result<WorkScheduleId>::Success(id);
}

foundation::Result<DutyId> RolesJobsService::CreateDutyInternal(Duty duty, RolesJobsChangeKind initial_shift_change,
                                                                GameplayContext context)
{
    auto staged_ids = duty_ids_;
    if (!duty.id.IsValid())
        duty.id = DutyId{staged_ids.Next()};
    else
        AdvanceGeneratorPastAcceptedId(staged_ids, duty.id);
    if (!duty.id.IsValid())
        return foundation::Result<DutyId>::Failure(Error("gameplay.roles_jobs.id_exhausted", "duty id is exhausted"));
    if (duties_.contains(duty.id))
        return foundation::Result<DutyId>::Failure(Error("gameplay.roles_jobs.duplicate_duty", "duplicate duty"));

    auto next_revision = NextRevision();
    if (!next_revision)
        return foundation::Result<DutyId>::Failure(next_revision.GetError());

    duty_ids_ = staged_ids;
    Bump(next_revision.Value());
    duty.revision = revision_;
    const auto id = duty.id;
    const auto subject = duty.subject;
    const auto assignment = duty.assignment;
    WorkplaceId workplace{};
    if (const auto assignment_it = assignments_.find(assignment); assignment_it != assignments_.end())
        workplace = assignment_it->second.workplace;
    duties_.emplace(id, std::move(duty));
    RebuildIndexes();
    Record({0, RolesJobsChangeKind::DutyCreated, subject, assignment, workplace, id, context, revision_});
    if (initial_shift_change == RolesJobsChangeKind::ShiftStarted)
    {
        Record({0, RolesJobsChangeKind::ShiftStarted, subject, assignment, workplace, id, context, revision_});
        Record({0, RolesJobsChangeKind::DutyActivated, subject, assignment, workplace, id, context, revision_});
        ++diagnostics_.shift_events;
    }
    else if (initial_shift_change == RolesJobsChangeKind::ShiftScheduled)
    {
        Record({0, RolesJobsChangeKind::ShiftScheduled, subject, assignment, workplace, id, context, revision_});
    }
    else if (initial_shift_change == RolesJobsChangeKind::DutySkipped)
    {
        Record({0, RolesJobsChangeKind::DutySkipped, subject, assignment, workplace, id, context, revision_});
        Record({0, RolesJobsChangeKind::ShiftEnded, subject, assignment, workplace, id, context, revision_});
    }
    return foundation::Result<DutyId>::Success(id);
}

foundation::Result<DutyId> RolesJobsService::CreateDuty(Duty duty, GameplayContext context)
{
    if (!duty.subject.IsValid())
        return foundation::Result<DutyId>::Failure(Error("gameplay.roles_jobs.invalid_duty", "invalid duty"));
    if (!IsValidDutyState(duty.state))
        return foundation::Result<DutyId>::Failure(Error("gameplay.roles_jobs.invalid_duty_state", "invalid duty state"));
    if (IsTerminalDuty(duty.state))
        return foundation::Result<DutyId>::Failure(
            Error("gameplay.roles_jobs.invalid_duty_state", "new duty cannot start in terminal state"));
    if (duty.assignment.IsValid())
    {
        const auto assignment = assignments_.find(duty.assignment);
        if (assignment == assignments_.end() || IsTerminalAssignment(assignment->second.state))
            return foundation::Result<DutyId>::Failure(
                Error("gameplay.roles_jobs.invalid_duty", "duty assignment is invalid"));
        if (duty.state == DutyState::Active &&
            (assignment->second.state != AssignmentState::Active || !IsWorkplaceOperational(assignment->second.workplace)))
            return foundation::Result<DutyId>::Failure(
                Error("gameplay.roles_jobs.workplace_not_operational", "active duty requires active assignment and workplace"));
        if (duty.shift.IsValid())
        {
            bool shift_found = false;
            for (const auto &[schedule_id, schedule] : schedules_)
            {
                (void)schedule_id;
                if (schedule.assignment != duty.assignment)
                    continue;
                for (const auto &shift : schedule.shifts)
                    shift_found = shift_found || shift.id == duty.shift;
            }
            if (!shift_found)
                return foundation::Result<DutyId>::Failure(
                    Error("gameplay.roles_jobs.invalid_duty", "duty shift does not belong to assignment"));
        }
    }
    return CreateDutyInternal(std::move(duty), duty.state == DutyState::Active ? RolesJobsChangeKind::ShiftStarted
                                                                               : RolesJobsChangeKind::ShiftScheduled,
                              context);
}

std::vector<DutyId> RolesJobsService::ActivateDueShifts(GameplayTimePoint now, GameplayContext context)
{
    return ActivateDueShifts(OneTickBefore(now), now, context);
}

std::vector<DutyId> RolesJobsService::ActivateDueShifts(GameplayTimePoint from, GameplayTimePoint to,
                                                        GameplayContext context)
{
    std::vector<DutyId> created;
    if (to < from)
        return created;

    std::vector<WorkSchedule> schedules;
    schedules.reserve(schedules_.size());
    for (const auto &[id, schedule] : schedules_)
    {
        (void)id;
        schedules.push_back(schedule);
    }
    std::sort(schedules.begin(), schedules.end(), [](const auto &lhs, const auto &rhs) {
        if (lhs.priority != rhs.priority)
            return lhs.priority > rhs.priority;
        return lhs.id < rhs.id;
    });

    for (const auto &schedule : schedules)
    {
        auto assignment_it = assignments_.find(schedule.assignment);
        if (assignment_it == assignments_.end() || assignment_it->second.state != AssignmentState::Active)
            continue;
        if (!IsWorkplaceOperational(assignment_it->second.workplace))
        {
            const auto suspended = SuspendAssignment(schedule.assignment, context);
            if (!suspended)
                ++diagnostics_.automatic_suspension_failures;
            continue;
        }
        const auto definition_it = definitions_.find(assignment_it->second.job);
        if (definition_it == definitions_.end())
            continue;

        std::vector<WorkShift> shifts = schedule.shifts;
        std::sort(shifts.begin(), shifts.end(), [](const auto &lhs, const auto &rhs) {
            if (lhs.start != rhs.start)
                return lhs.start < rhs.start;
            return lhs.id < rhs.id;
        });

        for (const auto &shift : shifts)
        {
            if (!IsPositiveDuration(shift.duration))
                continue;

            auto occurrence_start = shift.start;
            if (shift.recurrence.ticks > 0)
            {
                const auto base_end = CheckedAdd(shift.start, shift.duration);
                if (!base_end.has_value())
                    continue;
                if (base_end->ticks <= from.ticks)
                {
                    const auto delta = CheckedDifference(from, *base_end);
                    if (!delta.has_value())
                        continue;
                    const auto skipped_occurrences = delta->ticks / shift.recurrence.ticks;
                    if (skipped_occurrences == std::numeric_limits<std::int64_t>::max())
                        continue;
                    const auto skip_count = skipped_occurrences + 1;
                    if (skip_count > std::numeric_limits<std::int64_t>::max() / shift.recurrence.ticks)
                        continue;
                    const auto advanced = CheckedAdd(shift.start, GameplayDuration{skip_count * shift.recurrence.ticks});
                    if (!advanced.has_value())
                        continue;
                    occurrence_start = *advanced;
                }
            }

            while (occurrence_start.ticks <= to.ticks)
            {
                const auto occurrence_end = CheckedAdd(occurrence_start, shift.duration);
                if (!occurrence_end.has_value())
                    break;
                if (!IntersectsOpenClosed(from, to, occurrence_start, *occurrence_end))
                    break;

                if (!HasDutyOccurrence(schedule.assignment, shift.id, occurrence_start))
                {
                    Duty duty;
                    duty.subject = assignment_it->second.worker;
                    duty.type = shift.task_type.IsValid() ? TypeId{shift.task_type.value}
                                                          : TypeId{definition_it->second.default_tasks.front().value};
                    duty.priority = schedule.priority;
                    duty.state = IsMissedAt(to, *occurrence_end) ? DutyState::Skipped : DutyState::Active;
                    duty.assignment = schedule.assignment;
                    duty.shift = shift.id;
                    duty.scheduled_start = occurrence_start;
                    duty.scheduled_end = *occurrence_end;
                    duty.payload = shift.payload;
                    if (shift.task_type.IsValid())
                        duty.tasks.push_back(shift.task_type);
                    else
                        duty.tasks = definition_it->second.default_tasks;

                    const auto created_duty = CreateDutyInternal(
                        std::move(duty),
                        IsMissedAt(to, *occurrence_end) ? RolesJobsChangeKind::DutySkipped
                                                       : RolesJobsChangeKind::ShiftStarted,
                        context);
                    if (created_duty)
                        created.push_back(created_duty.Value());
                }

                if (shift.recurrence.ticks <= 0)
                    break;
                const auto next_occurrence = CheckedAdd(occurrence_start, shift.recurrence);
                if (!next_occurrence.has_value())
                    break;
                occurrence_start = *next_occurrence;
            }
        }
    }
    PruneTerminalDuties();
    return created;
}

foundation::Result<void> RolesJobsService::TransitionDuty(DutyId id, DutyState state, RolesJobsChangeKind change,
                                                          GameplayContext context)
{
    auto *duty = FindMutableDuty(id);
    if (duty == nullptr)
        return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.duty_missing", "duty missing"));
    if (!IsValidDutyState(duty->state) || !IsValidDutyState(state))
        return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.invalid_duty_state", "invalid duty state"));
    if (!IsAllowedDutyTransition(duty->state, state))
        return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.invalid_duty_transition", "invalid duty transition"));
    if (IsTerminalDuty(duty->state))
        return foundation::Result<void>::Failure(
            Error("gameplay.roles_jobs.duty_terminal", "terminal duty cannot transition"));
    if (state == DutyState::Active)
    {
        const auto assignment = assignments_.find(duty->assignment);
        if (assignment == assignments_.end() || assignment->second.state != AssignmentState::Active ||
            !IsWorkplaceOperational(assignment->second.workplace))
            return foundation::Result<void>::Failure(
                Error("gameplay.roles_jobs.workplace_not_operational", "active duty requires active assignment and workplace"));
    }

    auto next_revision = NextRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(next_revision.GetError());

    Bump(next_revision.Value());
    duty->state = state;
    duty->revision = revision_;
    WorkplaceId workplace{};
    if (const auto assignment_it = assignments_.find(duty->assignment); assignment_it != assignments_.end())
        workplace = assignment_it->second.workplace;
    Record({0, change, duty->subject, duty->assignment, workplace, id, context, revision_});
    if (IsTerminalDuty(state) && state != DutyState::Skipped)
        Record({0, RolesJobsChangeKind::ShiftEnded, duty->subject, duty->assignment, workplace, id, context, revision_});
    if (state == DutyState::Failed)
        ++diagnostics_.duty_failures;
    RebuildIndexes();
    PruneTerminalDuties();
    return foundation::Result<void>::Success();
}

foundation::Result<void> RolesJobsService::ActivateDuty(DutyId id, GameplayContext context)
{
    return TransitionDuty(id, DutyState::Active, RolesJobsChangeKind::DutyActivated, context);
}

foundation::Result<void> RolesJobsService::CompleteDuty(DutyId id, GameplayContext context)
{
    return TransitionDuty(id, DutyState::Completed, RolesJobsChangeKind::DutyCompleted, context);
}

foundation::Result<void> RolesJobsService::FailDuty(DutyId id, GameplayContext context)
{
    return TransitionDuty(id, DutyState::Failed, RolesJobsChangeKind::DutyFailed, context);
}

foundation::Result<void> RolesJobsService::SkipDuty(DutyId id, GameplayContext context)
{
    return TransitionDuty(id, DutyState::Skipped, RolesJobsChangeKind::DutySkipped, context);
}

foundation::Result<void> RolesJobsService::CancelDuty(DutyId id, GameplayContext context)
{
    return TransitionDuty(id, DutyState::Cancelled, RolesJobsChangeKind::DutyCancelled, context);
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

const Duty *RolesJobsService::FindDuty(DutyId id) const noexcept
{
    auto it = duties_.find(id);
    return it == duties_.end() ? nullptr : &it->second;
}

const Duty *RolesJobsService::GetCurrentDuty(GameplayObjectRef subject) const noexcept
{
    const Duty *best = nullptr;
    const auto index = duties_by_subject_.find(subject);
    if (index == duties_by_subject_.end())
        return nullptr;
    for (const auto duty_id : index->second)
    {
        const auto it = duties_.find(duty_id);
        if (it == duties_.end())
            continue;
        const auto &duty = it->second;
        if (duty.state == DutyState::Active)
        {
            if (!best || duty.priority > best->priority || (duty.priority == best->priority && duty.id < best->id))
                best = &duty;
        }
    }
    return best;
}

std::vector<JobAssignment> RolesJobsService::FindJobsOfSubject(GameplayObjectRef subject) const
{
    std::vector<JobAssignment> out;
    const auto index = assignments_by_worker_.find(subject);
    if (index == assignments_by_worker_.end())
        return out;
    for (const auto id : index->second)
    {
        const auto it = assignments_.find(id);
        if (it != assignments_.end())
            out.push_back(it->second);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}

std::vector<JobAssignment> RolesJobsService::FindWorkersAtWorkplace(WorkplaceId workplace) const
{
    std::vector<JobAssignment> out;
    const auto index = assignments_by_workplace_.find(workplace);
    if (index == assignments_by_workplace_.end())
        return out;
    for (const auto id : index->second)
    {
        const auto it = assignments_.find(id);
        if (it != assignments_.end())
            out.push_back(it->second);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        if (a.worker != b.worker)
            return a.worker < b.worker;
        return a.id < b.id;
    });
    return out;
}

std::vector<Workplace> RolesJobsService::FindWorkplacesInArea(GameplayObjectRef area) const
{
    std::vector<Workplace> out;
    for (const auto &[id, workplace] : workplaces_)
    {
        (void)id;
        if (workplace.area == area)
            out.push_back(workplace);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}

std::vector<Duty> RolesJobsService::FindDutiesForAssignment(JobAssignmentId assignment) const
{
    std::vector<Duty> out;
    const auto index = duties_by_assignment_.find(assignment);
    if (index == duties_by_assignment_.end())
        return out;
    for (const auto id : index->second)
    {
        const auto it = duties_.find(id);
        if (it != duties_.end())
            out.push_back(it->second);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        if (a.scheduled_start != b.scheduled_start)
            return a.scheduled_start < b.scheduled_start;
        return a.id < b.id;
    });
    return out;
}

std::vector<Duty> RolesJobsService::FindActiveDuties() const
{
    std::vector<Duty> out;
    for (const auto &[id, duty] : duties_)
    {
        (void)id;
        if (duty.state == DutyState::Active)
            out.push_back(duty);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        if (a.subject != b.subject)
            return a.subject < b.subject;
        return a.id < b.id;
    });
    return out;
}

std::vector<RolesJobsChange> RolesJobsService::ChangesSinceSequence(std::uint64_t sequence) const
{
    return ReadChangesSinceSequence(sequence).changes;
}

RolesJobsChangeBatch RolesJobsService::ReadChangesSinceSequence(std::uint64_t sequence) const
{
    RolesJobsChangeBatch batch;
    batch.latest_sequence = LatestChangeCursor().sequence;
    batch.oldest_available_sequence = OldestChangeSequence();
    if (next_change_sequence_ == 0 || sequence > batch.latest_sequence)
    {
        batch.snapshot_required = true;
        return batch;
    }
    if (changes_.empty())
    {
        batch.snapshot_required = sequence < batch.latest_sequence;
        return batch;
    }
    if (sequence < batch.oldest_available_sequence && batch.oldest_available_sequence - sequence > 1)
    {
        batch.snapshot_required = true;
        return batch;
    }
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(batch.changes),
                 [sequence](const auto &change) { return change.sequence > sequence; });
    return batch;
}


std::uint64_t RolesJobsService::OldestChangeSequence() const noexcept
{
    return changes_.empty() ? next_change_sequence_ : changes_.front().sequence;
}

void RolesJobsService::PruneChangesBefore(std::uint64_t sequence) noexcept
{
    while (!changes_.empty() && changes_.front().sequence < sequence)
        changes_.pop_front();
}

RolesJobsSnapshot RolesJobsService::CaptureSnapshot() const
{
    RolesJobsSnapshot snapshot;
    for (const auto &[id, definition] : definitions_)
    {
        (void)id;
        snapshot.definitions.push_back(definition);
    }
    for (const auto &[id, workplace] : workplaces_)
    {
        (void)id;
        snapshot.workplaces.push_back(workplace);
    }
    for (const auto &[id, assignment] : assignments_)
    {
        (void)id;
        snapshot.assignments.push_back(assignment);
    }
    for (const auto &[id, schedule] : schedules_)
    {
        (void)id;
        snapshot.schedules.push_back(schedule);
    }
    for (const auto &[id, duty] : duties_)
    {
        (void)id;
        snapshot.duties.push_back(duty);
    }
    std::sort(snapshot.definitions.begin(), snapshot.definitions.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.workplaces.begin(), snapshot.workplaces.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.assignments.begin(), snapshot.assignments.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.schedules.begin(), snapshot.schedules.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.duties.begin(), snapshot.duties.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    snapshot.assignment_ids = assignment_ids_.GetSnapshot();
    snapshot.workplace_ids = workplace_ids_.GetSnapshot();
    snapshot.schedule_ids = schedule_ids_.GetSnapshot();
    snapshot.shift_ids = shift_ids_.GetSnapshot();
    snapshot.duty_ids = duty_ids_.GetSnapshot();
    snapshot.revision = revision_;
    snapshot.change_epoch = journal_epoch_;
    return snapshot;
}

foundation::Result<void> RolesJobsService::RestoreSnapshot(RolesJobsSnapshot snapshot)
{
    const auto next_journal_epoch = CheckedNextChangeEpoch(snapshot.change_epoch > journal_epoch_ ? snapshot.change_epoch : journal_epoch_);
    if (!next_journal_epoch)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.change_journal.epoch_exhausted", "change journal epoch is exhausted"));
    }
    std::unordered_map<JobDefinitionId, JobDefinition, IdHash> definitions;
    std::unordered_map<WorkplaceId, Workplace, IdHash> workplaces;
    std::unordered_map<JobAssignmentId, JobAssignment, IdHash> assignments;
    std::unordered_map<WorkScheduleId, WorkSchedule, IdHash> schedules;
    std::unordered_map<DutyId, Duty, IdHash> duties;

    std::vector<JobAssignmentId> assignment_ids;
    std::vector<WorkplaceId> workplace_ids;
    std::vector<WorkScheduleId> schedule_ids;
    std::vector<WorkShiftId> shift_ids;
    std::vector<DutyId> duty_ids;

    for (const auto &definition : snapshot.definitions)
    {
        if (!definition.id.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.roles_jobs.restore_invalid", "invalid definition"));
        for (const auto task : definition.default_tasks)
        {
            if (!task.IsValid())
                return foundation::Result<void>::Failure(
                    Error("gameplay.roles_jobs.restore_invalid", "invalid default task"));
        }
        if (!definitions.emplace(definition.id, definition).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.roles_jobs.restore_invalid", "duplicate definition"));
    }

    for (const auto &workplace : snapshot.workplaces)
    {
        if (!workplace.id.IsValid() || !workplace.area.IsValid() || !IsValidWorkplaceState(workplace.state) || workplace.revision.value > snapshot.revision.value)
            return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.restore_invalid", "invalid workplace"));
        if (!workplaces.emplace(workplace.id, workplace).second)
            return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.restore_invalid", "duplicate workplace"));
        workplace_ids.push_back(workplace.id);
    }

    for (const auto &assignment : snapshot.assignments)
    {
        if (!assignment.id.IsValid() || !assignment.worker.IsValid() || !assignment.job.IsValid() ||
            !definitions.contains(assignment.job) || !assignment.workplace.IsValid() ||
            !workplaces.contains(assignment.workplace) || !IsValidAssignmentState(assignment.state) || assignment.revision.value > snapshot.revision.value)
            return foundation::Result<void>::Failure(
                Error("gameplay.roles_jobs.restore_invalid", "invalid assignment"));
        if (assignment.state == AssignmentState::Active &&
            !IsWorkplaceStateOperational(workplaces.at(assignment.workplace).state))
            return foundation::Result<void>::Failure(
                Error("gameplay.roles_jobs.restore_invalid", "active assignment references non-operational workplace"));
        if (!assignments.emplace(assignment.id, assignment).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.roles_jobs.restore_invalid", "duplicate assignment"));
        assignment_ids.push_back(assignment.id);
    }

    std::unordered_set<WorkShiftId, IdHash> global_shift_ids;
    for (const auto &schedule : snapshot.schedules)
    {
        if (!schedule.id.IsValid() || !schedule.assignment.IsValid() || !assignments.contains(schedule.assignment) || schedule.revision.value > snapshot.revision.value)
            return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.restore_invalid", "invalid schedule"));
        const auto assignment = assignments.at(schedule.assignment);
        const auto definition = definitions.find(assignment.job);
        if (definition == definitions.end())
            return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.restore_invalid", "schedule job missing"));
        std::unordered_set<WorkShiftId, IdHash> local_shift_ids;
        for (const auto &shift : schedule.shifts)
        {
            if (!shift.id.IsValid() || !IsPositiveDuration(shift.duration) || !IsNonNegativeDuration(shift.recurrence) ||
                (shift.recurrence.ticks > 0 && shift.recurrence.ticks < shift.duration.ticks) ||
                (!shift.task_type.IsValid() && definition->second.default_tasks.empty()))
                return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.restore_invalid", "invalid shift"));
            if (!local_shift_ids.insert(shift.id).second || !global_shift_ids.insert(shift.id).second)
                return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.restore_invalid", "duplicate shift"));
            shift_ids.push_back(shift.id);
        }
        if (!schedules.emplace(schedule.id, schedule).second)
            return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.restore_invalid", "duplicate schedule"));
        schedule_ids.push_back(schedule.id);
    }

    std::unordered_set<DutyOccurrenceKey, DutyOccurrenceKeyHash> live_occurrences;
    for (const auto &duty : snapshot.duties)
    {
        if (!duty.id.IsValid() || !duty.subject.IsValid() || !IsValidDutyState(duty.state) || duty.revision.value > snapshot.revision.value)
            return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.restore_invalid", "invalid duty"));
        if (duty.assignment.IsValid())
        {
            const auto assignment = assignments.find(duty.assignment);
            if (assignment == assignments.end())
                return foundation::Result<void>::Failure(
                    Error("gameplay.roles_jobs.restore_invalid", "duty assignment missing"));
            if (duty.state == DutyState::Active &&
                (assignment->second.state != AssignmentState::Active ||
                 !IsWorkplaceStateOperational(workplaces.at(assignment->second.workplace).state)))
                return foundation::Result<void>::Failure(
                    Error("gameplay.roles_jobs.restore_invalid", "active duty has invalid assignment/workplace"));
            if (duty.shift.IsValid())
            {
                bool shift_found = false;
                for (const auto &[schedule_id, schedule] : schedules)
                {
                    (void)schedule_id;
                    if (schedule.assignment != duty.assignment)
                        continue;
                    for (const auto &shift : schedule.shifts)
                    {
                        if (shift.id == duty.shift)
                        {
                            shift_found = true;
                            break;
                        }
                    }
                    if (shift_found)
                        break;
                }
                if (!shift_found)
                    return foundation::Result<void>::Failure(
                        Error("gameplay.roles_jobs.restore_invalid", "duty shift does not belong to assignment"));
                if (IsLiveDuty(duty.state))
                {
                    DutyOccurrenceKey key{duty.assignment, duty.shift, duty.scheduled_start};
                    if (!live_occurrences.insert(key).second)
                        return foundation::Result<void>::Failure(
                            Error("gameplay.roles_jobs.restore_invalid", "duplicate live duty occurrence"));
                }
            }
        }
        if (!duties.emplace(duty.id, duty).second)
            return foundation::Result<void>::Failure(Error("gameplay.roles_jobs.restore_invalid", "duplicate duty"));
        duty_ids.push_back(duty.id);
    }

    const auto assignment_generator_ok = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        snapshot.assignment_ids, assignment_ids_.Scope(), MaxLowPart(assignment_ids));
    const auto workplace_generator_ok = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        snapshot.workplace_ids, workplace_ids_.Scope(), MaxLowPart(workplace_ids));
    const auto schedule_generator_ok = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        snapshot.schedule_ids, schedule_ids_.Scope(), MaxLowPart(schedule_ids));
    const auto shift_generator_ok = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        snapshot.shift_ids, shift_ids_.Scope(), MaxLowPart(shift_ids));
    const auto duty_generator_ok = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        snapshot.duty_ids, duty_ids_.Scope(), MaxLowPart(duty_ids));
    if (!assignment_generator_ok || !workplace_generator_ok || !schedule_generator_ok || !shift_generator_ok ||
        !duty_generator_ok)
        return foundation::Result<void>::Failure(
            Error("gameplay.roles_jobs.restore_invalid", "invalid id generator snapshot"));

    definitions_ = std::move(definitions);
    workplaces_ = std::move(workplaces);
    assignments_ = std::move(assignments);
    schedules_ = std::move(schedules);
    duties_ = std::move(duties);
    assignment_ids_.Restore(snapshot.assignment_ids);
    workplace_ids_.Restore(snapshot.workplace_ids);
    schedule_ids_.Restore(snapshot.schedule_ids);
    shift_ids_.Restore(snapshot.shift_ids);
    duty_ids_.Restore(snapshot.duty_ids);
    revision_ = snapshot.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    RebuildIndexes();
    journal_epoch_ = *next_journal_epoch;
    return foundation::Result<void>::Success();
}

RolesJobsDiagnostics RolesJobsService::GetDiagnostics() const noexcept
{
    auto diagnostics = diagnostics_;
    diagnostics.definitions = definitions_.size();
    diagnostics.workplaces = workplaces_.size();
    diagnostics.assignments = assignments_.size();
    diagnostics.schedules = schedules_.size();
    diagnostics.active_duties = 0;
    diagnostics.retained_changes = changes_.size();
    diagnostics.retained_terminal_duties = 0;
    for (const auto &[id, duty] : duties_)
    {
        (void)id;
        if (duty.state == DutyState::Active)
            ++diagnostics.active_duties;
        if (IsValidDutyState(duty.state) && IsTerminalDuty(duty.state))
            ++diagnostics.retained_terminal_duties;
    }
    return diagnostics;
}

void RolesJobsService::Record(RolesJobsChange change)
{
    if (next_change_sequence_ == 0)
        return;
    const auto assigned = next_change_sequence_;
    change.sequence = assigned;
    changes_.push_back(std::move(change));
    next_change_sequence_ = assigned == std::numeric_limits<std::uint64_t>::max() ? 0 : assigned + 1;
    while (changes_.size() > change_retention_capacity_)
        changes_.pop_front();
}
} // namespace epidemic::gameplay::roles_jobs
