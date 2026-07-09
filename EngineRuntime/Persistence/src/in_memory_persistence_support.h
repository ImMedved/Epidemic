#pragma once

#include "Epidemic/Runtime/Persistence/dirty_tracker.h"
#include "Epidemic/Runtime/Persistence/lazy_rule_record.h"
#include "Epidemic/Runtime/Persistence/persistence_store.h"
#include "Epidemic/Runtime/Persistence/persistent_object_store.h"
#include "Epidemic/Runtime/Persistence/save_transaction.h"
#include "Epidemic/Runtime/Persistence/tombstone_store.h"
#include "Epidemic/Runtime/Persistence/zone_override_store.h"

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
    void MarkDirty(PersistentObjectId id) override;
    void MarkClean(PersistentObjectId id) override;
    [[nodiscard]] bool IsDirty(PersistentObjectId id) const override;
    [[nodiscard]] std::vector<PersistentObjectId> CollectDirty() const override;

  private:
    std::unordered_set<PersistentObjectId> dirty_ids_;
};

class InMemoryTombstoneStore final : public ITombstoneStore
{
  public:
    [[nodiscard]] foundation::Result<void> AddTombstone(PersistentObjectId id) override;
    [[nodiscard]] bool IsTombstoned(PersistentObjectId id) const override;

  private:
    std::unordered_set<PersistentObjectId> tombstones_;
};

class InMemoryZoneOverrideStore final : public IZoneOverrideStore
{
  public:
    [[nodiscard]] foundation::Result<void> Upsert(ZoneOverrideSnapshot snapshot) override;
    [[nodiscard]] std::optional<ZoneOverrideSnapshot> Find(const PersistenceLocation& location) const override;
    [[nodiscard]] foundation::Result<void> Remove(const PersistenceLocation& location) override;

  private:
    std::unordered_map<PersistenceLocation, ZoneOverrideSnapshot, PersistenceLocationHash> overrides_;
};

class InMemoryPersistentObjectStore final : public IPersistentObjectStore
{
  public:
    [[nodiscard]] foundation::Result<void> Upsert(PersistentObjectRecord record) override;
    [[nodiscard]] std::optional<PersistentObjectRecord> Find(PersistentObjectId id) const override;
    [[nodiscard]] std::vector<PersistentObjectRecord> FindByLocation(const PersistenceLocation& location) const override;
    [[nodiscard]] foundation::Result<void> Remove(PersistentObjectId id) override;

  private:
    std::unordered_map<PersistentObjectId, PersistentObjectRecord> records_;
};

class InMemorySaveTransaction final : public ISaveTransaction
{
  public:
    InMemorySaveTransaction();

    [[nodiscard]] SaveTransactionState GetState() const override;
    [[nodiscard]] foundation::Result<void> Commit() override;
    void Rollback() override;

  private:
    SaveTransactionState state_ = SaveTransactionState::NotStarted;
};

class InMemoryPersistenceStore final : public IPersistenceStore
{
  public:
    [[nodiscard]] IPersistentObjectStore& Objects() override;
    [[nodiscard]] IDirtyTracker& Dirty() override;
    [[nodiscard]] ITombstoneStore& Tombstones() override;
    [[nodiscard]] IZoneOverrideStore& ZoneOverrides() override;

    [[nodiscard]] foundation::Result<void> UpsertLazyRule(LazyRuleRecord record) override;
    [[nodiscard]] std::vector<LazyRuleRecord> FindLazyRules(PersistentObjectId target_id) const override;

    [[nodiscard]] std::unique_ptr<ISaveTransaction> OpenTransaction() override;

  private:
    InMemoryPersistentObjectStore object_store_;
    InMemoryDirtyTracker dirty_tracker_;
    InMemoryTombstoneStore tombstone_store_;
    InMemoryZoneOverrideStore zone_override_store_;
    std::unordered_multimap<PersistentObjectId, LazyRuleRecord> lazy_rules_;
};
} // namespace epidemic::runtime
