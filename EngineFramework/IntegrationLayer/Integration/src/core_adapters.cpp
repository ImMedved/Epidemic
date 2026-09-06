#include "Epidemic/GameFramework/Integration/core_adapters.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <string>
#include <unordered_set>

namespace epidemic::gameplay::integration
{
namespace
{
[[nodiscard]] bool ContainsHandler(
    const std::vector<ScheduledTriggerHandlerId>& handlers,
    ScheduledTriggerHandlerId id) noexcept
{
    return std::find(handlers.begin(), handlers.end(), id) != handlers.end();
}

[[nodiscard]] bool SameTriggerIdentity(
    const time::ScheduledTrigger& left,
    const time::ScheduledTrigger& right) noexcept
{
    return left.schedule == right.schedule && left.clock == right.clock && left.action == right.action &&
           left.scheduled_for == right.scheduled_for;
}

[[nodiscard]] OperationId DeliveryOperationId(const time::ScheduledTrigger& trigger) noexcept
{
    auto mix = [](std::uint64_t value) noexcept {
        value ^= value >> 30u;
        value *= 0xBF58476D1CE4E5B9ull;
        value ^= value >> 27u;
        value *= 0x94D049BB133111EBull;
        value ^= value >> 31u;
        return value;
    };

    const auto scheduled = static_cast<std::uint64_t>(trigger.scheduled_for.ticks);
    auto high = mix(trigger.schedule.High() ^ trigger.action.Raw() ^ scheduled);
    auto low = mix(trigger.schedule.Low() ^ trigger.clock.Raw() ^ (scheduled + 0x9E3779B97F4A7C15ull));
    if (high == 0 && low == 0)
    {
        low = 1;
    }
    return OperationId::FromRaw(high, low);
}
} // namespace

foundation::Result<void> FactsQueryAdapter::RegisterProviders()
{
    if (registered_)
    {
        return foundation::Result<void>::Success();
    }

    const queries::QueryProviderCapabilities capabilities{true, true, false, false};

    const auto facts_result = queries_.RegisterSnapshotProvider<FindFactsQuery, facts::FactsSnapshot>(
        "framework.query.facts.find",
        capabilities,
        [this](const FindFactsQuery& query, const queries::QueryContext&) {
            auto values = facts_.FindFacts(query.type, query.subject, query.scope);
            queries::QueryMetadata metadata;
            metadata.revision = facts_.FactRevision();
            metadata.coverage = queries::QueryCoverage::Complete;
            metadata.result_count = values.size();
            metadata.work_units = values.size();
            metadata.deterministic_order = true;
            return foundation::Result<queries::QueryResponse<FindFactsQuery::ResultType>>::Success(
                {std::move(values), metadata});
        },
        [this](const queries::QueryContext&) {
            auto snapshot = facts_.CaptureSnapshot();
            if (!snapshot)
            {
                return foundation::Result<queries::QueryProviderSnapshot<facts::FactsSnapshot>>::Failure(snapshot.GetError());
            }
            return foundation::Result<queries::QueryProviderSnapshot<facts::FactsSnapshot>>::Success(
                {snapshot.Value(), snapshot.Value().fact_revision});
        },
        [](const FindFactsQuery& query, const queries::QueryContext&, const facts::FactsSnapshot& snapshot) {
            std::vector<facts::FactRecord> values;
            for (const auto& fact : snapshot.facts)
            {
                if (fact.key.type == query.type && (!query.subject.IsValid() || fact.key.subject == query.subject) &&
                    (!query.scope.IsValid() || fact.key.scope == query.scope))
                {
                    values.push_back(fact);
                }
            }
            std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
            queries::QueryMetadata metadata;
            metadata.revision = snapshot.fact_revision;
            metadata.coverage = queries::QueryCoverage::Complete;
            metadata.result_count = values.size();
            metadata.work_units = values.size();
            metadata.deterministic_order = true;
            return foundation::Result<queries::QueryResponse<FindFactsQuery::ResultType>>::Success(
                {std::move(values), metadata});
        });
    if (!facts_result)
    {
        return facts_result;
    }

    const auto history_result = queries_.RegisterSnapshotProvider<FindHistoryQuery, facts::FactsSnapshot>(
        "framework.query.history.find",
        capabilities,
        [this](const FindHistoryQuery& query, const queries::QueryContext&) {
            auto values = facts_.FindHistory(query.type, query.subject);
            queries::QueryMetadata metadata;
            metadata.revision = facts_.FactRevision();
            metadata.coverage = queries::QueryCoverage::Complete;
            metadata.result_count = values.size();
            metadata.work_units = values.size();
            metadata.deterministic_order = true;
            return foundation::Result<queries::QueryResponse<FindHistoryQuery::ResultType>>::Success(
                {std::move(values), metadata});
        },
        [this](const queries::QueryContext&) {
            auto snapshot = facts_.CaptureSnapshot();
            if (!snapshot)
            {
                return foundation::Result<queries::QueryProviderSnapshot<facts::FactsSnapshot>>::Failure(snapshot.GetError());
            }
            return foundation::Result<queries::QueryProviderSnapshot<facts::FactsSnapshot>>::Success(
                {snapshot.Value(), snapshot.Value().fact_revision});
        },
        [](const FindHistoryQuery& query, const queries::QueryContext&, const facts::FactsSnapshot& snapshot) {
            std::vector<facts::EventRecord> values;
            for (const auto& event : snapshot.history)
            {
                if (event.envelope.type == query.type && (!query.subject.IsValid() || event.envelope.subject == query.subject))
                {
                    values.push_back(event);
                }
            }
            std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) {
                return left.envelope.sequence < right.envelope.sequence;
            });
            queries::QueryMetadata metadata;
            metadata.revision = snapshot.fact_revision;
            metadata.coverage = queries::QueryCoverage::Complete;
            metadata.result_count = values.size();
            metadata.work_units = values.size();
            metadata.deterministic_order = true;
            return foundation::Result<queries::QueryResponse<FindHistoryQuery::ResultType>>::Success(
                {std::move(values), metadata});
        });
    if (!history_result)
    {
        return history_result;
    }

    registered_ = true;
    return foundation::Result<void>::Success();
}

