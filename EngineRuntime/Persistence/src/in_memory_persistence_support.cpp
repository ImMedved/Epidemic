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

foundation::Result<void> InMemorySaveTransaction::RemoveObject(PersistentObjectId id)
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
    operations_.push_back(RemoveObjectOperation{id});
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

foundation::Result<void> InMemorySaveTransaction::AddTombstone(TombstoneRecord tombstone)
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
    operations_.push_back(AddTombstoneOperation{std::move(tombstone)});
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
    const auto applied = store_.Apply(*this);
    if (!applied)
    {
        state_ = SaveTransactionState::Failed;
        return applied;
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

foundation::Result<void> InMemoryPersistenceStore::Validate(const InMemorySaveTransaction& transaction) const
{
    if (transaction.base_revision_ != revision_)
    {
        return PersistenceFailure("persistence.conflict", "transaction base revision does not match current store revision");
    }

    std::unordered_set<PersistentObjectId> available_objects;
    available_objects.reserve(objects_.size());
    for (const auto& [id, record] : objects_)
    {
        (void)record;
        available_objects.insert(id);
    }

    std::unordered_set<PersistenceLocation, PersistenceLocationHash> available_overrides;
    available_overrides.reserve(zone_overrides_.size());
    for (const auto& [location, snapshot] : zone_overrides_)
    {
        (void)snapshot;
        available_overrides.insert(location);
    }

    for (const PersistenceOperation& operation : transaction.operations_)
    {
        const auto valid = std::visit(
            [&](const auto& typed_operation) -> foundation::Result<void> {
                using Operation = std::decay_t<decltype(typed_operation)>;
                if constexpr (std::is_same_v<Operation, UpsertObjectOperation>)
                {
                    const auto record_valid = ValidateObject(typed_operation.record);
                    if (record_valid)
                    {
                        available_objects.insert(typed_operation.record.persistent_id);
                    }
                    return record_valid;
                }
                else if constexpr (std::is_same_v<Operation, RemoveObjectOperation>)
                {
                    if (!available_objects.contains(typed_operation.id))
                    {
                        return PersistenceFailure("persistence.record_not_found", "persistent object record was not found for removal");
                    }
                    available_objects.erase(typed_operation.id);
                    return foundation::Result<void>::Success();
                }
                else if constexpr (std::is_same_v<Operation, DeleteObjectOperation>)
                {
                    const auto tombstone_valid = ValidateTombstone(typed_operation.tombstone);
                    if (!tombstone_valid)
                    {
                        return tombstone_valid;
                    }
                    if (!available_objects.contains(typed_operation.tombstone.persistent_id))
                    {
                        return PersistenceFailure("persistence.record_not_found", "persistent object record was not found for deletion");
                    }
                    available_objects.erase(typed_operation.tombstone.persistent_id);
                    return foundation::Result<void>::Success();
                }
                else if constexpr (std::is_same_v<Operation, AddTombstoneOperation>)
                {
                    return ValidateTombstone(typed_operation.tombstone);
                }
                else if constexpr (std::is_same_v<Operation, UpsertLazyRuleOperation>)
                {
                    return ValidateLazyRule(typed_operation.record);
                }
                else if constexpr (std::is_same_v<Operation, RemoveLazyRuleOperation>)
                {
                    if (!lazy_rules_.contains(typed_operation.id))
                    {
                        return PersistenceFailure("persistence.lazy_rule_not_found", "lazy rule was not found for removal");
                    }
                    return foundation::Result<void>::Success();
                }
                else if constexpr (std::is_same_v<Operation, UpsertZoneOverrideOperation>)
                {
                    const auto snapshot_valid = ValidateZoneOverride(typed_operation.snapshot);
                    if (snapshot_valid)
                    {
                        available_overrides.insert(typed_operation.snapshot.location);
                    }
                    return snapshot_valid;
                }
                else
                {
                    if (!available_overrides.contains(typed_operation.location))
                    {
                        return PersistenceFailure("persistence.override_not_found", "zone override was not found for removal");
                    }
                    available_overrides.erase(typed_operation.location);
                    return foundation::Result<void>::Success();
                }
            },
            operation);
        if (!valid)
        {
            return valid;
        }
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryPersistenceStore::Apply(InMemorySaveTransaction& transaction)
{
    const auto valid = Validate(transaction);
    if (!valid)
    {
        return valid;
    }

    ++revision_;
    for (const PersistenceOperation& operation : transaction.operations_)
    {
        std::visit(
            [&](const auto& typed_operation) {
                using Operation = std::decay_t<decltype(typed_operation)>;
                if constexpr (std::is_same_v<Operation, UpsertObjectOperation>)
                {
                    PersistentObjectRecord record = typed_operation.record;
                    record.revision = revision_;
                    dirty_ids_.insert(record.persistent_id);
                    objects_[record.persistent_id] = std::move(record);
                }
                else if constexpr (std::is_same_v<Operation, RemoveObjectOperation>)
                {
                    objects_.erase(typed_operation.id);
                    dirty_ids_.insert(typed_operation.id);
                }
                else if constexpr (std::is_same_v<Operation, DeleteObjectOperation>)
                {
                    TombstoneRecord tombstone = typed_operation.tombstone;
                    tombstone.revision = revision_;
                    objects_.erase(tombstone.persistent_id);
                    dirty_ids_.insert(tombstone.persistent_id);
                    tombstones_[tombstone.persistent_id] = std::move(tombstone);
                }
                else if constexpr (std::is_same_v<Operation, AddTombstoneOperation>)
                {
                    TombstoneRecord tombstone = typed_operation.tombstone;
                    tombstone.revision = revision_;
                    tombstones_[tombstone.persistent_id] = std::move(tombstone);
                }
                else if constexpr (std::is_same_v<Operation, UpsertLazyRuleOperation>)
                {
                    LazyRuleRecord rule = typed_operation.record;
                    rule.revision = revision_;
                    lazy_rules_[rule.rule_id] = std::move(rule);
                }
                else if constexpr (std::is_same_v<Operation, RemoveLazyRuleOperation>)
                {
                    lazy_rules_.erase(typed_operation.id);
                }
                else if constexpr (std::is_same_v<Operation, UpsertZoneOverrideOperation>)
                {
                    ZoneOverrideSnapshot snapshot = typed_operation.snapshot;
                    snapshot.revision = revision_;
                    zone_overrides_[snapshot.location] = std::move(snapshot);
                }
                else
                {
                    zone_overrides_.erase(typed_operation.location);
                }
            },
            operation);
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

