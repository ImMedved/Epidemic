#include "Epidemic/GameFramework/Conditions/conditions.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <limits>

namespace epidemic::gameplay::conditions
{
namespace
{
[[nodiscard]] foundation::Error Error(std::string code, std::string message)
{
    return foundation::Error::Create(std::move(code), std::move(message));
}
} // namespace

ConditionService::ConditionService()
    : ids_(GameplayObjectId::FromString("framework.conditions.instances").High())
{
}

foundation::Result<ConditionTypeId> ConditionService::RegisterCondition(
    ConditionDefinition definition,
    PayloadValidator validator)
{
    if (frozen_)
    {
        return foundation::Result<ConditionTypeId>::Failure(Error("gameplay.registry_frozen", "condition registry is frozen"));
    }
    if (definition.canonical_name.empty())
    {
        return foundation::Result<ConditionTypeId>::Failure(Error("gameplay.condition_invalid", "condition canonical name must not be empty"));
    }
    const auto expected = ConditionTypeId::FromString(definition.canonical_name);
    if (!definition.id.IsValid())
    {
        definition.id = expected;
    }
    if (definition.id != expected)
    {
        return foundation::Result<ConditionTypeId>::Failure(Error("gameplay.condition_id_mismatch", "condition id does not match canonical name"));
    }
    if (definitions_.contains(definition.id))
    {
        return foundation::Result<ConditionTypeId>::Failure(Error("gameplay.already_registered", "condition type is already registered"));
    }
    if (definition.stacking == ConditionStackingPolicy::AddStacks && definition.max_stacks == 0)
    {
        return foundation::Result<ConditionTypeId>::Failure(Error("gameplay.condition_invalid_stacks", "stacking condition must allow at least one stack"));
    }
    if (definition.default_duration.ticks < 0 || definition.periodic_interval.ticks < 0)
    {
        return foundation::Result<ConditionTypeId>::Failure(Error("gameplay.condition_invalid_duration", "condition durations must be non-negative"));
    }
    if ((definition.default_duration.ticks > 0 || definition.periodic_interval.ticks > 0) && !definition.clock.IsValid())
    {
        return foundation::Result<ConditionTypeId>::Failure(Error("gameplay.condition_invalid_clock", "finite or periodic condition requires a valid clock"));
    }
    if (definition.payload_type.IsValid() && (definition.payload_schema_version == 0 || definition.max_payload_bytes == 0))
    {
        return foundation::Result<ConditionTypeId>::Failure(Error("gameplay.condition_invalid_payload", "typed condition payload requires non-zero schema version and max payload bytes"));
    }

    const auto id = definition.id;
    definitions_.emplace(id, DefinitionEntry{std::move(definition), std::move(validator)});
    return foundation::Result<ConditionTypeId>::Success(id);
}

const ConditionDefinition* ConditionService::FindDefinition(ConditionTypeId id) const noexcept
{
    const auto found = definitions_.find(id);
    return found == definitions_.end() ? nullptr : &found->second.definition;
}

foundation::Result<void> ConditionService::ValidatePayload(
    const DefinitionEntry& definition,
    const RegisteredConditionPayload& payload) const
{
    const auto& def = definition.definition;
    if (!def.payload_type.IsValid())
    {
        if (payload.type.IsValid() || !payload.bytes.empty())
        {
            return foundation::Result<void>::Failure(Error("gameplay.condition_payload_unexpected", "condition type does not accept payload"));
        }
        return foundation::Result<void>::Success();
    }
    if (payload.type != def.payload_type || payload.schema_version != def.payload_schema_version || payload.bytes.size() > def.max_payload_bytes)
    {
        return foundation::Result<void>::Failure(Error("gameplay.condition_payload_invalid", "condition payload type or size is invalid"));
    }
    if (definition.validator && !definition.validator(payload.bytes))
    {
        return foundation::Result<void>::Failure(Error("gameplay.condition_payload_rejected", "condition payload validator rejected payload"));
    }
    return foundation::Result<void>::Success();
}

std::optional<GameplayTimePoint> ConditionService::ComputeExpiration(GameplayTimePoint now, GameplayDuration duration) const noexcept
{
    if (duration.ticks <= 0)
    {
        return std::nullopt;
    }
    return ::epidemic::gameplay::CheckedAdd(now, duration);
}

std::vector<ConditionInstanceId> ConditionService::MatchingInstances(
    const ApplyConditionRequest& request,
    const ConditionDefinition& definition) const
{
    std::vector<ConditionInstanceId> result;
    const auto subject_found = subject_index_.find(request.subject);
    if (subject_found == subject_index_.end())
    {
        return result;
    }
    for (const auto id : subject_found->second)
    {
        const auto* instance = Find(id);
        if (instance == nullptr || instance->type != definition.id)
        {
            continue;
        }
        if (definition.stacking == ConditionStackingPolicy::UniquePerSource && instance->source != request.source)
        {
            continue;
        }
        result.push_back(id);
    }
    std::sort(result.begin(), result.end());
    return result;
}

void ConditionService::IndexInstance(const ConditionInstance& instance)
{
    auto& ids = subject_index_[instance.subject];
    const auto pos = std::lower_bound(ids.begin(), ids.end(), instance.id);
    ids.insert(pos, instance.id);
}

void ConditionService::UnindexInstance(const ConditionInstance& instance)
{
    const auto found = subject_index_.find(instance.subject);
    if (found == subject_index_.end())
    {
        return;
    }
    auto& ids = found->second;
    const auto pos = std::lower_bound(ids.begin(), ids.end(), instance.id);
    if (pos != ids.end() && *pos == instance.id)
    {
        ids.erase(pos);
    }
    if (ids.empty())
    {
        subject_index_.erase(found);
    }
}

void ConditionService::BumpRevision(ConditionInstance& instance) noexcept
{
    ++revision_.value;
    instance.revision = revision_;
}

foundation::Result<ConditionInstanceId> ConditionService::AddNew(
    const ApplyConditionRequest& request,
    const ConditionDefinition& definition)
{
    const auto raw = ids_.Next();
    if (!raw.IsValid())
    {
        return foundation::Result<ConditionInstanceId>::Failure(Error("gameplay.condition_id_exhausted", "condition instance id generator is exhausted"));
    }
    const auto id = ConditionInstanceId{raw};
    const auto duration = request.duration.value_or(definition.default_duration);
    const auto expiration = ComputeExpiration(request.context.time, duration);
    if (duration.ticks > 0 && !expiration.has_value())
    {
        return foundation::Result<ConditionInstanceId>::Failure(Error("gameplay.time_overflow", "condition expiration overflows gameplay time"));
    }

    ConditionInstance instance;
    instance.id = id;
    instance.type = request.type;
    instance.subject = request.subject;
    instance.source = request.source;
    instance.instigator = request.instigator;
    instance.applied_at = request.context.time;
    instance.expires_at = expiration;
    instance.magnitude_micro = request.magnitude_micro;
    instance.stacks = 1;
    instance.payload = request.payload;
    BumpRevision(instance);

    const auto index = instances_.size();
    instances_.push_back(std::move(instance));
    id_to_index_.emplace(id, index);
    IndexInstance(instances_.back());
    ++applied_;
    ConditionChange added_change{0,
                                      ConditionChangeKind::Added,
                                      id,
                                      request.type,
                                      request.subject,
                                      ConditionRemovalReason::SystemCleanup,
                                      1,
                                      instances_.back().expiration_schedule,
                                      instances_.back().periodic_schedule,
                                      instances_.back().revision,
                                      request.context};
    added_change.source = request.source;
    added_change.instigator = request.instigator;
    added_change.magnitude_micro = request.magnitude_micro;
    added_change.stacks = 1;
    RecordChange(std::move(added_change));
    return foundation::Result<ConditionInstanceId>::Success(id);
}

foundation::Result<ApplyConditionResult> ConditionService::Apply(ApplyConditionRequest request)
{
    if (!frozen_)
    {
        return foundation::Result<ApplyConditionResult>::Failure(Error("gameplay.registry_not_frozen", "condition registry must be frozen before runtime mutation"));
    }
    const auto definition_found = definitions_.find(request.type);
    if (definition_found == definitions_.end() || !request.subject.IsValid())
    {
        return foundation::Result<ApplyConditionResult>::Failure(Error("gameplay.condition_request_invalid", "condition type and subject must be valid"));
    }
    if (request.duration.has_value() && request.duration->ticks < 0)
    {
        return foundation::Result<ApplyConditionResult>::Failure(Error("gameplay.condition_invalid_duration", "condition duration override must be non-negative"));
    }
    if (definition_found->second.definition.stacking == ConditionStackingPolicy::UniquePerSource && !request.source.IsValid())
    {
        return foundation::Result<ApplyConditionResult>::Failure(Error("gameplay.condition_source_required", "UniquePerSource condition requires a valid source"));
    }
    const auto effective_duration = request.duration.value_or(definition_found->second.definition.default_duration);
    if (effective_duration.ticks > 0 && !definition_found->second.definition.clock.IsValid())
    {
        return foundation::Result<ApplyConditionResult>::Failure(Error("gameplay.condition_invalid_clock", "finite condition application requires a valid clock"));
    }
    const auto payload_result = ValidatePayload(definition_found->second, request.payload);
    if (!payload_result)
    {
        return foundation::Result<ApplyConditionResult>::Failure(payload_result.GetError());
    }

    const auto& definition = definition_found->second.definition;
    auto matches = MatchingInstances(request, definition);
    if (definition.stacking == ConditionStackingPolicy::Independent || matches.empty())
    {
        const auto added = AddNew(request, definition);
        if (!added)
        {
            return foundation::Result<ApplyConditionResult>::Failure(added.GetError());
        }
        return foundation::Result<ApplyConditionResult>::Success(ApplyConditionResult{ConditionApplyDisposition::Added, added.Value(), std::nullopt});
    }

    const auto existing_id = matches.front();
    auto existing_found = id_to_index_.find(existing_id);
    if (existing_found == id_to_index_.end())
    {
        return foundation::Result<ApplyConditionResult>::Failure(Error("gameplay.condition_index_invalid", "condition index is inconsistent"));
    }
    auto& existing = instances_[existing_found->second];

    switch (definition.stacking)
    {
    case ConditionStackingPolicy::UniquePerSubject:
    case ConditionStackingPolicy::UniquePerSource:
        return foundation::Result<ApplyConditionResult>::Success(ApplyConditionResult{ConditionApplyDisposition::Rejected, existing.id, std::nullopt});

    case ConditionStackingPolicy::RefreshDuration:
    {
        const auto duration = request.duration.value_or(definition.default_duration);
        const auto expiration = ComputeExpiration(request.context.time, duration);
        if (duration.ticks > 0 && !expiration.has_value())
        {
            return foundation::Result<ApplyConditionResult>::Failure(Error("gameplay.time_overflow", "condition expiration overflows gameplay time"));
        }
        const auto old = existing;
        existing.applied_at = request.context.time;
        existing.expires_at = expiration;
        existing.magnitude_micro = request.magnitude_micro;
        existing.payload = std::move(request.payload);
        existing.source = request.source;
        existing.instigator = request.instigator;
        if (existing.applied_at == old.applied_at && existing.expires_at == old.expires_at && existing.magnitude_micro == old.magnitude_micro &&
            existing.payload.type == old.payload.type && existing.payload.bytes == old.payload.bytes && existing.source == old.source && existing.instigator == old.instigator)
        {
            return foundation::Result<ApplyConditionResult>::Success(ApplyConditionResult{ConditionApplyDisposition::NoOp, existing.id, std::nullopt});
        }
        BumpRevision(existing);
        RecordChange(ConditionChange{0, ConditionChangeKind::Refreshed, existing.id, existing.type, existing.subject,
                                     ConditionRemovalReason::SystemCleanup, 1, existing.expiration_schedule, existing.periodic_schedule, existing.revision, request.context});
        return foundation::Result<ApplyConditionResult>::Success(ApplyConditionResult{ConditionApplyDisposition::Refreshed, existing.id, std::nullopt});
    }

    case ConditionStackingPolicy::ExtendDuration:
    {
        const auto duration = request.duration.value_or(definition.default_duration);
        if (duration.ticks == 0)
        {
            return foundation::Result<ApplyConditionResult>::Success(ApplyConditionResult{ConditionApplyDisposition::NoOp, existing.id, std::nullopt});
        }
        if (duration.ticks > 0)
        {
            const auto base = existing.expires_at.value_or(request.context.time);
            const auto expiration = ::epidemic::gameplay::CheckedAdd(base, duration);
            if (!expiration.has_value())
            {
                return foundation::Result<ApplyConditionResult>::Failure(Error("gameplay.time_overflow", "condition extension overflows gameplay time"));
            }
            existing.expires_at = *expiration;
        }
        BumpRevision(existing);
        RecordChange(ConditionChange{0, ConditionChangeKind::DurationExtended, existing.id, existing.type, existing.subject,
                                     ConditionRemovalReason::SystemCleanup, 1, existing.expiration_schedule, existing.periodic_schedule, existing.revision, request.context});
        return foundation::Result<ApplyConditionResult>::Success(ApplyConditionResult{ConditionApplyDisposition::Refreshed, existing.id, std::nullopt});
    }

    case ConditionStackingPolicy::AddStacks:
    {
        if (existing.stacks >= definition.max_stacks)
        {
            return foundation::Result<ApplyConditionResult>::Success(ApplyConditionResult{ConditionApplyDisposition::NoOp, existing.id, std::nullopt});
        }
        ++existing.stacks;
        existing.magnitude_micro = std::max(existing.magnitude_micro, request.magnitude_micro);
        BumpRevision(existing);
        ++stack_merges_;
        RecordChange(ConditionChange{0, ConditionChangeKind::StackChanged, existing.id, existing.type, existing.subject,
                                     ConditionRemovalReason::SystemCleanup, 1, existing.expiration_schedule, existing.periodic_schedule, existing.revision, request.context});
        return foundation::Result<ApplyConditionResult>::Success(ApplyConditionResult{ConditionApplyDisposition::Stacked, existing.id, std::nullopt});
    }

    case ConditionStackingPolicy::ReplaceIfStronger:
        if (request.magnitude_micro <= existing.magnitude_micro)
        {
            return foundation::Result<ApplyConditionResult>::Success(ApplyConditionResult{ConditionApplyDisposition::NoOp, existing.id, std::nullopt});
        }
        [[fallthrough]];

    case ConditionStackingPolicy::ReplaceExisting:
    {
        // Fully preflight the replacement before touching the current authoritative instance.
        const auto duration = request.duration.value_or(definition.default_duration);
        const auto expiration = ComputeExpiration(request.context.time, duration);
        if (duration.ticks > 0 && !expiration.has_value())
        {
            return foundation::Result<ApplyConditionResult>::Failure(Error("gameplay.time_overflow", "condition replacement expiration overflows gameplay time"));
        }
        const auto next_revision = ::epidemic::gameplay::CheckedNext(revision_);
        if (!next_revision.has_value())
        {
            return foundation::Result<ApplyConditionResult>::Failure(Error("gameplay.revision_exhausted", "condition revision is exhausted"));
        }
        const auto raw = ids_.Next();
        if (!raw.IsValid())
        {
            return foundation::Result<ApplyConditionResult>::Failure(Error("gameplay.condition_id_exhausted", "condition instance id generator is exhausted"));
        }

        ConditionInstance replacement;
        replacement.id = ConditionInstanceId{raw};
        replacement.type = request.type;
        replacement.subject = request.subject;
        replacement.source = request.source;
        replacement.instigator = request.instigator;
        replacement.applied_at = request.context.time;
        replacement.expires_at = expiration;
        replacement.magnitude_micro = request.magnitude_micro;
        replacement.stacks = 1;
        replacement.payload = request.payload;
        replacement.revision = *next_revision;

        const auto replaced = existing.id;
        const auto old_index = existing_found->second;
        const auto removed = instances_[old_index];
        UnindexInstance(removed);
        id_to_index_.erase(removed.id);
        instances_[old_index] = std::move(replacement);
        id_to_index_[instances_[old_index].id] = old_index;
        IndexInstance(instances_[old_index]);
        revision_ = *next_revision;
        ++removed_;
        ++applied_;

        ConditionChange removed_change{0, ConditionChangeKind::Removed, removed.id, removed.type, removed.subject,
                                       ConditionRemovalReason::Replaced, 1, removed.expiration_schedule, removed.periodic_schedule,
                                       revision_, request.context};
        removed_change.source = removed.source;
        removed_change.instigator = removed.instigator;
        removed_change.magnitude_micro = removed.magnitude_micro;
        removed_change.stacks = removed.stacks;
        RecordChange(std::move(removed_change));

        const auto& current = instances_[old_index];
        ConditionChange added_change{0, ConditionChangeKind::Added, current.id, current.type, current.subject,
                                     ConditionRemovalReason::SystemCleanup, 1, current.expiration_schedule, current.periodic_schedule,
                                     revision_, request.context};
        added_change.source = current.source;
        added_change.instigator = current.instigator;
        added_change.magnitude_micro = current.magnitude_micro;
        added_change.stacks = current.stacks;
        RecordChange(std::move(added_change));
        return foundation::Result<ApplyConditionResult>::Success(ApplyConditionResult{ConditionApplyDisposition::Replaced, current.id, replaced});
    }

    case ConditionStackingPolicy::Independent:
        break;
    }

    return foundation::Result<ApplyConditionResult>::Failure(Error("gameplay.condition_stacking_unknown", "unhandled condition stacking policy"));
}

foundation::Result<void> ConditionService::RemoveAtIndex(
    std::size_t index,
    ConditionRemovalReason reason,
    GameplayContext context)
{
    if (index >= instances_.size())
    {
        return foundation::Result<void>::Failure(Error("gameplay.condition_unknown", "condition instance does not exist"));
    }

    const auto removed = instances_[index];
    UnindexInstance(removed);
    id_to_index_.erase(removed.id);

    const auto last = instances_.size() - 1;
    if (index != last)
    {
        instances_[index] = std::move(instances_[last]);
        id_to_index_[instances_[index].id] = index;
    }
    instances_.pop_back();
    ++revision_.value;
    ++removed_;
    if (reason == ConditionRemovalReason::Expired)
    {
        ++expired_;
    }
    ConditionChange change{0,
                           reason == ConditionRemovalReason::Expired ? ConditionChangeKind::Expired : ConditionChangeKind::Removed,
                           removed.id,
                           removed.type,
                           removed.subject,
                           reason,
                           1,
                           removed.expiration_schedule,
                           removed.periodic_schedule,
                           revision_,
                           context};
    change.source = removed.source;
    change.instigator = removed.instigator;
    change.magnitude_micro = removed.magnitude_micro;
    change.stacks = removed.stacks;
    RecordChange(std::move(change));
    return foundation::Result<void>::Success();
}

foundation::Result<void> ConditionService::Remove(
    ConditionInstanceId instance,
    ConditionRemovalReason reason,
    GameplayContext context)
{
    const auto found = id_to_index_.find(instance);
    if (found == id_to_index_.end())
    {
        return foundation::Result<void>::Failure(Error("gameplay.condition_unknown", "condition instance does not exist"));
    }
    return RemoveAtIndex(found->second, reason, context);
}

std::uint64_t ConditionService::RemoveByType(
    GameplayObjectRef subject,
    ConditionTypeId type,
    ConditionRemovalReason reason,
    GameplayContext context)
{
    auto instances = GetConditions(subject);
    std::uint64_t count = 0;
    for (const auto& instance : instances)
    {
        if (instance.type == type && Remove(instance.id, reason, context))
        {
            ++count;
        }
    }
    return count;
}

std::uint64_t ConditionService::RemoveBySource(
    GameplayObjectRef subject,
    GameplayObjectRef source,
    ConditionRemovalReason reason,
    GameplayContext context)
{
    auto instances = GetConditions(subject);
    std::uint64_t count = 0;
    for (const auto& instance : instances)
    {
        if (instance.source == source && Remove(instance.id, reason, context))
        {
            ++count;
        }
    }
    return count;
}

std::uint64_t ConditionService::RemoveByTag(
    GameplayObjectRef subject,
    TagId tag,
    const GameplayTagRegistry& tags,
    ConditionRemovalReason reason,
    GameplayContext context)
{
    auto instances = GetConditions(subject);
    std::uint64_t count = 0;
    for (const auto& instance : instances)
    {
        const auto* definition = FindDefinition(instance.type);
        if (definition != nullptr && definition->tags.HasMatching(tag, tags) && Remove(instance.id, reason, context))
        {
            ++count;
        }
    }
    return count;
}

std::uint64_t ConditionService::RemoveSubject(
    GameplayObjectRef subject,
    ConditionRemovalReason reason,
    GameplayContext context)
{
    const auto instances = GetConditions(subject);
    std::uint64_t count = 0;
    for (const auto& instance : instances)
    {
        if (Remove(instance.id, reason, context))
        {
            ++count;
        }
    }
    return count;
}

foundation::Result<void> ConditionService::SetScheduleLinks(
    ConditionInstanceId instance,
    std::optional<ScheduleId> expiration,
    std::optional<ScheduleId> periodic,
    GameplayContext context)
{
    const auto found = id_to_index_.find(instance);
    if (found == id_to_index_.end())
    {
        return foundation::Result<void>::Failure(Error("gameplay.condition_unknown", "condition instance does not exist"));
    }
    auto& value = instances_[found->second];
    if (value.expiration_schedule == expiration && value.periodic_schedule == periodic)
    {
        return foundation::Result<void>::Success();
    }
    value.expiration_schedule = expiration;
    value.periodic_schedule = periodic;
    BumpRevision(value);
    RecordChange(ConditionChange{0, ConditionChangeKind::ScheduleLinksChanged, value.id, value.type, value.subject,
                                 ConditionRemovalReason::SystemCleanup, 1, value.expiration_schedule, value.periodic_schedule, value.revision, context});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ConditionService::HandleExpirationDue(
    ConditionInstanceId instance,
    GameplayTimePoint observed_at,
    GameplayContext context)
{
    const auto* value = Find(instance);
    if (value == nullptr)
    {
        return foundation::Result<void>::Failure(Error("gameplay.condition_unknown", "condition instance does not exist"));
    }
    if (value->paused_for_materialization)
    {
        return foundation::Result<void>::Success();
    }
    if (!value->expires_at.has_value() || *value->expires_at > observed_at)
    {
        return foundation::Result<void>::Failure(Error("gameplay.condition_not_due", "condition is not due for expiration"));
    }
    return Remove(instance, ConditionRemovalReason::Expired, context);
}

foundation::Result<void> ConditionService::HandlePeriodicDue(
    ConditionInstanceId instance,
    std::uint64_t occurrence_count,
    GameplayContext context)
{
    const auto* value = Find(instance);
    if (value == nullptr)
    {
        return foundation::Result<void>::Failure(Error("gameplay.condition_unknown", "condition instance does not exist"));
    }
    const auto* definition = FindDefinition(value->type);
    if (definition == nullptr || definition->periodic_interval.ticks <= 0 || occurrence_count == 0)
    {
        return foundation::Result<void>::Failure(Error("gameplay.condition_periodic_invalid", "condition is not due for periodic processing"));
    }
    if (value->paused_for_materialization)
    {
        return foundation::Result<void>::Success();
    }
    const auto available = std::numeric_limits<std::uint64_t>::max() - periodic_triggers_;
    periodic_triggers_ += std::min(available, occurrence_count);
    RecordChange(ConditionChange{0, ConditionChangeKind::PeriodicDue, value->id, value->type, value->subject,
                                 ConditionRemovalReason::SystemCleanup, occurrence_count, value->expiration_schedule, value->periodic_schedule, value->revision, context});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ConditionService::NotifySubjectMaterialization(
    GameplayObjectRef subject,
    bool materialized,
    GameplayContext context)
{
    auto instances = GetConditions(subject);
    if (!materialized)
    {
        for (const auto& instance : instances)
        {
            const auto* definition = FindDefinition(instance.type);
            if (definition != nullptr && definition->materialization == ConditionMaterializationPolicy::MaterializedOnly &&
                definition->dematerialization == ConditionDematerializationPolicy::RejectDematerialization)
            {
                return foundation::Result<void>::Failure(Error("gameplay.condition_dematerialization_rejected", "active condition rejects subject dematerialization"));
            }
        }
    }

    for (const auto& snapshot : instances)
    {
        const auto* definition = FindDefinition(snapshot.type);
        if (definition == nullptr || definition->materialization != ConditionMaterializationPolicy::MaterializedOnly)
        {
            continue;
        }
        if (!materialized && definition->dematerialization == ConditionDematerializationPolicy::Remove)
        {
            [[maybe_unused]] const auto removed = Remove(snapshot.id, ConditionRemovalReason::SystemCleanup, context);
            continue;
        }
        const auto found = id_to_index_.find(snapshot.id);
        if (found == id_to_index_.end())
        {
            continue;
        }
        auto& current = instances_[found->second];
        const bool should_pause = !materialized && definition->dematerialization == ConditionDematerializationPolicy::Pause;
        if (current.paused_for_materialization != should_pause)
        {
            if (should_pause)
            {
                current.paused_for_materialization = true;
                current.materialization_paused_at = context.time;
            }
            else
            {
                if (current.materialization_paused_at.has_value() && context.time >= *current.materialization_paused_at)
                {
                    const GameplayDuration paused_for{context.time.ticks - current.materialization_paused_at->ticks};
                    std::optional<GameplayTimePoint> shifted_expiration;
                    if (current.expires_at.has_value())
                    {
                        shifted_expiration = ::epidemic::gameplay::CheckedAdd(*current.expires_at, paused_for);
                        if (!shifted_expiration.has_value())
                        {
                            return foundation::Result<void>::Failure(Error("gameplay.time_overflow", "condition resume expiration overflows gameplay time"));
                        }
                    }
                    const auto shifted_applied = ::epidemic::gameplay::CheckedAdd(current.applied_at, paused_for);
                    if (!shifted_applied.has_value())
                    {
                        return foundation::Result<void>::Failure(Error("gameplay.time_overflow", "condition resume phase overflows gameplay time"));
                    }
                    // Commit the temporal shift only after every affected timestamp has been
                    // validated, so an overflow cannot leave a half-resumed condition.
                    if (shifted_expiration.has_value()) current.expires_at = *shifted_expiration;
                    current.applied_at = *shifted_applied;
                }
                current.paused_for_materialization = false;
                current.materialization_paused_at.reset();
            }
            BumpRevision(current);
            ConditionChange pause_change{0, ConditionChangeKind::MaterializationPauseChanged, current.id, current.type,
                                         current.subject, ConditionRemovalReason::SystemCleanup, 1, current.expiration_schedule, current.periodic_schedule, current.revision, context};
            pause_change.source = current.source;
            pause_change.instigator = current.instigator;
            pause_change.magnitude_micro = current.magnitude_micro;
            pause_change.stacks = current.stacks;
            RecordChange(std::move(pause_change));
        }
    }
    return foundation::Result<void>::Success();
}

const ConditionInstance* ConditionService::Find(ConditionInstanceId id) const noexcept
{
    const auto found = id_to_index_.find(id);
    return found == id_to_index_.end() ? nullptr : &instances_[found->second];
}

std::optional<ConditionInstance> ConditionService::FindCopy(ConditionInstanceId id) const noexcept
{
    const auto* value = Find(id);
    return value == nullptr ? std::nullopt : std::optional<ConditionInstance>{*value};
}

std::vector<ConditionInstance> ConditionService::GetConditions(GameplayObjectRef subject) const
{
    std::vector<ConditionInstance> result;
    const auto found = subject_index_.find(subject);
    if (found == subject_index_.end())
    {
        return result;
    }
    result.reserve(found->second.size());
    for (const auto id : found->second)
    {
        if (const auto* instance = Find(id))
        {
            result.push_back(*instance);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
    return result;
}

std::vector<ConditionInstance> ConditionService::AllConditions() const
{
    auto result = instances_;
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
    return result;
}

bool ConditionService::HasCondition(GameplayObjectRef subject, ConditionTypeId type) const
{
    const auto conditions = GetConditions(subject);
    return std::any_of(conditions.begin(), conditions.end(), [type](const auto& instance) { return instance.type == type; });
}

bool ConditionService::HasConditionTag(GameplayObjectRef subject, TagId tag, const GameplayTagRegistry& tags) const
{
    const auto conditions = GetConditions(subject);
    for (const auto& instance : conditions)
    {
        const auto* definition = FindDefinition(instance.type);
        if (definition != nullptr && definition->tags.HasMatching(tag, tags))
        {
            return true;
        }
    }
    return false;
}

std::vector<ConditionInstance> ConditionService::FindByType(ConditionTypeId type) const
{
    std::vector<ConditionInstance> result;
    for (const auto& instance : instances_)
    {
        if (instance.type == type)
        {
            result.push_back(instance);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
    return result;
}

std::vector<ConditionInstance> ConditionService::FindBySource(GameplayObjectRef source) const
{
    std::vector<ConditionInstance> result;
    for (const auto& instance : instances_)
    {
        if (instance.source == source)
        {
            result.push_back(instance);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
    return result;
}

void ConditionService::RecordChange(ConditionChange change)
{
    change.sequence = next_change_sequence_++;
    changes_.push_back(std::move(change));
}

std::vector<ConditionChange> ConditionService::ChangesSince(std::uint64_t sequence) const
{
    const auto found = std::upper_bound(changes_.begin(), changes_.end(), sequence, [](std::uint64_t value, const ConditionChange& change) {
        return value < change.sequence;
    });
    return std::vector<ConditionChange>(found, changes_.end());
}

void ConditionService::PruneChangesBefore(std::uint64_t sequence)
{
    const auto found = std::lower_bound(changes_.begin(), changes_.end(), sequence, [](const ConditionChange& change, std::uint64_t value) {
        return change.sequence < value;
    });
    changes_.erase(changes_.begin(), found);
}

ConditionsSnapshot ConditionService::CaptureSnapshot() const
{
    ConditionsSnapshot snapshot;
    snapshot.id_generator = ids_.GetSnapshot();
    snapshot.revision = revision_;
    for (const auto& instance : instances_)
    {
        const auto* definition = FindDefinition(instance.type);
        if (definition != nullptr && definition->persistence == ConditionPersistencePolicy::Persistent)
        {
            auto persistent = instance;
            persistent.expiration_schedule.reset();
            persistent.periodic_schedule.reset();
            snapshot.instances.push_back(std::move(persistent));
        }
    }
    std::sort(snapshot.instances.begin(), snapshot.instances.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
    return snapshot;
}

foundation::Result<void> ConditionService::RestoreSnapshot(ConditionsSnapshot snapshot)
{
    std::unordered_map<ConditionInstanceId, bool, ConditionInstanceIdHash> seen;
    for (auto& instance : snapshot.instances)
    {
        const auto definition_found = definitions_.find(instance.type);
        if (!instance.id.IsValid() || !instance.subject.IsValid() || definition_found == definitions_.end() || seen.contains(instance.id) ||
            definition_found->second.definition.persistence != ConditionPersistencePolicy::Persistent || instance.stacks == 0 ||
            instance.stacks > std::max<std::uint32_t>(1, definition_found->second.definition.max_stacks) ||
            instance.revision.value > snapshot.revision.value ||
            (instance.expires_at.has_value() && *instance.expires_at < instance.applied_at) ||
            (instance.paused_for_materialization && !instance.materialization_paused_at.has_value()))
        {
            return foundation::Result<void>::Failure(Error("gameplay.condition_snapshot_invalid", "condition snapshot contains invalid instance"));
        }
        instance.expiration_schedule.reset();
        instance.periodic_schedule.reset();
        const auto payload = ValidatePayload(definition_found->second, instance.payload);
        if (!payload)
        {
            return foundation::Result<void>::Failure(payload.GetError());
        }
        seen.emplace(instance.id, true);
    }

    instances_.clear();
    id_to_index_.clear();
    subject_index_.clear();
    changes_.clear();
    next_change_sequence_ = 1;
    ids_.Restore(snapshot.id_generator);
    revision_ = snapshot.revision;
    applied_ = expired_ = removed_ = periodic_triggers_ = stack_merges_ = 0;

    instances_.reserve(snapshot.instances.size());
    for (auto& instance : snapshot.instances)
    {
        const auto index = instances_.size();
        id_to_index_.emplace(instance.id, index);
        instances_.push_back(std::move(instance));
        IndexInstance(instances_.back());
    }
    return foundation::Result<void>::Success();
}

ConditionsDiagnostics ConditionService::GetDiagnostics() const noexcept
{
    ConditionsDiagnostics result;
    result.active_conditions = instances_.size();
    result.applied = applied_;
    result.expired = expired_;
    result.removed = removed_;
    result.periodic_triggers = periodic_triggers_;
    result.stack_merges = stack_merges_;
    for (const auto& instance : instances_)
    {
        if (instance.paused_for_materialization)
        {
            ++result.paused_for_materialization;
        }
    }
    return result;
}
} // namespace epidemic::gameplay::conditions


