#include "in_memory_persistence_support.h"

namespace epidemic::runtime
{
void InMemoryDirtyTracker::MarkDirty(PersistentObjectId id)
{
    if (!id.IsValid())
    {
        return;
    }

    dirty_ids_.insert(id);
}

void InMemoryDirtyTracker::MarkClean(PersistentObjectId id)
{
    dirty_ids_.erase(id);
}

bool InMemoryDirtyTracker::IsDirty(PersistentObjectId id) const
{
    return dirty_ids_.contains(id);
}

std::vector<PersistentObjectId> InMemoryDirtyTracker::CollectDirty() const
{
    return std::vector<PersistentObjectId>(dirty_ids_.begin(), dirty_ids_.end());
}

foundation::Result<void> InMemoryTombstoneStore::AddTombstone(PersistentObjectId id)
{
    if (!id.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("persistence.invalid_id", "persistent object id must be valid before tombstoning"));
    }

    tombstones_.insert(id);
    return foundation::Result<void>::Success();
}

bool InMemoryTombstoneStore::IsTombstoned(PersistentObjectId id) const
{
    return tombstones_.contains(id);
}

foundation::Result<void> InMemoryZoneOverrideStore::Upsert(ZoneOverrideSnapshot snapshot)
{
    if (!snapshot.location.region_id.IsValid() && !snapshot.location.chunk_id.IsValid() && !snapshot.location.location_tag.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("persistence.invalid_location", "zone override location must contain at least one valid component"));
    }

    overrides_[snapshot.location] = std::move(snapshot);
    return foundation::Result<void>::Success();
}

std::optional<ZoneOverrideSnapshot> InMemoryZoneOverrideStore::Find(const PersistenceLocation& location) const
{
    const auto iterator = overrides_.find(location);
    if (iterator == overrides_.end())
    {
        return std::nullopt;
    }

    return iterator->second;
}

foundation::Result<void> InMemoryZoneOverrideStore::Remove(const PersistenceLocation& location)
{
    const auto removed = overrides_.erase(location);
    if (removed == 0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("persistence.override_not_found", "zone override was not found"));
    }

    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryPersistentObjectStore::Upsert(PersistentObjectRecord record)
{
    if (!record.persistent_id.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("persistence.invalid_id", "persistent object id must be valid before upsert"));
    }

    records_[record.persistent_id] = std::move(record);
    return foundation::Result<void>::Success();
}

std::optional<PersistentObjectRecord> InMemoryPersistentObjectStore::Find(PersistentObjectId id) const
{
    const auto iterator = records_.find(id);
    if (iterator == records_.end())
    {
        return std::nullopt;
    }

    return iterator->second;
}

std::vector<PersistentObjectRecord> InMemoryPersistentObjectStore::FindByLocation(const PersistenceLocation& location) const
{
    std::vector<PersistentObjectRecord> matches;
    for (const auto& [id, record] : records_)
    {
        (void)id;
        if (record.location == location)
        {
            matches.push_back(record);
        }
    }

    return matches;
}

foundation::Result<void> InMemoryPersistentObjectStore::Remove(PersistentObjectId id)
{
    const auto removed = records_.erase(id);
    if (removed == 0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("persistence.record_not_found", "persistent object record was not found"));
    }

    return foundation::Result<void>::Success();
}

InMemorySaveTransaction::InMemorySaveTransaction() : state_(SaveTransactionState::Open)
{
}

SaveTransactionState InMemorySaveTransaction::GetState() const
{
    return state_;
}

foundation::Result<void> InMemorySaveTransaction::Commit()
{
    if (state_ != SaveTransactionState::Open)
    {
        state_ = SaveTransactionState::Failed;
        return foundation::Result<void>::Failure(
            foundation::Error::Create("persistence.transaction_invalid_state", "save transaction can only commit from Open state"));
    }

    state_ = SaveTransactionState::Committing;
    state_ = SaveTransactionState::Committed;
    return foundation::Result<void>::Success();
}

void InMemorySaveTransaction::Rollback()
{
    if (state_ == SaveTransactionState::Committed)
    {
        return;
    }

    state_ = SaveTransactionState::RolledBack;
}

IPersistentObjectStore& InMemoryPersistenceStore::Objects()
{
    return object_store_;
}

IDirtyTracker& InMemoryPersistenceStore::Dirty()
{
    return dirty_tracker_;
}

ITombstoneStore& InMemoryPersistenceStore::Tombstones()
{
    return tombstone_store_;
}

IZoneOverrideStore& InMemoryPersistenceStore::ZoneOverrides()
{
    return zone_override_store_;
}

foundation::Result<void> InMemoryPersistenceStore::UpsertLazyRule(LazyRuleRecord record)
{
    if (!record.target_id.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("persistence.invalid_id", "lazy rule target id must be valid before upsert"));
    }

    lazy_rules_.emplace(record.target_id, std::move(record));
    return foundation::Result<void>::Success();
}

std::vector<LazyRuleRecord> InMemoryPersistenceStore::FindLazyRules(PersistentObjectId target_id) const
{
    std::vector<LazyRuleRecord> matches;
    const auto [begin, end] = lazy_rules_.equal_range(target_id);
    for (auto iterator = begin; iterator != end; ++iterator)
    {
        matches.push_back(iterator->second);
    }

    return matches;
}

std::unique_ptr<ISaveTransaction> InMemoryPersistenceStore::OpenTransaction()
{
    return std::make_unique<InMemorySaveTransaction>();
}
} // namespace epidemic::runtime
