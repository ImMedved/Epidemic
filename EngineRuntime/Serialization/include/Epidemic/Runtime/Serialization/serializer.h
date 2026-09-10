#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Foundation/string_id.h"
#include "Epidemic/Runtime/Serialization/archive_reader.h"
#include "Epidemic/Runtime/Serialization/archive_writer.h"
#include "Epidemic/Runtime/Serialization/schema_version.h"
#include "Epidemic/Runtime/Serialization/serialization_error.h"

#include <exception>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <utility>

namespace epidemic::runtime
{
class IArchiveFactory;
class ISerializer;

template <typename TObject>
[[nodiscard]] foundation::Result<void> SerializeObject(const ISerializer& serializer, const TObject& object, IArchiveWriter& writer);

template <typename TObject>
[[nodiscard]] foundation::Result<void> DeserializeObject(const ISerializer& serializer, IArchiveReader& reader, TObject& object);

template <typename TObject>
[[nodiscard]] foundation::Result<SerializedDocument> SerializeToDocument(
    const ISerializer& serializer,
    const TObject& object,
    IArchiveFactory& archives);

class ISerializer
{
  public:
    virtual ~ISerializer() = default;

    [[nodiscard]] virtual foundation::StringId GetTypeId() const = 0;
    [[nodiscard]] virtual SchemaVersion GetSchemaVersion() const = 0;
    [[nodiscard]] virtual std::type_index GetCppType() const = 0;

  protected:
    [[nodiscard]] virtual foundation::Result<void> Serialize(const void* object, IArchiveWriter& writer) const = 0;
    [[nodiscard]] virtual foundation::Result<void> Deserialize(IArchiveReader& reader, void* object) const = 0;

    template <typename TObject>
    friend foundation::Result<void> SerializeObject(const ISerializer& serializer, const TObject& object, IArchiveWriter& writer);

    template <typename TObject>
    friend foundation::Result<void> DeserializeObject(const ISerializer& serializer, IArchiveReader& reader, TObject& object);

    template <typename TObject>
    friend foundation::Result<SerializedDocument> SerializeToDocument(
        const ISerializer& serializer,
        const TObject& object,
        IArchiveFactory& archives);
};

template <typename TObject>
[[nodiscard]] foundation::Result<void> SerializeObject(const ISerializer& serializer, const TObject& object, IArchiveWriter& writer)
{
    // Low-level streaming helper. It is intentionally not rollback-capable for arbitrary
    // IArchiveWriter implementations; callers that need public failure atomicity should
    // use SerializeToDocument(), which owns a fresh writer and publishes nothing on failure.
    try
    {
        if (serializer.GetCppType() != std::type_index(typeid(TObject)))
        {
            return foundation::Result<void>::Failure(
                CreateSerializationError("serialization.cpp_type_mismatch", "serializer C++ type does not match source object"));
        }
        return serializer.Serialize(&object, writer);
    }
    catch (const std::exception& error)
    {
        return foundation::Result<void>::Failure(
            CreateSerializationError("serialization.serializer.exception", "serializer threw during serialization", error.what()));
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(
            CreateSerializationError("serialization.serializer.exception", "serializer threw an unknown exception during serialization"));
    }
}

template <typename TObject>
[[nodiscard]] foundation::Result<void> DeserializeObject(const ISerializer& serializer, IArchiveReader& reader, TObject& object)
{
    try
    {
        if (serializer.GetCppType() != std::type_index(typeid(TObject)))
        {
            return foundation::Result<void>::Failure(
                CreateSerializationError("serialization.cpp_type_mismatch", "serializer C++ type does not match target object"));
        }

        if constexpr (std::is_copy_constructible_v<TObject> && std::is_nothrow_swappable_v<TObject>)
        {
            TObject candidate(object);
            auto result = serializer.Deserialize(reader, &candidate);
            if (!result)
            {
                return result;
            }
            using std::swap;
            swap(object, candidate);
            return foundation::Result<void>::Success();
        }
        else
        {
            return foundation::Result<void>::Failure(CreateSerializationError(
                "serialization.staging_unsupported",
                "destination type must be copy constructible and nothrow swappable for failure-atomic deserialization"));
        }
    }
    catch (const std::exception& error)
    {
        return foundation::Result<void>::Failure(
            CreateSerializationError("serialization.serializer.exception", "serializer threw during deserialization", error.what()));
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(
            CreateSerializationError("serialization.serializer.exception", "serializer threw an unknown exception during deserialization"));
    }
}

} // namespace epidemic::runtime
