#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Persistence/persistence_location.h"
#include "Epidemic/Runtime/Persistence/persistent_record.h"

#include <optional>
#include <vector>

namespace epidemic::runtime
{
class IPersistentObjectStore
{
  public:
    virtual ~IPersistentObjectStore() = default;

    [[nodiscard]] virtual foundation::Result<void> Upsert(PersistentObjectRecord record) = 0;
    [[nodiscard]] virtual std::optional<PersistentObjectRecord> Find(PersistentObjectId id) const = 0;
    [[nodiscard]] virtual std::vector<PersistentObjectRecord> FindByLocation(const PersistenceLocation& location) const = 0;
    [[nodiscard]] virtual foundation::Result<void> Remove(PersistentObjectId id) = 0;
};
} // namespace epidemic::runtime
