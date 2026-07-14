#include "in_memory_persistence_support.h"

#include <algorithm>
#include <utility>
#include <string_view>

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

InMemorySaveTransaction::InMemorySaveTransaction(InMemoryPersistenceStore& store) : store_(store)
{
}

SaveTransactionState InMemorySaveTransaction::GetState() const
{
    return state_;
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
    upsert_objects_.push_back(std::move(record));
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
    remove_objects_.push_back(id);
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
    upsert_lazy_rules_.push_back(std::move(record));
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
    tombstones_.push_back(std::move(tombstone));
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
    upsert_zone_overrides_.push_back(std::move(snapshot));
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
    remove_zone_overrides_.push_back(location);
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
        upsert_objects_.clear();
        remove_objects_.clear();
        upsert_lazy_rules_.clear();
        tombstones_.clear();
        upsert_zone_overrides_.clear();
        remove_zone_overrides_.clear();
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

std::uint64_t InMemoryPersistenceStore::GetRevision() const
{
    return revision_;
}

std::unique_ptr<ISaveTransaction> InMemoryPersistenceStore::OpenTransaction()
{
    return std::make_unique<InMemorySaveTransaction>(*this);
}

foundation::Result<void> InMemoryPersistenceStore::Validate(const InMemorySaveTransaction& transaction) const
{
    for (const PersistentObjectRecord& record : transaction.upsert_objects_)
    {
        const auto valid = ValidateObject(record);
        if (!valid)
        {
            return valid;
        }
    }
    for (const LazyRuleRecord& rule : transaction.upsert_lazy_rules_)
    {
        const auto valid = ValidateLazyRule(rule);
        if (!valid)
        {
            return valid;
        }
    }
    for (const TombstoneRecord& tombstone : transaction.tombstones_)
    {
        const auto valid = ValidateTombstone(tombstone);
        if (!valid)
        {
            return valid;
        }
    }
    for (const ZoneOverrideSnapshot& snapshot : transaction.upsert_zone_overrides_)
    {
        const auto valid = ValidateZoneOverride(snapshot);
        if (!valid)
        {
            return valid;
        }
    }
    for (PersistentObjectId id : transaction.remove_objects_)
    {
        if (!objects_.contains(id))
        {
            return PersistenceFailure("persistence.record_not_found", "persistent object record was not found for removal");
        }
    }
    for (const PersistenceLocation& location : transaction.remove_zone_overrides_)
    {
        if (!zone_overrides_.contains(location))
        {
            return PersistenceFailure("persistence.override_not_found", "zone override was not found for removal");
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
    for (PersistentObjectRecord record : transaction.upsert_objects_)
    {
        record.revision = revision_;
        dirty_ids_.insert(record.persistent_id);
        objects_[record.persistent_id] = std::move(record);
    }
    for (PersistentObjectId id : transaction.remove_objects_)
    {
        objects_.erase(id);
        dirty_ids_.insert(id);
    }
    for (LazyRuleRecord rule : transaction.upsert_lazy_rules_)
    {
        rule.revision = revision_;
        lazy_rules_[rule.rule_id] = std::move(rule);
    }
    for (TombstoneRecord tombstone : transaction.tombstones_)
    {
        tombstone.revision = revision_;
        tombstones_[tombstone.persistent_id] = std::move(tombstone);
    }
    for (ZoneOverrideSnapshot snapshot : transaction.upsert_zone_overrides_)
    {
        snapshot.revision = revision_;
        zone_overrides_[snapshot.location] = std::move(snapshot);
    }
    for (const PersistenceLocation& location : transaction.remove_zone_overrides_)
    {
        zone_overrides_.erase(location);
    }
    return foundation::Result<void>::Success();
}
} // namespace epidemic::runtime

