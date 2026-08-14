#include "Epidemic/GameFramework/Facts/gameplay_facts.h"

#include <iterator>
#include <limits>
#include <map>
#include <tuple>

namespace epidemic::gameplay::facts
{
GameplayFactsService::GameplayFactsService()
{
    const auto registered = RegisterEventType<FactChange>(
        kFactChangedEventName,
        GameplayDomainId::FromString(kFactsDomainName),
        HistoryPolicy::None);
    if (registered)
    {
        fact_changed_event_type_ = registered.Value();
    }
}

void GameplayFactsService::Freeze()
{
    for (auto& [_, list] : subscribers_)
    {
        std::sort(list.begin(), list.end(), [](const Subscriber& left, const Subscriber& right) {
            if (left.priority != right.priority)
            {
                return left.priority < right.priority;
            }
            return left.id.Raw() < right.id.Raw();
        });
    }
    frozen_ = true;
}

foundation::Result<void> GameplayFactsService::ValidateEventPayload(EventTypeId type, std::type_index payload_type) const
{
    const auto found = event_types_.find(type);
    if (found == event_types_.end())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.event_unknown", "event type is not registered"));
    }
    if (found->second.payload_type != payload_type)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.event_payload_mismatch", "event payload C++ type does not match registration"));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> GameplayFactsService::SubmitBatch(GameplayEventBatch batch)
{
    if (!batch.Producer().IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.event_batch_invalid", "worker event batch requires a stable producer id"));
    }

    for (const auto& pending : batch.pending_)
    {
        const auto validation = ValidateEventPayload(pending.type, pending.payload_type);
        if (!validation)
        {
            ++rejected_events_;
            return validation;
        }
    }

    const auto event_count = batch.pending_.size();
    {
        std::scoped_lock lock(batch_mutex_);
        submitted_batches_.push_back(std::move(batch));
    }
    published_events_.fetch_add(event_count, std::memory_order_relaxed);
    return foundation::Result<void>::Success();
}

void GameplayFactsService::MergeSubmittedBatches()
{
    std::vector<GameplayEventBatch> batches;
    {
        std::scoped_lock lock(batch_mutex_);
        batches.swap(submitted_batches_);
    }
    if (batches.empty())
    {
        return;
    }

    struct FlattenedEvent
    {
        PendingEvent event;
        std::uint64_t batch_order = 0;
        std::uint64_t local_sequence = 0;
    };

    std::vector<FlattenedEvent> flattened;
    for (auto& batch : batches)
    {
        for (auto& pending : batch.pending_)
        {
            flattened.push_back(FlattenedEvent{PendingEvent{pending.type,
                                                             std::move(pending.context),
                                                             pending.subject,
                                                             std::move(pending.payload),
                                                             pending.payload_type,
                                                             pending.producer,
                                                             pending.local_sequence},
                                                   batch.BatchOrder(),
                                                   pending.local_sequence});
        }
    }

    std::sort(flattened.begin(), flattened.end(), [](const FlattenedEvent& left, const FlattenedEvent& right) {
        return std::tie(left.event.context.tick.value, left.event.producer.value, left.batch_order, left.local_sequence) <
               std::tie(right.event.context.tick.value, right.event.producer.value, right.batch_order, right.local_sequence);
    });
    for (auto& item : flattened)
    {
        pending_events_.push_back(std::move(item.event));
    }
}

foundation::Result<std::uint64_t> GameplayFactsService::Dispatch(EventDispatchLimits limits)
{
    if (dispatching_)
    {
        return foundation::Result<std::uint64_t>::Failure(
            foundation::Error::Create("gameplay.event_dispatch_reentrant", "event dispatch cannot be entered recursively"));
    }
    if (limits.max_events == 0 || limits.max_waves == 0)
    {
        return foundation::Result<std::uint64_t>::Failure(
            foundation::Error::Create("gameplay.event_dispatch_invalid_limits", "dispatch limits must be non-zero"));
    }

    struct DispatchGuard
    {
        bool& value;
        explicit DispatchGuard(bool& target) : value(target) { value = true; }
        ~DispatchGuard() { value = false; }
    } guard(dispatching_);

    std::uint64_t processed = 0;
    std::uint32_t wave = 0;

    while (wave < limits.max_waves)
    {
        MergeSubmittedBatches();
        if (pending_events_.empty())
        {
            break;
        }

        ++wave;
        ++dispatch_waves_;
        std::vector<PendingEvent> current;
        current.swap(pending_events_);

        std::size_t index = 0;
        for (; index < current.size(); ++index)
        {
            if (processed >= limits.max_events)
            {
                pending_events_.insert(pending_events_.begin(),
                                       std::make_move_iterator(current.begin() + static_cast<std::ptrdiff_t>(index)),
                                       std::make_move_iterator(current.end()));
                return foundation::Result<std::uint64_t>::Failure(
                    foundation::Error::Create("gameplay.event_budget_exceeded", "event dispatch exceeded max events budget"));
            }

            auto& pending = current[index];
            EventRecord record;
            record.envelope.id = event_ids_.Next();
            record.envelope.type = pending.type;
            record.envelope.tick = pending.context.tick;
            record.envelope.time = pending.context.time;
            record.envelope.context = pending.context;
            record.envelope.subject = pending.subject;
            record.envelope.producer = pending.producer;
            record.envelope.sequence = next_event_sequence_++;
            record.payload = std::move(pending.payload);
            record.payload_type = pending.payload_type;

            AppendHistory(record);

            const auto found = subscribers_.find(record.envelope.type);
            if (found != subscribers_.end())
            {
                for (const auto& subscriber : found->second)
                {
                    subscriber.callback(record);
                }
            }
            ++processed;
            ++dispatched_events_;
        }
    }

    MergeSubmittedBatches();
    if (!pending_events_.empty())
    {
        return foundation::Result<std::uint64_t>::Failure(
            foundation::Error::Create("gameplay.event_wave_budget_exceeded", "event dispatch exceeded max wave budget"));
    }

    return foundation::Result<std::uint64_t>::Success(processed);
}

void GameplayFactsService::AppendHistory(const EventRecord& event)
{
    const auto type = event_types_.find(event.envelope.type);
    if (type == event_types_.end() || type->second.history_policy == HistoryPolicy::None)
    {
        return;
    }

    history_.push_back(event);
    if (type->second.history_policy == HistoryPolicy::Recent && type->second.recent_limit > 0)
    {
        std::size_t count = 0;
        for (auto iterator = history_.rbegin(); iterator != history_.rend(); ++iterator)
        {
            if (iterator->envelope.type == event.envelope.type && ++count > type->second.recent_limit)
            {
                const auto erase_index = static_cast<std::size_t>(std::distance(iterator, history_.rend()) - 1);
                history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(erase_index));
                break;
            }
        }
    }
}

