#include "serializer_registry.h"

#include "Epidemic/Runtime/Serialization/serialization_error.h"

namespace epidemic::runtime
{
namespace
{
[[nodiscard]] foundation::Result<void> SerializerFailure(std::string_view code, std::string_view message)
{
    return foundation::Result<void>::Failure(CreateSerializationError(code, message));
}
} // namespace

foundation::Result<void> SerializerRegistry::RegisterSerializer(std::shared_ptr<const ISerializer> serializer)
{
    if (!serializer)
    {
        return SerializerFailure("serialization.serializer.null", "serializer pointer must not be null");
    }

    const foundation::StringId type_id = serializer->GetTypeId();
    if (!type_id.IsValid())
    {
        return SerializerFailure("serialization.serializer.invalid_type", "serializer must declare a valid type id");
    }

    if (serializers_.contains(type_id))
    {
        return SerializerFailure("serialization.serializer.duplicate_type", "serializer type is already registered");
    }

    serializers_.emplace(type_id, std::move(serializer));
    return foundation::Result<void>::Success();
}

std::shared_ptr<const ISerializer> SerializerRegistry::FindSerializer(foundation::StringId type_id) const
{
    const auto iterator = serializers_.find(type_id);
    if (iterator == serializers_.end())
    {
        return {};
    }
    return iterator->second;
}

bool SerializerRegistry::HasSerializer(foundation::StringId type_id) const
{
    return FindSerializer(type_id) != nullptr;
}
} // namespace epidemic::runtime
