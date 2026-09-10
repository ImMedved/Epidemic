#include "serializer_registry.h"

#include "Epidemic/Runtime/Serialization/serialization_error.h"

#include <exception>

namespace epidemic::runtime
{
namespace
{
[[nodiscard]] foundation::Result<void> SerializerFailure(std::string_view code, std::string_view message, std::string_view context = {})
{
    return foundation::Result<void>::Failure(CreateSerializationError(code, message, context));
}
} // namespace

foundation::Result<void> SerializerRegistry::RegisterSerializer(std::shared_ptr<const ISerializer> serializer)
{
    if (frozen_)
    {
        return SerializerFailure("serialization.registry_frozen", "serializer registry is frozen");
    }
    if (!serializer)
    {
        return SerializerFailure("serialization.serializer.null", "serializer pointer must not be null");
    }

    try
    {
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
    catch (const std::exception& error)
    {
        return SerializerFailure("serialization.serializer.exception", "serializer metadata callback threw", error.what());
    }
    catch (...)
    {
        return SerializerFailure("serialization.serializer.exception", "serializer metadata callback threw an unknown exception");
    }
}

foundation::Result<void> SerializerRegistry::Freeze()
{
    frozen_ = true;
    return foundation::Result<void>::Success();
}

bool SerializerRegistry::IsFrozen() const noexcept
{
    return frozen_;
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