foundation::Result<void> GameplayFactsService::ValidateMutation(
    const FactTransaction& transaction,
    const FactTransaction::Mutation& mutation) const
{
    const auto type = fact_types_.find(mutation.key.type);
    if (type == fact_types_.end())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.fact_unknown", "fact type is not registered"));
    }
    if (type->second.owner != transaction.owner_)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.fact_owner_violation", "only the registered fact owner may modify this fact type"));
    }
    if (!mutation.key.subject.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.fact_invalid_subject", "fact subject must be a valid gameplay object reference"));
    }
    if (mutation.kind == FactTransaction::MutationKind::Set && type->second.value_type != mutation.value_type)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.fact_value_mismatch", "fact value C++ type does not match registration"));
    }
    if (mutation.kind == FactTransaction::MutationKind::Set && mutation.persistence == FactPersistence::Timed && !mutation.expires_at.has_value())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.fact_missing_expiration", "timed fact requires expiration time"));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<std::vector<FactChange>> GameplayFactsService::Commit(
    FactTransaction transaction,
    GameplayContext context,
    EventId source_event)
{
    if (!transaction.owner_.IsValid())
    {
        return foundation::Result<std::vector<FactChange>>::Failure(
            foundation::Error::Create("gameplay.fact_invalid_owner", "fact transaction owner must be valid"));
    }
    std::unordered_map<FactKey, bool, FactKeyHash> touched_keys;
    for (const auto& mutation : transaction.mutations_)
    {
        const auto validation = ValidateMutation(transaction, mutation);
        if (!validation)
        {
            return foundation::Result<std::vector<FactChange>>::Failure(validation.GetError());
        }
        if (!touched_keys.emplace(mutation.key, true).second)
        {
            return foundation::Result<std::vector<FactChange>>::Failure(
                foundation::Error::Create("gameplay.fact_duplicate_mutation", "a fact transaction may mutate each key at most once"));
        }
    }
    if (transaction.mutations_.empty())
    {
        return foundation::Result<std::vector<FactChange>>::Success({});
    }

    if (fact_revision_.value == std::numeric_limits<std::uint64_t>::max())
    {
        return foundation::Result<std::vector<FactChange>>::Failure(
            foundation::Error::Create("gameplay.fact_revision_exhausted", "fact revision counter is exhausted"));
    }
    ++fact_revision_.value;
    std::vector<FactChange> changes;
    changes.reserve(transaction.mutations_.size());

    for (auto& mutation : transaction.mutations_)
    {
        const auto existing = facts_.find(mutation.key);
        if (mutation.kind == FactTransaction::MutationKind::Remove)
        {
            if (existing != facts_.end())
            {
                changes.push_back(FactChange{FactChangeKind::Removed, mutation.key, existing->second, std::nullopt});
                facts_.erase(existing);
            }
            continue;
        }

        if (existing == facts_.end())
        {
            FactRecord record;
            record.id = fact_ids_.Next();
            record.key = mutation.key;
            record.owner = transaction.owner_;
            record.created_at = context.time;
            record.updated_at = context.time;
            record.revision = fact_revision_;
            record.source_operation = context.operation;
            record.source_event = source_event;
            record.persistence = mutation.persistence;
            record.expires_at = mutation.expires_at;
            record.value = std::move(mutation.value);
            record.value_type = mutation.value_type;
            facts_.emplace(record.key, record);
            changes.push_back(FactChange{FactChangeKind::Created, record.key, std::nullopt, record});
        }
        else
        {
            const FactRecord before = existing->second;
            existing->second.updated_at = context.time;
            existing->second.revision = fact_revision_;
            existing->second.source_operation = context.operation;
            existing->second.source_event = source_event;
            existing->second.persistence = mutation.persistence;
            existing->second.expires_at = mutation.expires_at;
            existing->second.value = std::move(mutation.value);
            existing->second.value_type = mutation.value_type;
            changes.push_back(FactChange{FactChangeKind::Updated, mutation.key, before, existing->second});
        }
    }

    PublishFactChanges(changes, context, source_event);
    return foundation::Result<std::vector<FactChange>>::Success(std::move(changes));
}

