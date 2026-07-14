#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Physics/physics_types.h"

namespace epidemic::runtime::physics
{
// File note:
// Public query contract for deterministic raycast and overlap requests.
class IPhysicsQuery
{
  public:
    virtual ~IPhysicsQuery() = default;

    [[nodiscard]] virtual foundation::Result<RaycastHit> Raycast(const RaycastQuery& query) const = 0;
    [[nodiscard]] virtual foundation::Result<OverlapResult> Overlap(const OverlapQuery& query) const = 0;
};
} // namespace epidemic::runtime::physics
