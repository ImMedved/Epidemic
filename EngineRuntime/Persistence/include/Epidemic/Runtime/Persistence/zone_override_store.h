#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Persistence/lazy_rule_record.h"
#include "Epidemic/Runtime/Persistence/persistence_location.h"
#include "Epidemic/Runtime/Persistence/tombstone_store.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace epidemic::runtime
{
struct ZoneOverrideSnapshot
{
    PersistenceLocation location{};
    std::vector<PersistentObjectId> record_ids;
    std::vector<TombstoneRecord> tombstones;
    std::vector<LazyRuleRecord> lazy_rules;
    std::uint64_t revision = 0;
};

class IZoneOverrideStore
{
  public:
    virtual ~IZoneOverrideStore() = default;

    [[nodiscard]] virtual std::optional<ZoneOverrideSnapshot> FindZoneOverride(const PersistenceLocation& location) const = 0;
    [[nodiscard]] virtual std::vector<ZoneOverrideSnapshot> ListZoneOverrides() const = 0;
};
} // namespace epidemic::runtime
