#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Persistence/lazy_rule_record.h"
#include "Epidemic/Runtime/Persistence/persistence_location.h"

#include <optional>
#include <vector>

namespace epidemic::runtime
{
struct ZoneOverrideSnapshot
{
    PersistenceLocation location{};
    std::vector<PersistentObjectId> record_ids;
    std::vector<PersistentObjectId> tombstones;
    std::vector<LazyRuleRecord> lazy_rules;
};

class IZoneOverrideStore
{
  public:
    virtual ~IZoneOverrideStore() = default;

    [[nodiscard]] virtual foundation::Result<void> Upsert(ZoneOverrideSnapshot snapshot) = 0;
    [[nodiscard]] virtual std::optional<ZoneOverrideSnapshot> Find(const PersistenceLocation& location) const = 0;
    [[nodiscard]] virtual foundation::Result<void> Remove(const PersistenceLocation& location) = 0;
};
} // namespace epidemic::runtime