void GameplayFactsService::PublishFactChanges(const std::vector<FactChange>& changes, const GameplayContext& context, EventId source_event)
{
    for (const auto& change : changes)
    {
        auto event_context = context;
        if (!event_context.correlation.IsValid() && source_event.IsValid())
        {
            event_context.correlation = CorrelationId::FromRaw(source_event.High(), source_event.Low());
        }
        [[maybe_unused]] const auto result = Publish<FactChange>(fact_changed_event_type_, event_context, change.key.subject, change);
    }
}

const FactRecord* GameplayFactsService::FindFact(const FactKey& key) const noexcept
{
    const auto found = facts_.find(key);
    return found == facts_.end() ? nullptr : &found->second;
}

std::vector<FactRecord> GameplayFactsService::FindFacts(FactTypeId type, GameplayObjectRef subject, GameplayObjectRef scope) const
{
    std::vector<FactRecord> result;
    for (const auto& [key, record] : facts_)
    {
        if (key.type != type)
        {
            continue;
        }
        if (subject.IsValid() && key.subject != subject)
        {
            continue;
        }
        if (scope.IsValid() && key.scope != scope)
        {
            continue;
        }
        result.push_back(record);
    }
    std::sort(result.begin(), result.end(), [](const FactRecord& left, const FactRecord& right) {
        if (left.key.subject != right.key.subject)
        {
            return left.key.subject < right.key.subject;
        }
        return left.key.scope < right.key.scope;
    });
    return result;
}

foundation::Result<std::uint64_t> GameplayFactsService::ExpireDueFacts(GameplayTimePoint now, GameplayContext context)
{
    std::map<std::uint64_t, FactTransaction> transactions;
    for (const auto& [key, record] : facts_)
    {
        if (record.persistence == FactPersistence::Timed && record.expires_at.has_value() && *record.expires_at <= now)
        {
            auto [iterator, inserted] = transactions.try_emplace(record.owner.Raw(), record.owner);
            (void)inserted;
            iterator->second.Remove(key);
        }
    }

    std::uint64_t expired = 0;
    for (auto& [_, transaction] : transactions)
    {
        const auto result = Commit(std::move(transaction), context);
        if (!result)
        {
            return foundation::Result<std::uint64_t>::Failure(result.GetError());
        }
        expired += result.Value().size();
    }
    return foundation::Result<std::uint64_t>::Success(expired);
}

std::vector<EventRecord> GameplayFactsService::FindHistory(EventTypeId type, GameplayObjectRef subject) const
{
    std::vector<EventRecord> result;
    for (const auto& event : history_)
    {
        if (event.envelope.type == type && (!subject.IsValid() || event.envelope.subject == subject))
        {
            result.push_back(event);
        }
    }
    return result;
}

