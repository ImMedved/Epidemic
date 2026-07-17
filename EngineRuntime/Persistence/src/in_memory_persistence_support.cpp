#include "in_memory_persistence_support.h"

#include <algorithm>
#include <string_view>
#include <type_traits>
#include <utility>

namespace epidemic::runtime
{
namespace
{
[[nodiscard]] foundation::Result<void> PersistenceFailure(std::string_view code, std::string_view message)
{
    return foundation::Result<void>::Failure(foundation::Error::Create(code, message));
}

[[nodiscard]] bool IsValidLocation(const PersistenceLocation& location) noexcept
{
    return location.region_id.IsValid() || location.chunk_id.IsValid() || location.location_tag.IsValid();
}

[[nodiscard]] foundation::Result<void> ValidateObject(const PersistentObjectRecord& record)
{
    if (!record.persistent_id.IsValid())
    {
        return PersistenceFailure("persistence.invalid_id", "persistent object id must be valid");
    }
    if (!record.payload.IsValid())
    {
        return PersistenceFailure("persistence.invalid_payload", "persistent object payload must declare schema and bytes");
    }
    if (record.last_observed_game_time < record.created_game_time)
    {
        return PersistenceFailure("persistence.invalid_game_time", "persistent object observation time must not precede creation time");
    }
    return foundation::Result<void>::Success();
}

[[nodiscard]] foundation::Result<void> ValidateLazyRule(const LazyRuleRecord& record)
{
    if (!record.rule_id.IsValid())
    {
        return PersistenceFailure("persistence.invalid_lazy_rule_id", "lazy rule id must be valid");
    }
    if (!record.target_id.IsValid())
    {
        return PersistenceFailure("persistence.invalid_id", "lazy rule target id must be valid");
    }
    if (record.evaluate_after_game_time < record.created_game_time)
    {
        return PersistenceFailure("persistence.invalid_lazy_rule_time", "lazy rule evaluation time must not precede creation time");
    }
    return foundation::Result<void>::Success();
}

[[nodiscard]] foundation::Result<void> ValidateTombstone(const TombstoneRecord& tombstone)
{
    if (!tombstone.persistent_id.IsValid())
    {
        return PersistenceFailure("persistence.invalid_id", "tombstone object id must be valid");
    }
    return foundation::Result<void>::Success();
}

[[nodiscard]] foundation::Result<void> ValidateZoneOverride(const ZoneOverrideSnapshot& snapshot)
{
    if (!IsValidLocation(snapshot.location))
    {
        return PersistenceFailure("persistence.invalid_location", "zone override location must contain at least one valid component");
    }
    return foundation::Result<void>::Success();
}

} // namespace

size_t PersistenceLocationHash::operator()(const PersistenceLocation& location) const noexcept
{
    size_t seed = std::hash<RegionId>{}(location.region_id);
    seed ^= std::hash<ChunkId>{}(location.chunk_id) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<std::uint64_t>{}(location.location_tag.Raw()) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
}

InMemoryPersistenceStore::InMemoryPersistenceStore(PersistenceSnapshot snapshot)
{
    const auto valid = ValidatePersistenceSnapshot(snapshot);
    if (!valid)
    {
        return;
    }
    revision_ = snapshot.current_revision;
    for (PersistentObjectRecord record : snapshot.objects)
    {
        objects_[record.persistent_id] = std::move(record);
    }
    for (TombstoneRecord tombstone : snapshot.tombstones)
    {
        tombstones_[tombstone.persistent_id] = std::move(tombstone);
    }
    for (LazyRuleRecord rule : snapshot.lazy_rules)
    {
        lazy_rules_[rule.rule_id] = std::move(rule);
    }
    for (ZoneOverrideSnapshot zone : snapshot.zone_overrides)
    {
        zone_overrides_[zone.location] = std::move(zone);
    }
}

InMemoryPersistenceStore::InMemoryPersistenceStore(PersistenceSnapshot snapshot, std::shared_ptr<IPersistenceBackend> backend,
                                                   PersistenceDurability durability)
    : InMemoryPersistenceStore(std::move(snapshot))
{
    backend_ = std::move(backend);
    durability_ = durability;
}

InMemorySaveTransaction::InMemorySaveTransaction(InMemoryPersistenceStore& store, PersistenceRevision base_revision)
    : store_(store), base_revision_(base_revision)
{
}

SaveTransactionState InMemorySaveTransaction::GetState() const
{
    return state_;
}

PersistenceRevision InMemorySaveTransaction::GetBaseRevision() const
{
    return base_revision_;
}

foundation::Result<void> InMemorySaveTransaction::EnsureOpen() const
{
    if (state_ != SaveTransactionState::Open)
    {
        return PersistenceFailure("persistence.transaction_invalid_state", "transaction accepts mutations only while open");
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemorySaveTransaction::UpsertObject(PersistentObjectRecord record)
{
    const auto open = EnsureOpen();
    if (!open)
    {
        return open;
    }
    const auto valid = ValidateObject(record);
    if (!valid)
    {
        return valid;
    }
    operations_.push_back(UpsertObjectOperation{std::move(record)});
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemorySaveTransaction::AdminRemoveObject(PersistentObjectId id)
{
    const auto open = EnsureOpen();
    if (!open)
    {
        return open;
    }
    if (!id.IsValid())
    {
        return PersistenceFailure("persistence.invalid_id", "persistent object id must be valid before removal");
    }
    operations_.push_back(AdminRemoveObjectOperation{id});
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemorySaveTransaction::DeleteObject(TombstoneRecord tombstone)
{
    const auto open = EnsureOpen();
    if (!open)
    {
        return open;
    }
    const auto valid = ValidateTombstone(tombstone);
    if (!valid)
    {
        return valid;
    }
    operations_.push_back(DeleteObjectOperation{std::move(tombstone)});
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemorySaveTransaction::UpsertLazyRule(LazyRuleRecord record)
{
    const auto open = EnsureOpen();
    if (!open)
    {
        return open;
    }
    const auto valid = ValidateLazyRule(record);
    if (!valid)
    {
        return valid;
    }
    operations_.push_back(UpsertLazyRuleOperation{std::move(record)});
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemorySaveTransaction::UpdateLazyRule(LazyRuleRecord record)
{
    return UpsertLazyRule(std::move(record));
}

foundation::Result<void> InMemorySaveTransaction::RemoveLazyRule(LazyRuleId id)
{
    const auto open = EnsureOpen();
    if (!open)
    {
        return open;
    }
    if (!id.IsValid())
    {
        return PersistenceFailure("persistence.invalid_lazy_rule_id", "lazy rule id must be valid before removal");
    }
    operations_.push_back(RemoveLazyRuleOperation{id});
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemorySaveTransaction::AdminAddTombstone(TombstoneRecord tombstone)
{
    const auto open = EnsureOpen();
    if (!open)
    {
        return open;
    }
    const auto valid = ValidateTombstone(tombstone);
    if (!valid)
    {
        return valid;
    }
    operations_.push_back(AdminAddTombstoneOperation{std::move(tombstone)});
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemorySaveTransaction::UpsertZoneOverride(ZoneOverrideSnapshot snapshot)
{
    const auto open = EnsureOpen();
    if (!open)
    {
        return open;
    }
    const auto valid = ValidateZoneOverride(snapshot);
    if (!valid)
    {
        return valid;
    }
    operations_.push_back(UpsertZoneOverrideOperation{std::move(snapshot)});
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemorySaveTransaction::RemoveZoneOverride(const PersistenceLocation& location)
{
    const auto open = EnsureOpen();
    if (!open)
    {
        return open;
    }
    if (!IsValidLocation(location))
    {
        return PersistenceFailure("persistence.invalid_location", "zone override removal location must contain at least one valid component");
    }
    operations_.push_back(RemoveZoneOverrideOperation{location});
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemorySaveTransaction::Commit()
{
    const auto open = EnsureOpen();
    if (!open)
    {
        state_ = SaveTransactionState::Failed;
        return open;
    }

    state_ = SaveTransactionState::Committing;
    const auto candidate = store_.BuildCandidateSnapshot(*this);
    if (!candidate)
    {
        state_ = SaveTransactionState::Failed;
        return foundation::Result<void>::Failure(candidate.GetError());
    }

    const auto published = store_.PublishSnapshot(candidate.Value());
    if (!published)
    {
        state_ = SaveTransactionState::Failed;
        return published;
    }

    state_ = SaveTransactionState::Committed;
    return foundation::Result<void>::Success();
}

void InMemorySaveTransaction::Rollback()
{
    if (state_ == SaveTransactionState::Open || state_ == SaveTransactionState::Failed)
    {
        operations_.clear();
        state_ = SaveTransactionState::RolledBack;
    }
}

std::optional<PersistentObjectRecord> InMemoryPersistenceStore::FindObject(PersistentObjectId id) const
{
    const auto iterator = objects_.find(id);
    if (iterator == objects_.end())
    {
        return std::nullopt;
    }
    return iterator->second;
}

std::vector<PersistentObjectRecord> InMemoryPersistenceStore::FindByLocation(const PersistenceLocation& location) const
{
    std::vector<PersistentObjectRecord> matches;
    for (const auto& [id, record] : objects_)
    {
        (void)id;
        if (record.location == location)
        {
            matches.push_back(record);
        }
    }
    std::sort(matches.begin(), matches.end(), [](const auto& left, const auto& right) { return left.persistent_id.Raw() < right.persistent_id.Raw(); });
    return matches;
}

std::vector<PersistentObjectRecord> InMemoryPersistenceStore::ListObjects() const
{
    std::vector<PersistentObjectRecord> records;
    records.reserve(objects_.size());
    for (const auto& [id, record] : objects_)
    {
        (void)id;
        records.push_back(record);
    }
    std::sort(records.begin(), records.end(), [](const auto& left, const auto& right) { return left.persistent_id.Raw() < right.persistent_id.Raw(); });
    return records;
}

bool InMemoryPersistenceStore::IsDirty(PersistentObjectId id) const
{
    return dirty_ids_.contains(id);
}

std::vector<PersistentObjectId> InMemoryPersistenceStore::CollectDirty() const
{
    std::vector<PersistentObjectId> ids(dirty_ids_.begin(), dirty_ids_.end());
    std::sort(ids.begin(), ids.end(), [](const auto& left, const auto& right) { return left.Raw() < right.Raw(); });
    return ids;
}

std::optional<TombstoneRecord> InMemoryPersistenceStore::FindTombstone(PersistentObjectId id) const
{
    const auto iterator = tombstones_.find(id);
    if (iterator == tombstones_.end())
    {
        return std::nullopt;
    }
    return iterator->second;
}

bool InMemoryPersistenceStore::IsTombstoned(PersistentObjectId id) const
{
    return tombstones_.contains(id);
}

std::vector<TombstoneRecord> InMemoryPersistenceStore::ListTombstones() const
{
    std::vector<TombstoneRecord> records;
    records.reserve(tombstones_.size());
    for (const auto& [id, tombstone] : tombstones_)
    {
        (void)id;
        records.push_back(tombstone);
    }
    std::sort(records.begin(), records.end(), [](const auto& left, const auto& right) { return left.persistent_id.Raw() < right.persistent_id.Raw(); });
    return records;
}

std::optional<ZoneOverrideSnapshot> InMemoryPersistenceStore::FindZoneOverride(const PersistenceLocation& location) const
{
    const auto iterator = zone_overrides_.find(location);
    if (iterator == zone_overrides_.end())
    {
        return std::nullopt;
    }
    return iterator->second;
}

std::vector<ZoneOverrideSnapshot> InMemoryPersistenceStore::ListZoneOverrides() const
{
    std::vector<ZoneOverrideSnapshot> snapshots;
    snapshots.reserve(zone_overrides_.size());
    for (const auto& [location, snapshot] : zone_overrides_)
    {
        (void)location;
        snapshots.push_back(snapshot);
    }
    std::sort(snapshots.begin(), snapshots.end(), [](const auto& left, const auto& right) {
        return left.location.region_id.Raw() < right.location.region_id.Raw();
    });
    return snapshots;
}

std::optional<LazyRuleRecord> InMemoryPersistenceStore::FindLazyRule(LazyRuleId id) const
{
    const auto iterator = lazy_rules_.find(id);
    if (iterator == lazy_rules_.end())
    {
        return std::nullopt;
    }
    return iterator->second;
}

std::vector<LazyRuleRecord> InMemoryPersistenceStore::FindLazyRules(PersistentObjectId target_id) const
{
    std::vector<LazyRuleRecord> matches;
    for (const auto& [id, rule] : lazy_rules_)
    {
        (void)id;
        if (rule.target_id == target_id)
        {
            matches.push_back(rule);
        }
    }
    std::sort(matches.begin(), matches.end(), [](const auto& left, const auto& right) { return left.rule_id.Raw() < right.rule_id.Raw(); });
    return matches;
}

std::vector<LazyRuleRecord> InMemoryPersistenceStore::QueryDueLazyRules(GameTimePoint now) const
{
    std::vector<LazyRuleRecord> matches;
    for (const auto& [id, rule] : lazy_rules_)
    {
        (void)id;
        if (rule.state == LazyRuleState::Pending && rule.evaluate_after_game_time <= now)
        {
            matches.push_back(rule);
        }
    }
    std::sort(matches.begin(), matches.end(), [](const auto& left, const auto& right) {
        if (left.evaluate_after_game_time == right.evaluate_after_game_time)
        {
            return left.rule_id.Raw() < right.rule_id.Raw();
        }
        return left.evaluate_after_game_time < right.evaluate_after_game_time;
    });
    return matches;
}

std::vector<LazyRuleRecord> InMemoryPersistenceStore::ListLazyRules() const
{
    std::vector<LazyRuleRecord> records;
    records.reserve(lazy_rules_.size());
    for (const auto& [id, rule] : lazy_rules_)
    {
        (void)id;
        records.push_back(rule);
    }
    std::sort(records.begin(), records.end(), [](const auto& left, const auto& right) { return left.rule_id.Raw() < right.rule_id.Raw(); });
    return records;
}

PersistenceRevision InMemoryPersistenceStore::GetRevision() const
{
    return revision_;
}

std::unique_ptr<ISaveTransaction> InMemoryPersistenceStore::OpenTransaction()
{
    return OpenTransaction(revision_);
}

std::unique_ptr<ISaveTransaction> InMemoryPersistenceStore::OpenTransaction(PersistenceRevision base_revision)
{
    return std::make_unique<InMemorySaveTransaction>(*this, base_revision);
}

PersistenceSnapshot InMemoryPersistenceStore::CreateSnapshot() const
{
    PersistenceSnapshot snapshot{};
    snapshot.current_revision = revision_;
    snapshot.objects = ListObjects();
    snapshot.tombstones = ListTombstones();
    snapshot.lazy_rules = ListLazyRules();
    snapshot.zone_overrides = ListZoneOverrides();
    return snapshot;
}

foundation::Result<void> ValidatePersistenceSnapshot(const PersistenceSnapshot& snapshot)
{
    std::unordered_set<PersistentObjectId> object_ids;
    std::unordered_set<PersistentObjectId> tombstone_ids;
    std::unordered_set<LazyRuleId> lazy_rule_ids;
    std::unordered_set<PersistenceLocation, PersistenceLocationHash> override_locations;

    for (const PersistentObjectRecord& record : snapshot.objects)
    {
        const auto valid = ValidateObject(record);
        if (!valid)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "snapshot contains an invalid persistent object");
        }
        if (record.revision > snapshot.current_revision)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "object revision exceeds snapshot revision");
        }
        if (!object_ids.insert(record.persistent_id).second)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "snapshot contains duplicate object ids");
        }
    }

    for (const TombstoneRecord& tombstone : snapshot.tombstones)
    {
        const auto valid = ValidateTombstone(tombstone);
        if (!valid)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "snapshot contains an invalid tombstone");
        }
        if (tombstone.revision > snapshot.current_revision)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "tombstone revision exceeds snapshot revision");
        }
        if (!tombstone_ids.insert(tombstone.persistent_id).second)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "snapshot contains duplicate tombstones");
        }
        if (object_ids.contains(tombstone.persistent_id))
        {
            return PersistenceFailure("persistence.invalid_snapshot", "snapshot contains an active and tombstoned object");
        }
    }

    for (const LazyRuleRecord& rule : snapshot.lazy_rules)
    {
        const auto valid = ValidateLazyRule(rule);
        if (!valid)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "snapshot contains an invalid lazy rule");
        }
        if (rule.revision > snapshot.current_revision)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "lazy rule revision exceeds snapshot revision");
        }
        if (!lazy_rule_ids.insert(rule.rule_id).second)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "snapshot contains duplicate lazy rule ids");
        }
    }

    for (const ZoneOverrideSnapshot& zone : snapshot.zone_overrides)
    {
        const auto valid = ValidateZoneOverride(zone);
        if (!valid)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "snapshot contains an invalid zone override");
        }
        if (zone.revision > snapshot.current_revision)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "zone override revision exceeds snapshot revision");
        }
        if (!override_locations.insert(zone.location).second)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "snapshot contains duplicate zone overrides");
        }
    }

    return foundation::Result<void>::Success();
}

