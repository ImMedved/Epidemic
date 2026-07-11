#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Serialization/serializer.h"

namespace epidemic::runtime
{
class ISerializerRegistry
{
  public:
    virtual ~ISerializerRegistry() = default;

    [[nodiscard]] virtual foundation::Result<void> RegisterSerializer(ISerializer& serializer) = 0;
    [[nodiscard]] virtual ISerializer* FindSerializer(foundation::StringId type_id) = 0;
    [[nodiscard]] virtual const ISerializer* FindSerializer(foundation::StringId type_id) const = 0;
    [[nodiscard]] virtual bool HasSerializer(foundation::StringId type_id) const = 0;
};
} // namespace epidemic::runtime
