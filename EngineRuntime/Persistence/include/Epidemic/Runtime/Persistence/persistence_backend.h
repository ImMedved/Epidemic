#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Persistence/lazy_rule_record.h"
#include "Epidemic/Runtime/Persistence/persistent_record.h"
#include "Epidemic/Runtime/Persistence/save_transaction.h"
#include "Epidemic/Runtime/Persistence/tombstone_store.h"
#include "Epidemic/Runtime/Persistence/zone_override_store.h"

#include <cstdint>
#include <vector>

namespace epidemic::runtime
{
struct PersistenceSnapshot
{
    PersistenceRevision current_revision = 0;
    std::vector<PersistentObjectRecord> objects;
    std::vector<TombstoneRecord> tombstones;
    std::vector<LazyRuleRecord> lazy_rules;
    std::vector<ZoneOverrideSnapshot> zone_overrides;
};

class IPersistenceBackend
{
  public:
    virtual ~IPersistenceBackend() = default;

    [[nodiscard]] virtual foundation::Result<PersistenceSnapshot> Load() = 0;
    [[nodiscard]] virtual foundation::Result<void> Save(const PersistenceSnapshot& snapshot) = 0;
    [[nodiscard]] virtual foundation::Result<void> Flush() = 0;
};
} // namespace epidemic::runtime
