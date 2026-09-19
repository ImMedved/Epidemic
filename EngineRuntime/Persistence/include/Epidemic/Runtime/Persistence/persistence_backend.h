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

// Closed enum domain. Callers must use one of these three durability levels.
enum class PersistenceDurability
{
    MemoryOnly,
    SaveRequired,
    SaveAndFlushRequired,
};

class IPersistenceBackend
{
  public:
    virtual ~IPersistenceBackend() = default;

    [[nodiscard]] virtual foundation::Result<PersistenceSnapshot> Load() = 0;
    // Atomic backend contract: Result failure or exception must leave the previously committed
    // durable snapshot authoritative. A successful return accepts the complete snapshot exactly
    // once at the requested durability level. Separate Save()/Flush() transaction APIs are not part
    // of the Runtime Persistence contract.
    [[nodiscard]] virtual foundation::Result<void> CommitSnapshot(const PersistenceSnapshot& snapshot, PersistenceDurability durability) = 0;
};
} // namespace epidemic::runtime
