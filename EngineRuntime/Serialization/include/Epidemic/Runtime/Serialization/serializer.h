#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Foundation/string_id.h"
#include "Epidemic/Runtime/Serialization/archive_reader.h"
#include "Epidemic/Runtime/Serialization/archive_writer.h"
#include "Epidemic/Runtime/Serialization/schema_version.h"
#include "Epidemic/Runtime/Serialization/serialization_error.h"

#include <typeindex>
#include <typeinfo>

namespace epidemic::runtime
{
class ISerializer;

template <typename TObject>
[[nodiscard]] foundation::Result<void> SerializeObject(const ISerializer& serializer, const TObject& object, IArchiveWriter& writer);

template <typename TObject>
[[nodiscard]] foundation::Result<void> DeserializeObject(const ISerializer& serializer, IArchiveReader& reader, TObject& object);

enum class SerializationState
{
    SchemaUnknown,
    SchemaKnown,
    Reading,
    Writing,
    Migrating,
    Valid,
    Invalid,
    Failed,
};

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
};

template <typename TObject>
[[nodiscard]] foundation::Result<void> SerializeObject(const ISerializer& serializer, const TObject& object, IArchiveWriter& writer)
{
    if (serializer.GetCppType() != std::type_index(typeid(TObject)))
    {
        return foundation::Result<void>::Failure(
            CreateSerializationError("serialization.cpp_type_mismatch", "serializer C++ type does not match source object"));
    }
    return serializer.Serialize(&object, writer);
}

template <typename TObject>
[[nodiscard]] foundation::Result<void> DeserializeObject(const ISerializer& serializer, IArchiveReader& reader, TObject& object)
{
    if (serializer.GetCppType() != std::type_index(typeid(TObject)))
    {
        return foundation::Result<void>::Failure(
            CreateSerializationError("serialization.cpp_type_mismatch", "serializer C++ type does not match target object"));
    }
    return serializer.Deserialize(reader, &object);
}
} // namespace epidemic::runtime