foundation::Result<void> RuntimeTimeAdapter::Synchronize()
{
    const auto snapshot = runtime_clock_.GetSnapshot();
    return gameplay_time_.SynchronizeClock(clock_, GameplayTimePoint{snapshot.now.ticks}, Revision{snapshot.revision});
}

foundation::Result<void> ScheduledTriggerDispatcher::RegisterObserver(
    ScheduledTriggerHandlerId id,
    int priority,
    Handler handler)
{
    if (frozen_)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_dispatcher_frozen", "scheduled trigger dispatcher registry is frozen"));
    }
    if (!id.IsValid() || !handler)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_handler_invalid", "scheduled trigger observer id/callback must be valid"));
    }
    if (HandlerExists(id))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_handler_duplicate", "scheduled trigger handler id is already registered"));
    }

    observers_.push_back(RegisteredHandler{id, priority, std::move(handler)});
    std::sort(observers_.begin(), observers_.end(), [](const RegisteredHandler& left, const RegisteredHandler& right) {
        if (left.priority != right.priority)
        {
            return left.priority > right.priority;
        }
        return left.id < right.id;
    });
    return foundation::Result<void>::Success();
}

foundation::Result<void> ScheduledTriggerDispatcher::RegisterActionHandler(
    ActionTypeId action,
    ScheduledTriggerHandlerId id,
    Handler handler)
{
    if (frozen_)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_dispatcher_frozen", "scheduled trigger dispatcher registry is frozen"));
    }
    if (!action.IsValid() || !id.IsValid() || !handler)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_handler_invalid", "scheduled trigger action/id/callback must be valid"));
    }
    if (HandlerExists(id) || action_handlers_.contains(action))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_handler_duplicate", "scheduled trigger action or handler id is already registered"));
    }

    action_handlers_.emplace(action, RegisteredHandler{id, 0, std::move(handler)});
    return foundation::Result<void>::Success();
}

bool ScheduledTriggerDispatcher::HandlerExists(ScheduledTriggerHandlerId id) const noexcept
{
    if (std::any_of(observers_.begin(), observers_.end(), [id](const RegisteredHandler& value) { return value.id == id; }))
    {
        return true;
    }
    return std::any_of(action_handlers_.begin(), action_handlers_.end(), [id](const auto& value) {
        return value.second.id == id;
    });
}

