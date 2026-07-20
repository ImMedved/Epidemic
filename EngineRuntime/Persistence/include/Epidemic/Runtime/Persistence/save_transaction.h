#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Persistence/lazy_rule_record.h"
#include "Epidemic/Runtime/Persistence/persistent_record.h"
#include "Epidemic/Runtime/Persistence/tombstone_store.h"
#include "Epidemic/Runtime/Persistence/zone_override_store.h"

namespace epidemic::runtime
{
using PersistenceRevision = std::uint64_t;

enum class SaveTransactionState
{
    NotStarted,
    Open,
    Committing,
    Committed,
    RolledBack,
    Failed,
};

// Threading: mutation must occur on the runtime thread.
// Concurrent reads/writes are not supported unless explicitly documented.
class ISaveTransaction
{
  public:
    virtual ~ISaveTransaction() = default;

    [[nodiscard]] virtual SaveTransactionState GetState() const = 0;
    [[nodiscard]] virtual PersistenceRevision GetBaseRevision() const = 0;
    [[nodiscard]] virtual foundation::Result<void> UpsertObject(PersistentObjectRecord record) = 0;
    [[nodiscard]] virtual foundation::Result<void> DeleteObject(TombstoneRecord tombstone) = 0;
    [[nodiscard]] virtual foundation::Result<void> UpsertLazyRule(LazyRuleRecord record) = 0;
    [[nodiscard]] virtual foundation::Result<void> UpdateLazyRule(LazyRuleRecord record) = 0;
    [[nodiscard]] virtual foundation::Result<void> RemoveLazyRule(LazyRuleId id) = 0;
    [[nodiscard]] virtual foundation::Result<void> UpsertZoneOverride(ZoneOverrideSnapshot snapshot) = 0;
    [[nodiscard]] virtual foundation::Result<void> RemoveZoneOverride(const PersistenceLocation& location) = 0;
    [[nodiscard]] virtual foundation::Result<void> Commit() = 0;
    virtual void Rollback() = 0;
};

class IPersistenceAdministrativeTransaction
{
  public:
    virtual ~IPersistenceAdministrativeTransaction() = default;

    [[nodiscard]] virtual foundation::Result<void> AdminRemoveObject(PersistentObjectId id) = 0;
    [[nodiscard]] virtual foundation::Result<void> AdminAddTombstone(TombstoneRecord tombstone) = 0;
};
} // namespace epidemic::runtime
