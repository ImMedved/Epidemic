#pragma once

#include "Epidemic/Runtime/Persistence/dirty_tracker.h"
#include "Epidemic/Runtime/Persistence/lazy_rule_record.h"
#include "Epidemic/Runtime/Persistence/persistence_store.h"
#include "Epidemic/Runtime/Persistence/persistent_object_store.h"
#include "Epidemic/Runtime/Persistence/save_transaction.h"
#include "Epidemic/Runtime/Persistence/tombstone_store.h"
#include "Epidemic/Runtime/Persistence/zone_override_store.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

#include <unordered_map>
#include <unordered_set>

namespace epidemic::runtime
{
struct PersistenceLocationHash
{
    [[nodiscard]] size_t operator()(const PersistenceLocation& location) const noexcept
    {
        size_t seed = std::hash<RegionId>{}(location.region_id);
        seed ^= std::hash<ChunkId>{}(location.chunk_id) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= std::hash<std::uint64_t>{}(location.location_tag.Raw()) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

class InMemoryDirtyTracker final : public IDirtyTracker
{
  public:
    // Function note: Marks dirty.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    void MarkDirty(PersistentObjectId id) override;
    // Function note: Marks clean.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    void MarkClean(PersistentObjectId id) override;
    // Function note: Checks dirty.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] bool IsDirty(PersistentObjectId id) const override;
    // Function note: Handles collect dirty.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] std::vector<PersistentObjectId> CollectDirty() const override;

  private:
    std::unordered_set<PersistentObjectId> dirty_ids_;
};

class InMemoryTombstoneStore final : public ITombstoneStore
{
  public:
    // Function note: Handles add tombstone.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] foundation::Result<void> AddTombstone(PersistentObjectId id) override;
    // Function note: Checks tombstoned.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] bool IsTombstoned(PersistentObjectId id) const override;

  private:
    std::unordered_set<PersistentObjectId> tombstones_;
};

class InMemoryZoneOverrideStore final : public IZoneOverrideStore
{
  public:
    // Function note: Handles upsert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] foundation::Result<void> Upsert(ZoneOverrideSnapshot snapshot) override;
    // Function note: Finds the associated runtime state.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] std::optional<ZoneOverrideSnapshot> Find(const PersistenceLocation& location) const override;
    // Function note: Handles remove.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] foundation::Result<void> Remove(const PersistenceLocation& location) override;

  private:
    std::unordered_map<PersistenceLocation, ZoneOverrideSnapshot, PersistenceLocationHash> overrides_;
};

class InMemoryPersistentObjectStore final : public IPersistentObjectStore
{
  public:
    // Function note: Handles upsert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] foundation::Result<void> Upsert(PersistentObjectRecord record) override;
    // Function note: Finds the associated runtime state.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] std::optional<PersistentObjectRecord> Find(PersistentObjectId id) const override;
    // Function note: Finds by location.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] std::vector<PersistentObjectRecord> FindByLocation(const PersistenceLocation& location) const override;
    // Function note: Handles remove.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] foundation::Result<void> Remove(PersistentObjectId id) override;

  private:
    std::unordered_map<PersistentObjectId, PersistentObjectRecord> records_;
};

class InMemorySaveTransaction final : public ISaveTransaction
{
  public:
    // Function note: Handles in memory save transaction.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    InMemorySaveTransaction();

    // Function note: Gets state.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] SaveTransactionState GetState() const override;
    // Function note: Handles commit.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] foundation::Result<void> Commit() override;
    // Function note: Handles rollback.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    void Rollback() override;

  private:
    SaveTransactionState state_ = SaveTransactionState::NotStarted;
};

class InMemoryPersistenceStore final : public IPersistenceStore
{
  public:
    // Function note: Handles objects.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] IPersistentObjectStore& Objects() override;
    // Function note: Handles dirty.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] IDirtyTracker& Dirty() override;
    // Function note: Handles tombstones.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] ITombstoneStore& Tombstones() override;
    // Function note: Handles zone overrides.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] IZoneOverrideStore& ZoneOverrides() override;

    // Function note: Handles upsert lazy rule.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] foundation::Result<void> UpsertLazyRule(LazyRuleRecord record) override;
    // Function note: Finds lazy rules.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] std::vector<LazyRuleRecord> FindLazyRules(PersistentObjectId target_id) const override;

    // Function note: Handles open transaction.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] std::unique_ptr<ISaveTransaction> OpenTransaction() override;

  private:
    InMemoryPersistentObjectStore object_store_;
    InMemoryDirtyTracker dirty_tracker_;
    InMemoryTombstoneStore tombstone_store_;
    InMemoryZoneOverrideStore zone_override_store_;
    std::unordered_multimap<PersistentObjectId, LazyRuleRecord> lazy_rules_;
};
} 
