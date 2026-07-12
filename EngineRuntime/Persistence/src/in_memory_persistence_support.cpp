#include "in_memory_persistence_support.h"

// File note:
// Implementation file for the surrounding runtime type or test fixture. The comments
// below describe responsibilities, data flow and relationships between local helpers.
namespace epidemic::runtime
{
// Function note: Marks dirty.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void InMemoryDirtyTracker::MarkDirty(PersistentObjectId id)
{
    if (!id.IsValid())
    {
        return;
    }

    dirty_ids_.insert(id);
}

// Function note: Marks clean.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void InMemoryDirtyTracker::MarkClean(PersistentObjectId id)
{
    dirty_ids_.erase(id);
}

// Function note: Checks dirty.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
bool InMemoryDirtyTracker::IsDirty(PersistentObjectId id) const
{
    return dirty_ids_.contains(id);
}

// Function note: Handles collect dirty.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
std::vector<PersistentObjectId> InMemoryDirtyTracker::CollectDirty() const
{
    return std::vector<PersistentObjectId>(dirty_ids_.begin(), dirty_ids_.end());
}

// Function note: Handles add tombstone.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> InMemoryTombstoneStore::AddTombstone(PersistentObjectId id)
{
    if (!id.IsValid())
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("persistence.invalid_id", "persistent object id must be valid before tombstoning"));
    }

    tombstones_.insert(id);
    return foundation::Result<void>::Success();
}

// Function note: Checks tombstoned.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
bool InMemoryTombstoneStore::IsTombstoned(PersistentObjectId id) const
{
    return tombstones_.contains(id);
}

// Function note: Handles upsert.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> InMemoryZoneOverrideStore::Upsert(ZoneOverrideSnapshot snapshot)
{
    if (!snapshot.location.region_id.IsValid() && !snapshot.location.chunk_id.IsValid() && !snapshot.location.location_tag.IsValid())
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("persistence.invalid_location", "zone override location must contain at least one valid component"));
    }

    // Function note: Handles move.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    overrides_[snapshot.location] = std::move(snapshot);
    return foundation::Result<void>::Success();
}

// Function note: Finds the associated runtime state.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
std::optional<ZoneOverrideSnapshot> InMemoryZoneOverrideStore::Find(const PersistenceLocation& location) const
{
    const auto iterator = overrides_.find(location);
    if (iterator == overrides_.end())
    {
        return std::nullopt;
    }

    return iterator->second;
}

// Function note: Handles remove.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> InMemoryZoneOverrideStore::Remove(const PersistenceLocation& location)
{
    const auto removed = overrides_.erase(location);
    if (removed == 0)
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("persistence.override_not_found", "zone override was not found"));
    }

    return foundation::Result<void>::Success();
}

// Function note: Handles upsert.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> InMemoryPersistentObjectStore::Upsert(PersistentObjectRecord record)
{
    if (!record.persistent_id.IsValid())
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("persistence.invalid_id", "persistent object id must be valid before upsert"));
    }

    // Function note: Handles move.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    records_[record.persistent_id] = std::move(record);
    return foundation::Result<void>::Success();
}

// Function note: Finds the associated runtime state.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
std::optional<PersistentObjectRecord> InMemoryPersistentObjectStore::Find(PersistentObjectId id) const
{
    const auto iterator = records_.find(id);
    if (iterator == records_.end())
    {
        return std::nullopt;
    }

    return iterator->second;
}

// Function note: Finds by location.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
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

// Function note: Handles remove.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> InMemoryPersistentObjectStore::Remove(PersistentObjectId id)
{
    const auto removed = records_.erase(id);
    if (removed == 0)
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("persistence.record_not_found", "persistent object record was not found"));
    }

    return foundation::Result<void>::Success();
}

// Function note: Handles in memory save transaction.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
InMemorySaveTransaction::InMemorySaveTransaction() : state_(SaveTransactionState::Open)
{
}

// Function note: Gets state.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
SaveTransactionState InMemorySaveTransaction::GetState() const
{
    return state_;
}

// Function note: Handles commit.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> InMemorySaveTransaction::Commit()
{
    if (state_ != SaveTransactionState::Open)
    {
        state_ = SaveTransactionState::Failed;
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("persistence.transaction_invalid_state", "save transaction can only commit from Open state"));
    }

    state_ = SaveTransactionState::Committing;
    state_ = SaveTransactionState::Committed;
    return foundation::Result<void>::Success();
}

// Function note: Handles rollback.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void InMemorySaveTransaction::Rollback()
{
    if (state_ == SaveTransactionState::Committed)
    {
        return;
    }

    state_ = SaveTransactionState::RolledBack;
}

// Function note: Handles objects.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
IPersistentObjectStore& InMemoryPersistenceStore::Objects()
{
    return object_store_;
}

// Function note: Handles dirty.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
IDirtyTracker& InMemoryPersistenceStore::Dirty()
{
    return dirty_tracker_;
}

// Function note: Handles tombstones.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
ITombstoneStore& InMemoryPersistenceStore::Tombstones()
{
    return tombstone_store_;
}

// Function note: Handles zone overrides.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
IZoneOverrideStore& InMemoryPersistenceStore::ZoneOverrides()
{
    return zone_override_store_;
}

// Function note: Handles upsert lazy rule.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> InMemoryPersistenceStore::UpsertLazyRule(LazyRuleRecord record)
{
    if (!record.target_id.IsValid())
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("persistence.invalid_id", "lazy rule target id must be valid before upsert"));
    }

    lazy_rules_.emplace(record.target_id, std::move(record));
    return foundation::Result<void>::Success();
}

// Function note: Finds lazy rules.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
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

// Function note: Handles open transaction.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
std::unique_ptr<ISaveTransaction> InMemoryPersistenceStore::OpenTransaction()
{
    return std::make_unique<InMemorySaveTransaction>();
}
} // namespace epidemic::runtime
