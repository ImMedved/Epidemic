#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"

#include <vector>

namespace epidemic::runtime
{
class IDirtyTracker
{
  public:
    virtual ~IDirtyTracker() = default;

    [[nodiscard]] virtual bool IsDirty(PersistentObjectId id) const = 0;
    [[nodiscard]] virtual std::vector<PersistentObjectId> CollectDirty() const = 0;
};
} // namespace epidemic::runtime