const ScheduledTriggerDispatcher::RegisteredHandler* ScheduledTriggerDispatcher::FindHandler(
    ScheduledTriggerHandlerId id) const noexcept
{
    const auto observer = std::find_if(observers_.begin(), observers_.end(), [id](const RegisteredHandler& value) {
        return value.id == id;
    });
    if (observer != observers_.end())
    {
        return &*observer;
    }

    const auto action = std::find_if(action_handlers_.begin(), action_handlers_.end(), [id](const auto& value) {
        return value.second.id == id;
    });
    return action == action_handlers_.end() ? nullptr : &action->second;
}

std::vector<ScheduledTriggerHandlerId> ScheduledTriggerDispatcher::RequiredHandlersFor(ActionTypeId action) const
{
    std::vector<ScheduledTriggerHandlerId> result;
    result.reserve(observers_.size() + 1);
    for (const auto& observer : observers_)
    {
        result.push_back(observer.id);
    }

    const auto found = action_handlers_.find(action);
    if (found != action_handlers_.end() && !ContainsHandler(result, found->second.id))
    {
        result.push_back(found->second.id);
    }
    return result;
}

foundation::Result<std::uint64_t> ScheduledTriggerDispatcher::CollectDue(
    ClockId clock,
    GameplayContext context,
    time::SchedulerBudget budget)
{
    if (!frozen_)
    {
        return foundation::Result<std::uint64_t>::Failure(
            foundation::Error::Create("integration.trigger_dispatcher_not_frozen", "scheduled trigger handlers must be frozen before collection"));
    }
    if (!clock.IsValid() || policy_.max_pending_deliveries == 0 || budget.max_triggers == 0)
    {
        return foundation::Result<std::uint64_t>::Failure(
            foundation::Error::Create("integration.trigger_collection_invalid", "scheduled trigger collection arguments/policy are invalid"));
    }
    if (pending_.size() >= policy_.max_pending_deliveries)
    {
        return foundation::Result<std::uint64_t>::Failure(
            foundation::Error::Create("integration.trigger_pending_capacity", "scheduled trigger pending delivery capacity is exhausted"));
    }

    const auto available = policy_.max_pending_deliveries - pending_.size();
    const auto max_collect = std::min<std::uint64_t>(budget.max_triggers, static_cast<std::uint64_t>(available));
    if (max_collect == 0)
    {
        return foundation::Result<std::uint64_t>::Failure(
            foundation::Error::Create("integration.trigger_pending_capacity", "scheduled trigger pending delivery capacity is exhausted"));
    }

    try
    {
        pending_.reserve(pending_.size() + static_cast<std::size_t>(max_collect));
    }
    catch (...)
    {
        return foundation::Result<std::uint64_t>::Failure(
            foundation::Error::Create("integration.trigger_pending_allocation", "failed to reserve pending trigger delivery capacity"));
    }

    budget.max_triggers = max_collect;
    auto collected = time_.CollectDue(clock, budget);
    if (!collected)
    {
        return foundation::Result<std::uint64_t>::Failure(collected.GetError());
    }

    auto triggers = std::move(collected).Value();
    for (auto& trigger : triggers)
    {
        auto trigger_context = context;
        trigger_context.time = trigger.observed_at;
        pending_.push_back(ScheduledTriggerDeliveryRecord{std::move(trigger), std::move(trigger_context), {}});
    }
    return foundation::Result<std::uint64_t>::Success(static_cast<std::uint64_t>(triggers.size()));
}

