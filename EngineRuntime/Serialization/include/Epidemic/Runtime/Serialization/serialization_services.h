#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Serialization/archive_reader.h"
#include "Epidemic/Runtime/Serialization/archive_writer.h"
#include "Epidemic/Runtime/Serialization/migration_registry.h"
#include "Epidemic/Runtime/Serialization/serializer_registry.h"

#include <memory>

namespace epidemic::runtime
{
struct SerializationOptions
{
};

class IArchiveFactory
{
  public:
    virtual ~IArchiveFactory() = default;

    [[nodiscard]] virtual std::unique_ptr<IArchiveWriter> CreateWriter() const = 0;
    [[nodiscard]] virtual foundation::Result<std::unique_ptr<IArchiveReader>> CreateReader(const SerializedDocument& document) const = 0;
};

struct SerializationServices
{
    std::shared_ptr<ISerializerRegistry> serializers;
    std::shared_ptr<IMigrationRegistry> migrations;
    std::shared_ptr<IArchiveFactory> archives;
};

[[nodiscard]] foundation::Result<SerializationServices> CreateSerializationServices(const SerializationOptions& options = {});
} // namespace epidemic::runtime
