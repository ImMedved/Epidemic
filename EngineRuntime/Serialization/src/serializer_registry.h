#pragma once

#include "Epidemic/Runtime/Serialization/serializer_registry.h"

#include <unordered_map>

namespace epidemic::runtime
{
class SerializerRegistry final : public ISerializerRegistry
{
  public:
    [[nodiscard]] foundation::Result<void> RegisterSerializer(ISerializer& serializer) override;
    [[nodiscard]] ISerializer* FindSerializer(foundation::StringId type_id) override;
    [[nodiscard]] const ISerializer* FindSerializer(foundation::StringId type_id) const override;
    [[nodiscard]] bool HasSerializer(foundation::StringId type_id) const override;

  private:
    std::unordered_map<foundation::StringId, ISerializer*> serializers_;
};
} // namespace epidemic::runtime