ScheduledTriggerPumpReport ScheduledTriggerDispatcher::DispatchPending(std::uint64_t max_attempts)
{
    ScheduledTriggerPumpReport report;
    if (max_attempts == 0)
    {
        max_attempts = policy_.max_delivery_attempts_per_pump;
    }
    if (max_attempts == 0 || pending_.empty())
    {
        report.pending = static_cast<std::uint64_t>(pending_.size());
        return report;
    }

    std::vector<ScheduledTriggerDeliveryRecord> remaining;
    remaining.reserve(pending_.size());
    std::uint64_t attempts = 0;

    for (std::size_t index = 0; index < pending_.size(); ++index)
    {
        auto delivery = std::move(pending_[index]);
        const auto required = RequiredHandlersFor(delivery.trigger.action);
        if (required.empty())
        {
            ++report.unhandled;
            remaining.push_back(std::move(delivery));
            continue;
        }

        bool retry = false;
        bool discarded = false;
        for (const auto handler_id : required)
        {
            if (ContainsHandler(delivery.completed_handlers, handler_id))
            {
                continue;
            }
            if (attempts >= max_attempts)
            {
                retry = true;
                break;
            }

            const auto* handler = FindHandler(handler_id);
            if (handler == nullptr)
            {
                ++report.handler_failures;
                ++report.retry_requests;
                retry = true;
                break;
            }

            ++attempts;
            foundation::Result<ScheduledTriggerDisposition> result =
                foundation::Result<ScheduledTriggerDisposition>::Failure(
                    foundation::Error::Create("integration.trigger_handler_failed", "scheduled trigger handler failed"));
            try
            {
                result = handler->callback(delivery.trigger, delivery.context);
            }
            catch (const std::exception&)
            {
                ++report.handler_failures;
                ++report.retry_requests;
                retry = true;
                break;
            }
            catch (...)
            {
                ++report.handler_failures;
                ++report.retry_requests;
                retry = true;
                break;
            }

            if (!result)
            {
                ++report.handler_failures;
                ++report.retry_requests;
                retry = true;
                break;
            }

            switch (result.Value())
            {
            case ScheduledTriggerDisposition::Ack:
                delivery.completed_handlers.push_back(handler_id);
                break;
            case ScheduledTriggerDisposition::DiscardTerminal:
                delivery.completed_handlers.push_back(handler_id);
                discarded = true;
                break;
            case ScheduledTriggerDisposition::Retry:
                ++report.retry_requests;
                retry = true;
                break;
            }

            if (retry)
            {
                break;
            }
        }

        const auto all_complete = std::all_of(required.begin(), required.end(), [&delivery](ScheduledTriggerHandlerId id) {
            return ContainsHandler(delivery.completed_handlers, id);
        });
        if (all_complete)
        {
            ++report.acknowledged;
            if (discarded)
            {
                ++report.discarded_terminal;
            }
            continue;
        }

        remaining.push_back(std::move(delivery));
    }

    pending_.swap(remaining);
    report.pending = static_cast<std::uint64_t>(pending_.size());
    return report;
}

foundation::Result<ScheduledTriggerPumpReport> ScheduledTriggerDispatcher::Pump(
    ClockId clock,
    GameplayContext context,
    time::SchedulerBudget budget)
{
    const auto collected = CollectDue(clock, std::move(context), budget);
    if (!collected)
    {
        // A full pending queue is backpressure, not loss. Caller may retry DispatchPending before collecting again.
        return foundation::Result<ScheduledTriggerPumpReport>::Failure(collected.GetError());
    }

    auto report = DispatchPending();
    report.collected = collected.Value();
    return foundation::Result<ScheduledTriggerPumpReport>::Success(report);
}

ScheduledTriggerDispatcherSnapshot ScheduledTriggerDispatcher::CaptureSnapshot() const
{
    ScheduledTriggerDispatcherSnapshot snapshot;
    snapshot.pending = pending_;
    return snapshot;
}