foundation::Result<PersistenceCandidateState> InMemoryPersistenceStore::BuildCandidateSnapshot(const InMemorySaveTransaction& transaction) const
{
    if (transaction.base_revision_ != revision_)
    {
        return foundation::Result<PersistenceCandidateState>::Failure(
            foundation::Error::Create("persistence.conflict", "transaction base revision does not match current store revision"));
    }

    std::unordered_map<PersistentObjectId, PersistentObjectRecord> candidate_objects = objects_;
    std::unordered_map<LazyRuleId, LazyRuleRecord> candidate_lazy_rules = lazy_rules_;
    std::unordered_map<PersistentObjectId, TombstoneRecord> candidate_tombstones = tombstones_;
    std::unordered_map<PersistenceLocation, ZoneOverrideSnapshot, PersistenceLocationHash> candidate_zone_overrides = zone_overrides_;
    std::unordered_set<PersistentObjectId> candidate_dirty = dirty_ids_;
    const PersistenceRevision candidate_revision = revision_ + 1;

    for (const PersistenceOperation& operation : transaction.operations_)
    {
        const auto valid = std::visit(
            [&](const auto& typed_operation) -> foundation::Result<void> {
                using Operation = std::decay_t<decltype(typed_operation)>;
                if constexpr (std::is_same_v<Operation, UpsertObjectOperation>)
                {
                    const auto record_valid = ValidateObject(typed_operation.record);
                    if (!record_valid)
                    {
                        return record_valid;
                    }
                    PersistentObjectRecord record = typed_operation.record;
                    record.revision = candidate_revision;
                    candidate_objects[record.persistent_id] = std::move(record);
                    candidate_tombstones.erase(typed_operation.record.persistent_id);
                    candidate_dirty.insert(typed_operation.record.persistent_id);
                    return foundation::Result<void>::Success();
                }
                else if constexpr (std::is_same_v<Operation, DeleteObjectOperation>)
                {
                    const auto tombstone_valid = ValidateTombstone(typed_operation.tombstone);
                    if (!tombstone_valid)
                    {
                        return tombstone_valid;
                    }
                    if (!candidate_objects.contains(typed_operation.tombstone.persistent_id))
                    {
                        return PersistenceFailure("persistence.record_not_found", "persistent object record was not found for deletion");
                    }
                    TombstoneRecord tombstone = typed_operation.tombstone;
                    tombstone.revision = candidate_revision;
                    candidate_objects.erase(tombstone.persistent_id);
                    candidate_tombstones[tombstone.persistent_id] = std::move(tombstone);
                    candidate_dirty.insert(typed_operation.tombstone.persistent_id);
                    return foundation::Result<void>::Success();
                }
                else if constexpr (std::is_same_v<Operation, AdminRemoveObjectOperation>)
                {
                    if (!candidate_objects.contains(typed_operation.id))
                    {
                        return PersistenceFailure("persistence.record_not_found", "persistent object record was not found for administrative removal");
                    }
                    candidate_objects.erase(typed_operation.id);
                    candidate_dirty.insert(typed_operation.id);
                    return foundation::Result<void>::Success();
                }
                else if constexpr (std::is_same_v<Operation, AdminAddTombstoneOperation>)
                {
                    const auto tombstone_valid = ValidateTombstone(typed_operation.tombstone);
                    if (!tombstone_valid)
                    {
                        return tombstone_valid;
                    }
                    TombstoneRecord tombstone = typed_operation.tombstone;
                    tombstone.revision = candidate_revision;
                    candidate_tombstones[tombstone.persistent_id] = std::move(tombstone);
                    return foundation::Result<void>::Success();
                }
                else if constexpr (std::is_same_v<Operation, UpsertLazyRuleOperation>)
                {
                    const auto rule_valid = ValidateLazyRule(typed_operation.record);
                    if (!rule_valid)
                    {
                        return rule_valid;
                    }
                    LazyRuleRecord rule = typed_operation.record;
                    rule.revision = candidate_revision;
                    candidate_lazy_rules[rule.rule_id] = std::move(rule);
                    return foundation::Result<void>::Success();
                }
                else if constexpr (std::is_same_v<Operation, RemoveLazyRuleOperation>)
                {
                    if (!candidate_lazy_rules.contains(typed_operation.id))
                    {
                        return PersistenceFailure("persistence.lazy_rule_not_found", "lazy rule was not found for removal");
                    }
                    candidate_lazy_rules.erase(typed_operation.id);
                    return foundation::Result<void>::Success();
                }
                else if constexpr (std::is_same_v<Operation, UpsertZoneOverrideOperation>)
                {
                    const auto snapshot_valid = ValidateZoneOverride(typed_operation.snapshot);
                    if (!snapshot_valid)
                    {
                        return snapshot_valid;
                    }
                    ZoneOverrideSnapshot snapshot = typed_operation.snapshot;
                    snapshot.revision = candidate_revision;
                    candidate_zone_overrides[snapshot.location] = std::move(snapshot);
                    return foundation::Result<void>::Success();
                }
                else
                {
                    if (!candidate_zone_overrides.contains(typed_operation.location))
                    {
                        return PersistenceFailure("persistence.override_not_found", "zone override was not found for removal");
                    }
                    candidate_zone_overrides.erase(typed_operation.location);
                    return foundation::Result<void>::Success();
                }
            },
            operation);
        if (!valid)
        {
            return foundation::Result<PersistenceCandidateState>::Failure(valid.GetError());
        }
    }

    PersistenceSnapshot candidate{};
    candidate.current_revision = candidate_revision;
    candidate.objects.reserve(candidate_objects.size());
    candidate.tombstones.reserve(candidate_tombstones.size());
    candidate.lazy_rules.reserve(candidate_lazy_rules.size());
    candidate.zone_overrides.reserve(candidate_zone_overrides.size());
    for (const auto& [id, record] : candidate_objects)
    {
        (void)id;
        candidate.objects.push_back(record);
    }
    for (const auto& [id, tombstone] : candidate_tombstones)
    {
        (void)id;
        candidate.tombstones.push_back(tombstone);
    }
    for (const auto& [id, rule] : candidate_lazy_rules)
    {
        (void)id;
        candidate.lazy_rules.push_back(rule);
    }
    for (const auto& [location, snapshot] : candidate_zone_overrides)
    {
        (void)location;
        candidate.zone_overrides.push_back(snapshot);
    }

    const auto valid_snapshot = ValidatePersistenceSnapshot(candidate);
    if (!valid_snapshot)
    {
        return foundation::Result<PersistenceCandidateState>::Failure(valid_snapshot.GetError());
    }
    PersistenceCandidateState candidate_state{};
    candidate_state.snapshot = std::move(candidate);
    candidate_state.dirty_ids = std::move(candidate_dirty);
    return foundation::Result<PersistenceCandidateState>::Success(std::move(candidate_state));
}

