#pragma once

#include "Epidemic/Runtime/Persistence/persistence_store.h"

#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace epidemic::runtime
{
struct PersistenceLocationHash
{
    [[nodiscard]] size_t operator()(const PersistenceLocation& location) const noexcept;
};

class InMemoryPersistenceStore;

class InMemorySaveTransaction final : public ISaveTransaction
{
  public:
    explicit InMemorySaveTransaction(InMemoryPersistenceStore& store);

    [[nodiscard]] SaveTransactionState GetState() const override;
    [[nodiscard]] foundation::Result<void> UpsertObject(PersistentObjectRecord record) override;
    [[nodiscard]] foundation::Result<void> RemoveObject(PersistentObjectId id) override;
    [[nodiscard]] foundation::Result<void> UpsertLazyRule(LazyRuleRecord record) override;
    [[nodiscard]] foundation::Result<void> AddTombstone(TombstoneRecord tombstone) override;
    [[nodiscard]] foundation::Result<void> UpsertZoneOverride(ZoneOverrideSnapshot snapshot) override;
    [[nodiscard]] foundation::Result<void> RemoveZoneOverride(const PersistenceLocation& location) override;
    [[nodiscard]] foundation::Result<void> Commit() override;
    void Rollback() override;

  private:
    friend class InMemoryPersistenceStore;

    [[nodiscard]] foundation::Result<void> EnsureOpen() const;

    InMemoryPersistenceStore& store_;
    SaveTransactionState state_ = SaveTransactionState::Open;
    std::vector<PersistentObjectRecord> upsert_objects_;
    std::vector<PersistentObjectId> remove_objects_;
    std::vector<LazyRuleRecord> upsert_lazy_rules_;
    std::vector<TombstoneRecord> tombstones_;
    std::vector<ZoneOverrideSnapshot> upsert_zone_overrides_;
    std::vector<PersistenceLocation> remove_zone_overrides_;
};

class InMemoryPersistenceStore final : public IPersistenceStore
{
  public:
    [[nodiscard]] std::optional<PersistentObjectRecord> FindObject(PersistentObjectId id) const override;
    [[nodiscard]] std::vector<PersistentObjectRecord> FindByLocation(const PersistenceLocation& location) const override;
    [[nodiscard]] std::vector<PersistentObjectRecord> ListObjects() const override;

    [[nodiscard]] bool IsDirty(PersistentObjectId id) const override;
    [[nodiscard]] std::vector<PersistentObjectId> CollectDirty() const override;

    [[nodiscard]] std::optional<TombstoneRecord> FindTombstone(PersistentObjectId id) const override;
    [[nodiscard]] bool IsTombstoned(PersistentObjectId id) const override;
    [[nodiscard]] std::vector<TombstoneRecord> ListTombstones() const override;

    [[nodiscard]] std::optional<ZoneOverrideSnapshot> FindZoneOverride(const PersistenceLocation& location) const override;
    [[nodiscard]] std::vector<ZoneOverrideSnapshot> ListZoneOverrides() const override;

    [[nodiscard]] std::optional<LazyRuleRecord> FindLazyRule(LazyRuleId id) const override;
    [[nodiscard]] std::vector<LazyRuleRecord> FindLazyRules(PersistentObjectId target_id) const override;
    [[nodiscard]] std::vector<LazyRuleRecord> ListLazyRules() const override;
    [[nodiscard]] std::uint64_t GetRevision() const override;
    [[nodiscard]] std::unique_ptr<ISaveTransaction> OpenTransaction() override;

  private:
    friend class InMemorySaveTransaction;

    [[nodiscard]] foundation::Result<void> Validate(const InMemorySaveTransaction& transaction) const;
    [[nodiscard]] foundation::Result<void> Apply(InMemorySaveTransaction& transaction);

    std::unordered_map<PersistentObjectId, PersistentObjectRecord> objects_;
    std::unordered_map<LazyRuleId, LazyRuleRecord> lazy_rules_;
    std::unordered_map<PersistentObjectId, TombstoneRecord> tombstones_;
    std::unordered_map<PersistenceLocation, ZoneOverrideSnapshot, PersistenceLocationHash> zone_overrides_;
    std::unordered_set<PersistentObjectId> dirty_ids_;
    std::uint64_t revision_ = 0;
};
} // namespace epidemic::runtime