foundation::Result<void> ScheduledTriggerDispatcher::ValidateSnapshot(
    const ScheduledTriggerDispatcherSnapshot& snapshot) const
{
    if (!frozen_)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_dispatcher_not_frozen", "dispatcher must be frozen before restoring pending deliveries"));
    }
    if (snapshot.pending.size() > policy_.max_pending_deliveries)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_snapshot_capacity", "trigger dispatcher snapshot exceeds configured capacity"));
    }

    for (std::size_t index = 0; index < snapshot.pending.size(); ++index)
    {
        const auto& delivery = snapshot.pending[index];
        const auto& trigger = delivery.trigger;
        if (!trigger.schedule.IsValid() || !trigger.clock.IsValid() || !trigger.owner.IsValid() || !trigger.action.IsValid() ||
            trigger.occurrence_count == 0 || delivery.context.time != trigger.observed_at)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("integration.trigger_snapshot_invalid", "pending scheduled trigger snapshot record is invalid"));
        }

        for (std::size_t other = 0; other < index; ++other)
        {
            if (SameTriggerIdentity(snapshot.pending[other].trigger, trigger))
            {
                return foundation::Result<void>::Failure(
                    foundation::Error::Create("integration.trigger_snapshot_duplicate", "pending scheduled trigger snapshot contains duplicate occurrence"));
            }
        }

        const auto required = RequiredHandlersFor(trigger.action);
        std::unordered_set<ScheduledTriggerHandlerId> completed;
        for (const auto handler_id : delivery.completed_handlers)
        {
            if (!handler_id.IsValid() || !ContainsHandler(required, handler_id) || !completed.insert(handler_id).second)
            {
                return foundation::Result<void>::Failure(
                    foundation::Error::Create("integration.trigger_snapshot_handler_invalid", "pending trigger snapshot contains invalid completed handler"));
            }
        }
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> ScheduledTriggerDispatcher::RestoreSnapshot(ScheduledTriggerDispatcherSnapshot snapshot)
{
    const auto validation = ValidateSnapshot(snapshot);
    if (!validation)
    {
        return validation;
    }
    pending_ = std::move(snapshot.pending);
    return foundation::Result<void>::Success();
}

foundation::Result<EventTypeId> TimeFactsAdapter::RegisterContracts()
{
    if (scheduled_due_event_type_.IsValid())
    {
        return foundation::Result<EventTypeId>::Success(scheduled_due_event_type_);
    }

    const auto registered = facts_.RegisterEventType<time::ScheduledTrigger>(
        "framework.schedule.due",
        GameplayDomainId::FromString("framework.time"),
        facts::HistoryPolicy::None);
    if (registered)
    {
        scheduled_due_event_type_ = registered.Value();
    }
    return registered;
}

foundation::Result<void> TimeFactsAdapter::RegisterWithDispatcher(
    ScheduledTriggerDispatcher& dispatcher,
    int priority)
{
    if (dispatcher_registered_)
    {
        return foundation::Result<void>::Success();
    }
    if (!scheduled_due_event_type_.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.time_facts_unregistered", "time/facts contracts must be registered before dispatcher binding"));
    }

    const auto registered = dispatcher.RegisterObserver(
        ScheduledTriggerHandlerId::FromString("framework.integration.time_facts"),
        priority,
        [this](const time::ScheduledTrigger& trigger, const GameplayContext& context) {
            return PublishTrigger(trigger, context);
        });
    if (registered)
    {
        dispatcher_registered_ = true;
    }
    return registered;
}

foundation::Result<ScheduledTriggerDisposition> TimeFactsAdapter::PublishTrigger(
    const time::ScheduledTrigger& trigger,
    const GameplayContext& context)
{
    if (!scheduled_due_event_type_.IsValid())
    {
        return foundation::Result<ScheduledTriggerDisposition>::Failure(
            foundation::Error::Create("integration.time_facts_unregistered", "time/facts contracts must be registered before use"));
    }
    if (!trigger.schedule.IsValid() || !trigger.action.IsValid() || !trigger.owner.IsValid())
    {
        return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::DiscardTerminal);
    }

    auto event_context = context;
    event_context.time = trigger.observed_at;
    const auto delivery_operation = DeliveryOperationId(trigger);
    if (event_context.operation.IsValid() && event_context.operation != delivery_operation)
    {
        event_context.parent_operation = event_context.operation;
    }
    event_context.operation = delivery_operation;

    const auto published = facts_.Publish<time::ScheduledTrigger>(
        scheduled_due_event_type_,
        event_context,
        trigger.owner,
        trigger,
        ProducerId::FromString("framework.time"));
    if (!published)
    {
        return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::Retry);
    }
    return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::Ack);
}

foundation::Result<std::uint64_t> TimeFactsAdapter::ExpireTimedFacts(GameplayContext context)
{
    return facts_.ExpireDueFacts(context.time, context);
}
} // namespace epidemic::gameplay::integration
