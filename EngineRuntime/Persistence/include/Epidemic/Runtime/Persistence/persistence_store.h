#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Persistence/dirty_tracker.h"
#include "Epidemic/Runtime/Persistence/lazy_rule_record.h"
#include "Epidemic/Runtime/Persistence/persistent_object_store.h"
#include "Epidemic/Runtime/Persistence/save_transaction.h"
#include "Epidemic/Runtime/Persistence/tombstone_store.h"
#include "Epidemic/Runtime/Persistence/zone_override_store.h"

#include <memory>
#include <vector>

namespace epidemic::runtime
{
class IPersistenceStore
{
  public:
    virtual ~IPersistenceStore() = default;

    [[nodiscard]] virtual IPersistentObjectStore& Objects() = 0;
    [[nodiscard]] virtual IDirtyTracker& Dirty() = 0;
    [[nodiscard]] virtual ITombstoneStore& Tombstones() = 0;
    [[nodiscard]] virtual IZoneOverrideStore& ZoneOverrides() = 0;

    [[nodiscard]] virtual foundation::Result<void> UpsertLazyRule(LazyRuleRecord record) = 0;
    [[nodiscard]] virtual std::vector<LazyRuleRecord> FindLazyRules(PersistentObjectId target_id) const = 0;

    [[nodiscard]] virtual std::unique_ptr<ISaveTransaction> OpenTransaction() = 0;
};
} // namespace epidemic::runtime
