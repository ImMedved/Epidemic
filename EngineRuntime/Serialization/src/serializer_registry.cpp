#include "serializer_registry.h"

#include "Epidemic/Runtime/Serialization/serialization_error.h"

namespace epidemic::runtime
{
foundation::Result<void> SerializerRegistry::RegisterSerializer(ISerializer& serializer)
{
    const foundation::StringId type_id = serializer.GetTypeId();
    if (!type_id.IsValid())
    {
        return foundation::Result<void>::Failure(
            CreateSerializationError("serialization.serializer.invalid_type", "serializer must declare a valid type id"));
    }

    if (serializers_.contains(type_id))
    {
        return foundation::Result<void>::Failure(
            CreateSerializationError("serialization.serializer.duplicate_type", "serializer type is already registered"));
    }

    serializers_.emplace(type_id, &serializer);
    return foundation::Result<void>::Success();
}

ISerializer* SerializerRegistry::FindSerializer(foundation::StringId type_id)
{
    const auto iterator = serializers_.find(type_id);
    if (iterator == serializers_.end())
    {
        return nullptr;
    }

    return iterator->second;
}

const ISerializer* SerializerRegistry::FindSerializer(foundation::StringId type_id) const
{
    const auto iterator = serializers_.find(type_id);
    if (iterator == serializers_.end())
    {
        return nullptr;
    }

    return iterator->second;
}

bool SerializerRegistry::HasSerializer(foundation::StringId type_id) const
{
    return FindSerializer(type_id) != nullptr;
}
} // namespace epidemic::runtime
