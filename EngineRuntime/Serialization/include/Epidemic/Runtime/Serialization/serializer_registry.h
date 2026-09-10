#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Serialization/serializer.h"

#include <memory>

namespace epidemic::runtime
{
// Threading: mutation must occur on the runtime thread.
// Concurrent reads/writes are not supported unless explicitly documented.
class ISerializerRegistry
{
  public:
    virtual ~ISerializerRegistry() = default;

    [[nodiscard]] virtual foundation::Result<void> RegisterSerializer(std::shared_ptr<const ISerializer> serializer) = 0;
    [[nodiscard]] virtual foundation::Result<void> Freeze() = 0;
    [[nodiscard]] virtual bool IsFrozen() const noexcept = 0;
    [[nodiscard]] virtual std::shared_ptr<const ISerializer> FindSerializer(foundation::StringId type_id) const = 0;
    [[nodiscard]] virtual bool HasSerializer(foundation::StringId type_id) const = 0;
};
} // namespace epidemic::runtime