foundation::Result<void> GameplayFactsService::CompactHistory(EventTypeId type, const IHistoryCompactor& compactor)
{
    const auto info = event_types_.find(type);
    if (info == event_types_.end())
    {
        return foundation::Result<void>::Failure(foundation::Error::Create("gameplay.event_unknown", "event type is not registered"));
    }
    if (info->second.history_policy != HistoryPolicy::PersistentCompactable)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.history_not_compactable", "event type does not allow history compaction"));
    }

    std::vector<EventRecord> selected;
    std::vector<EventRecord> retained;
    for (auto& event : history_)
    {
        if (event.envelope.type == type)
        {
            selected.push_back(event);
        }
        else
        {
            retained.push_back(event);
        }
    }

    auto compacted = compactor.Compact(selected);
    retained.insert(retained.end(), compacted.begin(), compacted.end());
    std::sort(retained.begin(), retained.end(), [](const EventRecord& left, const EventRecord& right) {
        return std::tie(left.envelope.tick.value, left.envelope.sequence) < std::tie(right.envelope.tick.value, right.envelope.sequence);
    });
    history_ = std::move(retained);
    return foundation::Result<void>::Success();
}

FactsSnapshot GameplayFactsService::CaptureSnapshot() const
{
    FactsSnapshot snapshot;
    snapshot.facts.reserve(facts_.size());
    for (const auto& [_, record] : facts_)
    {
        if (record.persistence == FactPersistence::Persistent || record.persistence == FactPersistence::Timed)
        {
            snapshot.facts.push_back(record);
        }
    }
    std::sort(snapshot.facts.begin(), snapshot.facts.end(), [](const FactRecord& left, const FactRecord& right) {
        return left.id < right.id;
    });

    for (const auto& event : history_)
    {
        const auto info = event_types_.find(event.envelope.type);
        if (info != event_types_.end() &&
            (info->second.history_policy == HistoryPolicy::Persistent || info->second.history_policy == HistoryPolicy::PersistentCompactable))
        {
            snapshot.history.push_back(event);
        }
    }
    snapshot.fact_ids = fact_ids_.GetSnapshot();
    snapshot.event_ids = event_ids_.GetSnapshot();
    snapshot.fact_revision = fact_revision_;
    snapshot.next_event_sequence = next_event_sequence_;
    return snapshot;
}

foundation::Result<void> GameplayFactsService::RestoreSnapshot(FactsSnapshot snapshot)
{
    if (dispatching_)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.restore_during_dispatch", "facts snapshot cannot be restored while dispatching events"));
    }

    std::unordered_map<FactKey, FactRecord, FactKeyHash> restored;
    for (auto& fact : snapshot.facts)
    {
        const auto type = fact_types_.find(fact.key.type);
        if (type == fact_types_.end() || type->second.owner != fact.owner || type->second.value_type != fact.value_type)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.fact_snapshot_invalid", "facts snapshot is incompatible with registered fact types"));
        }
        restored.emplace(fact.key, std::move(fact));
    }
    for (const auto& event : snapshot.history)
    {
        const auto validation = ValidateEventPayload(event.envelope.type, event.payload_type);
        if (!validation)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.history_snapshot_invalid", "history snapshot is incompatible with registered event types"));
        }
    }

    facts_ = std::move(restored);
    history_ = std::move(snapshot.history);
    fact_ids_.Restore(snapshot.fact_ids);
    event_ids_.Restore(snapshot.event_ids);
    fact_revision_ = snapshot.fact_revision;
    next_event_sequence_ = snapshot.next_event_sequence == 0 ? 1 : snapshot.next_event_sequence;
    pending_events_.clear();
    {
        std::scoped_lock lock(batch_mutex_);
        submitted_batches_.clear();
    }
    return foundation::Result<void>::Success();
}

FactsDiagnostics GameplayFactsService::GetDiagnostics() const noexcept
{
    std::uint64_t persistent = 0;
    for (const auto& [_, fact] : facts_)
    {
        if (fact.persistence == FactPersistence::Persistent || fact.persistence == FactPersistence::Timed)
        {
            ++persistent;
        }
    }
    return FactsDiagnostics{published_events_.load(std::memory_order_relaxed),
                            dispatched_events_,
                            dispatch_waves_,
                            rejected_events_.load(std::memory_order_relaxed),
                            facts_.size(),
                            persistent,
                            history_.size()};
}
} // namespace epidemic::gameplay::facts
