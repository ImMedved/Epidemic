#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"

#include <vector>

namespace epidemic::runtime
{
class IDirtyTracker
{
  public:
    virtual ~IDirtyTracker() = default;

    virtual void MarkDirty(PersistentObjectId id) = 0;
    virtual void MarkClean(PersistentObjectId id) = 0;
    [[nodiscard]] virtual bool IsDirty(PersistentObjectId id) const = 0;
    [[nodiscard]] virtual std::vector<PersistentObjectId> CollectDirty() const = 0;
};
} // namespace epidemic::runtime
