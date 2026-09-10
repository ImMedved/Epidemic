#pragma once

#include "Epidemic/Runtime/Persistence/persistence_backend.h"
#include "Epidemic/Runtime/Persistence/persistence_operation.h"
#include "Epidemic/Runtime/Persistence/persistence_services.h"
#include "Epidemic/Runtime/Persistence/persistence_store.h"

#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace epidemic::runtime
{
struct PersistenceRuntimeTestAccess;

struct PersistenceLocationHash
{
    [[nodiscard]] size_t operator()(const PersistenceLocation& location) const noexcept;
};

class InMemoryPersistenceStore;

struct PersistenceCandidateState
{
    PersistenceSnapshot snapshot{};
    std::unordered_map<PersistentObjectId, PersistentObjectRecord> objects{};
    std::unordered_map<LazyRuleId, LazyRuleRecord> lazy_rules{};
    std::unordered_map<PersistentObjectId, TombstoneRecord> tombstones{};
    std::unordered_map<PersistenceLocation, ZoneOverrideSnapshot, PersistenceLocationHash> zone_overrides{};
    std::unordered_set<PersistentObjectId> dirty_ids{};
};

class InMemorySaveTransaction final : public ISaveTransaction, public IPersistenceAdministrativeTransaction
{
  public:
    InMemorySaveTransaction(InMemoryPersistenceStore& store, PersistenceRevision base_revision);

    [[nodiscard]] SaveTransactionState GetState() const override;
    [[nodiscard]] PersistenceRevision GetBaseRevision() const override;
    [[nodiscard]] foundation::Result<void> UpsertObject(PersistentObjectRecord record) override;
    [[nodiscard]] foundation::Result<void> DeleteObject(TombstoneRecord tombstone) override;
    [[nodiscard]] foundation::Result<void> UpsertLazyRule(LazyRuleRecord record) override;
    [[nodiscard]] foundation::Result<void> UpdateLazyRule(LazyRuleRecord record) override;
    [[nodiscard]] foundation::Result<void> RemoveLazyRule(LazyRuleId id) override;
    [[nodiscard]] foundation::Result<void> UpsertZoneOverride(ZoneOverrideSnapshot snapshot) override;
    [[nodiscard]] foundation::Result<void> RemoveZoneOverride(const PersistenceLocation& location) override;
    [[nodiscard]] foundation::Result<void> Commit() override;
    void Rollback() override;

    [[nodiscard]] foundation::Result<void> AdminRemoveObject(PersistentObjectId id) override;
    [[nodiscard]] foundation::Result<void> AdminAddTombstone(TombstoneRecord tombstone) override;

  private:
    friend class InMemoryPersistenceStore;
    friend struct PersistenceRuntimeTestAccess;

    [[nodiscard]] foundation::Result<void> EnsureOpen() const;
    [[nodiscard]] foundation::Result<void> StageOperation(PersistenceOperation operation);

    InMemoryPersistenceStore& store_;
    PersistenceRevision base_revision_ = 0;
    SaveTransactionState state_ = SaveTransactionState::Open;
    std::vector<PersistenceOperation> operations_;
    bool fail_next_operation_allocation_for_testing_ = false;
};

class InMemoryPersistenceStore final : public IPersistenceStore
{
  public:
    InMemoryPersistenceStore() = default;
    explicit InMemoryPersistenceStore(PersistenceSnapshot snapshot);
    InMemoryPersistenceStore(PersistenceSnapshot snapshot, std::shared_ptr<IPersistenceBackend> backend, PersistenceDurability durability);

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
    [[nodiscard]] std::vector<LazyRuleRecord> QueryDueLazyRules(GameTimePoint now) const override;
    [[nodiscard]] std::vector<LazyRuleRecord> ListLazyRules() const override;
    [[nodiscard]] PersistenceRevision GetRevision() const override;
    [[nodiscard]] std::unique_ptr<ISaveTransaction> OpenTransaction() override;
    [[nodiscard]] std::unique_ptr<ISaveTransaction> OpenTransaction(PersistenceRevision base_revision) override;
    [[nodiscard]] PersistenceSnapshot CreateSnapshot() const;

  private:
    friend class InMemorySaveTransaction;
    friend struct PersistenceRuntimeTestAccess;

    [[nodiscard]] foundation::Result<PersistenceCandidateState> BuildCandidateSnapshot(const InMemorySaveTransaction& transaction) const;
    [[nodiscard]] foundation::Result<void> PublishSnapshot(PersistenceCandidateState candidate);

    std::unordered_map<PersistentObjectId, PersistentObjectRecord> objects_;
    std::unordered_map<LazyRuleId, LazyRuleRecord> lazy_rules_;
    std::unordered_map<PersistentObjectId, TombstoneRecord> tombstones_;
    std::unordered_map<PersistenceLocation, ZoneOverrideSnapshot, PersistenceLocationHash> zone_overrides_;
    std::unordered_set<PersistentObjectId> dirty_ids_;
    PersistenceRevision revision_ = 0;
    std::shared_ptr<IPersistenceBackend> backend_;
    PersistenceDurability durability_ = PersistenceDurability::MemoryOnly;
    mutable bool fail_next_candidate_build_allocation_for_testing_ = false;
};

[[nodiscard]] foundation::Result<void> ValidatePersistenceSnapshot(const PersistenceSnapshot& snapshot);

class InMemoryPersistenceBackend final : public IPersistenceBackend
{
  public:
    [[nodiscard]] foundation::Result<PersistenceSnapshot> Load() override;
    [[nodiscard]] foundation::Result<void> CommitSnapshot(const PersistenceSnapshot& snapshot, PersistenceDurability durability) override;

  private:
    PersistenceSnapshot snapshot_{};
};
} // namespace epidemic::runtime


