#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Persistence/dirty_tracker.h"
#include "Epidemic/Runtime/Persistence/lazy_rule_record.h"
#include "Epidemic/Runtime/Persistence/persistent_object_store.h"
#include "Epidemic/Runtime/Persistence/save_transaction.h"
#include "Epidemic/Runtime/Persistence/tombstone_store.h"
#include "Epidemic/Runtime/Persistence/zone_override_store.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace epidemic::runtime
{
class IPersistenceStore : public IPersistentObjectStore, public IDirtyTracker, public ITombstoneStore, public IZoneOverrideStore
{
  public:
    virtual ~IPersistenceStore() = default;

    [[nodiscard]] virtual std::optional<LazyRuleRecord> FindLazyRule(LazyRuleId id) const = 0;
    [[nodiscard]] virtual std::vector<LazyRuleRecord> FindLazyRules(PersistentObjectId target_id) const = 0;
    [[nodiscard]] virtual std::vector<LazyRuleRecord> ListLazyRules() const = 0;
    [[nodiscard]] virtual std::uint64_t GetRevision() const = 0;
    [[nodiscard]] virtual std::unique_ptr<ISaveTransaction> OpenTransaction() = 0;
};
} // namespace epidemic::runtime