foundation::Result<void> InMemoryPersistenceStore::PublishSnapshot(PersistenceCandidateState candidate)
{
    const PersistenceSnapshot& snapshot = candidate.snapshot;
    if (durability_ != PersistenceDurability::MemoryOnly)
    {
        if (!backend_)
        {
            return PersistenceFailure("persistence.backend_missing", "persistence backend is required by durability policy");
        }
        const auto saved = backend_->Save(snapshot);
        if (!saved)
        {
            return saved;
        }
        if (durability_ == PersistenceDurability::SaveAndFlushRequired)
        {
            const auto flushed = backend_->Flush();
            if (!flushed)
            {
                return flushed;
            }
        }
    }

    objects_.clear();
    tombstones_.clear();
    lazy_rules_.clear();
    zone_overrides_.clear();
    dirty_ids_ = std::move(candidate.dirty_ids);
    revision_ = snapshot.current_revision;
    for (PersistentObjectRecord record : snapshot.objects)
    {
        objects_[record.persistent_id] = std::move(record);
    }
    for (TombstoneRecord tombstone : snapshot.tombstones)
    {
        tombstones_[tombstone.persistent_id] = std::move(tombstone);
    }
    for (LazyRuleRecord rule : snapshot.lazy_rules)
    {
        lazy_rules_[rule.rule_id] = std::move(rule);
    }
    for (ZoneOverrideSnapshot zone : snapshot.zone_overrides)
    {
        zone_overrides_[zone.location] = std::move(zone);
    }
    return foundation::Result<void>::Success();
}

foundation::Result<PersistenceSnapshot> InMemoryPersistenceBackend::Load()
{
    return foundation::Result<PersistenceSnapshot>::Success(snapshot_);
}

foundation::Result<void> InMemoryPersistenceBackend::Save(const PersistenceSnapshot& snapshot)
{
    snapshot_ = snapshot;
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryPersistenceBackend::Flush()
{
    return foundation::Result<void>::Success();
}
} // namespace epidemic::runtime

