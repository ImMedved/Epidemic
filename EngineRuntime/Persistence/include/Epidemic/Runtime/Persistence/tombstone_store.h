#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"

namespace epidemic::runtime
{
class ITombstoneStore
{
  public:
    virtual ~ITombstoneStore() = default;

    [[nodiscard]] virtual foundation::Result<void> AddTombstone(PersistentObjectId id) = 0;
    [[nodiscard]] virtual bool IsTombstoned(PersistentObjectId id) const = 0;
};
} // namespace epidemic::runtime
