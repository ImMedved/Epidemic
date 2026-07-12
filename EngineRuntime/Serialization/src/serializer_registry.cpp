#include "serializer_registry.h"

// File note:
// Implementation file for the surrounding runtime type or test fixture. The comments
// below describe responsibilities, data flow and relationships between local helpers.
#include "Epidemic/Runtime/Serialization/serialization_error.h"

namespace epidemic::runtime
{
// Function note: Registers serializer.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> SerializerRegistry::RegisterSerializer(ISerializer& serializer)
{
    const foundation::StringId type_id = serializer.GetTypeId();
    if (!type_id.IsValid())
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates serialization error.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            CreateSerializationError("serialization.serializer.invalid_type", "serializer must declare a valid type id"));
    }

    if (serializers_.contains(type_id))
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates serialization error.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            CreateSerializationError("serialization.serializer.duplicate_type", "serializer type is already registered"));
    }

    serializers_.emplace(type_id, &serializer);
    return foundation::Result<void>::Success();
}

// Function note: Finds serializer.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
ISerializer* SerializerRegistry::FindSerializer(foundation::StringId type_id)
{
    const auto iterator = serializers_.find(type_id);
    if (iterator == serializers_.end())
    {
        return nullptr;
    }

    return iterator->second;
}

// Function note: Finds serializer.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
const ISerializer* SerializerRegistry::FindSerializer(foundation::StringId type_id) const
{
    const auto iterator = serializers_.find(type_id);
    if (iterator == serializers_.end())
    {
        return nullptr;
    }

    return iterator->second;
}

// Function note: Checks serializer.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
bool SerializerRegistry::HasSerializer(foundation::StringId type_id) const
{
    return FindSerializer(type_id) != nullptr;
}
} // namespace epidemic::runtime
