#include "Epidemic/GameFramework/InteractionTimeIntegration/interaction_time_adapter.h"

#include <algorithm>
#include <limits>

namespace epidemic::gameplay::interaction_time_integration
{
namespace
{
[[nodiscard]] foundation::Error E(const char* code, const char* message)
{
    return foundation::Error::Create(code, message);
}

[[nodiscard]] bool IsOwnedCompletionSchedule(
    const time::ScheduleEntry& entry,
    ActionTypeId complete_action) noexcept
{
    return entry.action == complete_action && entry.owner.domain == interaction::InteractionService::Domain();
}
} // namespace

foundation::Result<void> InteractionTimeAdapter::RegisterContracts()
{
    if (complete_action_.IsValid())
    {
        return foundation::Result<void>::Success();
    }

    auto registered = time_.RegisterAction("framework.interaction.complete", interaction::InteractionService::Domain());
    if (!registered)
    {
        return foundation::Result<void>::Failure(registered.GetError());
    }
    complete_action_ = registered.Value();
    return foundation::Result<void>::Success();
}

foundation::Result<void> InteractionTimeAdapter::RegisterWithDispatcher(
    integration::ScheduledTriggerDispatcher& dispatcher)
{
    if (!complete_action_.IsValid())
    {
        return foundation::Result<void>::Failure(
            E("gameplay.interaction_time.unregistered", "interaction completion action must be registered before dispatcher binding"));
    }
    if (dispatcher_ == &dispatcher)
    {
        return foundation::Result<void>::Success();
    }
    if (dispatcher_ != nullptr)
    {
        return foundation::Result<void>::Failure(
            E("gameplay.interaction_time.dispatcher_rebind", "interaction time adapter is already bound to another dispatcher"));
    }

    auto result = dispatcher.RegisterActionHandler(
        complete_action_,
        integration::ScheduledTriggerHandlerId::FromString("framework.integration.interaction_time.complete"),
        [this](const time::ScheduledTrigger& trigger, const GameplayContext& context) {
            return HandleTrigger(trigger, context);
        });
    if (!result)
    {
        return result;
    }
    dispatcher_ = &dispatcher;
    return foundation::Result<void>::Success();
}

foundation::Result<void> InteractionTimeAdapter::CancelOrRecord(
    interaction::InteractionExecutionId execution,
    ScheduleId schedule)
{
    if (!schedule.IsValid() || !time_.HasSchedule(schedule))
    {
        return foundation::Result<void>::Success();
    }

    auto cancelled = time_.Cancel(schedule);
    if (cancelled || !time_.HasSchedule(schedule))
    {
        return foundation::Result<void>::Success();
    }

    const auto exists = std::find_if(reconciliations_.begin(), reconciliations_.end(), [schedule](const auto& record) {
        return record.schedule == schedule;
    });
    if (exists == reconciliations_.end())
    {
        reconciliations_.push_back(
            {InteractionTimeReconciliationKind::OrphanScheduleCancellation, execution, schedule});
    }
    return foundation::Result<void>::Failure(cancelled.GetError());
}

std::uint64_t InteractionTimeAdapter::LatestRetainedSequence() const
{
    const auto first = interactions_.ReadChangesSince(0);
    if (!first.snapshot_required)
    {
        return first.changes.empty() ? 0 : first.changes.back().sequence;
    }

    const auto oldest = first.oldest_available_sequence;
    const auto retained = interactions_.ReadChangesSince(oldest == 0 ? 0 : oldest - 1);
    if (!retained.changes.empty())
    {
        return retained.changes.back().sequence;
    }
    return oldest == 0 ? 0 : oldest - 1;
}

bool InteractionTimeAdapter::HasPendingCompletion(interaction::InteractionExecutionId execution) const
{
    if (dispatcher_ == nullptr)
    {
        return false;
    }
    const auto snapshot = dispatcher_->CaptureSnapshot();
    return std::any_of(snapshot.pending.begin(), snapshot.pending.end(), [this, execution](const auto& delivery) {
        return delivery.trigger.action == complete_action_ &&
               delivery.trigger.owner == SessionRef(execution);
    });
}

foundation::Result<std::uint64_t> InteractionTimeAdapter::ReconcileSchedules(ClockId clock)
{
    if (!clock.IsValid() || !complete_action_.IsValid())
    {
        return foundation::Result<std::uint64_t>::Failure(
            E("gameplay.interaction_time.invalid_reconcile", "interaction time reconciliation requires a valid clock and registered action"));
    }

    std::uint64_t mutations = 0;

    // First retry cleanup operations that previously failed. The full scan below remains authoritative, so this list is
    // only a durable diagnostic/retry aid rather than a second source of scheduler truth.
    std::vector<InteractionTimeReconciliationRecord> remaining;
    remaining.reserve(reconciliations_.size());
    for (const auto& record : reconciliations_)
    {
        if (!time_.HasSchedule(record.schedule))
        {
            continue;
        }
        auto cancelled = time_.Cancel(record.schedule);
        if (!cancelled && time_.HasSchedule(record.schedule))
        {
            remaining.push_back(record);
            continue;
        }
        ++mutations;
    }
    reconciliations_.swap(remaining);

    auto time_snapshot = time_.CaptureSnapshot();
    std::sort(time_snapshot.schedules.begin(), time_snapshot.schedules.end(), [](const auto& left, const auto& right) {
        return left.id < right.id;
    });

    // Existing schedules are reconciled before new schedules are created. This is important after SaveGame restore:
    // Interaction intentionally clears scheduler handles, while Time may restore the persistent schedule itself.
    for (const auto& entry : time_snapshot.schedules)
    {
        if (!IsOwnedCompletionSchedule(entry, complete_action_))
        {
            continue;
        }

        const interaction::InteractionExecutionId execution{entry.owner.id};
        const auto* session = interactions_.FindSession(execution);
        if (session == nullptr || !session->completes_at.has_value())
        {
            const auto cancelled = CancelOrRecord(execution, entry.id);
            if (!cancelled)
            {
                return foundation::Result<std::uint64_t>::Failure(cancelled.GetError());
            }
            ++mutations;
            continue;
        }

        if (entry.clock != clock)
        {
            // A bound schedule on another clock cannot be safely rebound without mutating Interaction ownership state.
            // Treat it as an invariant violation instead of silently creating a duplicate completion occurrence.
            if (session->completion_schedule.has_value() && *session->completion_schedule == entry.id)
            {
                return foundation::Result<std::uint64_t>::Failure(
                    E("gameplay.interaction_time.clock_mismatch", "bound interaction completion schedule belongs to another clock"));
            }
            const auto cancelled = CancelOrRecord(execution, entry.id);
            if (!cancelled)
            {
                return foundation::Result<std::uint64_t>::Failure(cancelled.GetError());
            }
            ++mutations;
            continue;
        }

        if (session->completion_schedule.has_value() && *session->completion_schedule != entry.id)
        {
            const auto cancelled = CancelOrRecord(execution, entry.id);
            if (!cancelled)
            {
                return foundation::Result<std::uint64_t>::Failure(cancelled.GetError());
            }
            ++mutations;
            continue;
        }

        if (entry.due != *session->completes_at)
        {
            auto rescheduled = time_.Reschedule(entry.id, *session->completes_at);
            if (!rescheduled)
            {
                return foundation::Result<std::uint64_t>::Failure(rescheduled.GetError());
            }
            ++mutations;
        }

        if (!session->completion_schedule.has_value())
        {
            auto bound = interactions_.BindCompletionSchedule(execution, entry.id);
            if (!bound)
            {
                const auto cancelled = CancelOrRecord(execution, entry.id);
                if (!cancelled)
                {
                    return foundation::Result<std::uint64_t>::Failure(cancelled.GetError());
                }
                return foundation::Result<std::uint64_t>::Failure(bound.GetError());
            }
            ++mutations;
        }
    }

    // Any active timed session still lacking a binding has no restored/matching schedule and receives a new one.
    for (const auto& session : interactions_.SessionsRequiringSchedule())
    {
        // After restore the Interaction snapshot intentionally has no scheduler handle. If the Time occurrence had
        // already been collected before the save, the authoritative continuation is the dispatcher's persisted inbox,
        // not a newly created duplicate schedule.
        if (HasPendingCompletion(session.id))
        {
            continue;
        }

        auto scheduled = time_.Schedule(
            clock,
            *session.completes_at,
            SessionRef(session.id),
            complete_action_,
            {},
            time::CatchUpPolicy::FireOnce,
            time::SchedulePersistence::Persistent);
        if (!scheduled)
        {
            return foundation::Result<std::uint64_t>::Failure(scheduled.GetError());
        }

        auto bound = interactions_.BindCompletionSchedule(session.id, scheduled.Value());
        if (!bound)
        {
            const auto cancelled = CancelOrRecord(session.id, scheduled.Value());
            if (!cancelled)
            {
                return foundation::Result<std::uint64_t>::Failure(cancelled.GetError());
            }
            return foundation::Result<std::uint64_t>::Failure(bound.GetError());
        }
        ++mutations;
    }

    return foundation::Result<std::uint64_t>::Success(mutations);
}

foundation::Result<std::uint64_t> InteractionTimeAdapter::Synchronize(ClockId clock)
{
    if (dispatcher_ == nullptr)
    {
        return foundation::Result<std::uint64_t>::Failure(
            E("gameplay.interaction_time.dispatcher_unbound", "interaction time adapter must be registered with the shared trigger dispatcher"));
    }

    // Reconciliation is deliberately authoritative and runs even when the incremental cursor is valid. This makes the
    // cursor an optimization only and lets restore/journal pruning recover from current Interaction + Time state.
    auto reconciled = ReconcileSchedules(clock);
    if (!reconciled)
    {
        return reconciled;
    }

    const auto batch = interactions_.ReadChangesSince(cursor_);
    if (batch.snapshot_required)
    {
        cursor_ = LatestRetainedSequence();
        return reconciled;
    }
    if (!batch.changes.empty())
    {
        cursor_ = batch.changes.back().sequence;
    }
    return reconciled;
}

foundation::Result<integration::ScheduledTriggerDisposition> InteractionTimeAdapter::HandleTrigger(
    const time::ScheduledTrigger& trigger,
    GameplayContext context)
{
    if (trigger.action != complete_action_ || trigger.owner.domain != interaction::InteractionService::Domain())
    {
        return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
            integration::ScheduledTriggerDisposition::DiscardTerminal);
    }

