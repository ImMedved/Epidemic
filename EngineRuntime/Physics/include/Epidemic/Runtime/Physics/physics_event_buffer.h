#pragma once

#include "Epidemic/Runtime/Physics/physics_types.h"

#include <span>

namespace epidemic::runtime::physics
{
// Event boundary contract for publishing contacts out of Physics without mutating World directly.
class IPhysicsEventBuffer
{
  public:
    virtual ~IPhysicsEventBuffer() = default;

    [[nodiscard]] virtual std::span<const ContactEvent> Contacts() const = 0;
    virtual void Clear() = 0;
};
} // namespace epidemic::runtime::physics
