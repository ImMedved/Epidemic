#pragma once

#include "Epidemic/Runtime/Serialization/serializer_registry.h"

#include <unordered_map>

namespace epidemic::runtime
{
class SerializerRegistry final : public ISerializerRegistry
{
  public:
    [[nodiscard]] foundation::Result<void> RegisterSerializer(std::shared_ptr<const ISerializer> serializer) override;
    [[nodiscard]] std::shared_ptr<const ISerializer> FindSerializer(foundation::StringId type_id) const override;
    [[nodiscard]] bool HasSerializer(foundation::StringId type_id) const override;

  private:
    std::unordered_map<foundation::StringId, std::shared_ptr<const ISerializer>> serializers_;
};
} // namespace epidemic::runtime