    const interaction::InteractionExecutionId execution{trigger.owner.id};
    const auto* session = interactions_.FindSession(execution);
    if (session == nullptr)
    {
        return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
            integration::ScheduledTriggerDisposition::DiscardTerminal);
    }

    // A trigger from an obsolete schedule must never complete a session that has since been rebound elsewhere.
    if (session->completion_schedule.has_value() && *session->completion_schedule != trigger.schedule)
    {
        return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
            integration::ScheduledTriggerDisposition::DiscardTerminal);
    }

    // Scheduler observation time is the semantic completion time, including catch-up after load. Caller processing time
    // must not rewrite the chronology of the timed interaction.
    context.time = trigger.observed_at;
    const auto completed = interactions_.Complete(execution, context);
    if (completed)
    {
        return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
            integration::ScheduledTriggerDisposition::Ack);
    }

    // Interaction completion failures may be terminal (current Interaction removes the session for validation/commit
    // failures) or recoverable in future owner implementations. Only the latter keeps the dispatcher occurrence pending.
    if (interactions_.FindSession(execution) == nullptr)
    {
        return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
            integration::ScheduledTriggerDisposition::DiscardTerminal);
    }
    return foundation::Result<integration::ScheduledTriggerDisposition>::Success(
        integration::ScheduledTriggerDisposition::Retry);
}
} // namespace epidemic::gameplay::interaction_time_integration
