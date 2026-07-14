#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Foundation/string_id.h"
#include "Epidemic/Runtime/Serialization/archive_reader.h"
#include "Epidemic/Runtime/Serialization/archive_writer.h"
#include "Epidemic/Runtime/Serialization/schema_version.h"

namespace epidemic::runtime
{
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
    [[nodiscard]] virtual foundation::Result<void> Serialize(const void* object, IArchiveWriter& writer) const = 0;
    [[nodiscard]] virtual foundation::Result<void> Deserialize(IArchiveReader& reader, void* object) const = 0;
};
} // namespace epidemic::runtime
