#include "Epidemic/Runtime/Serialization/serialization_services.h"

#include "in_memory_archive.h"
#include "migration_registry.h"
#include "serializer_registry.h"

namespace epidemic::runtime
{
foundation::Result<SerializationServices> CreateSerializationServices(const SerializationOptions& options)
{
    (void)options;

    SerializationServices services{};
    services.serializers = std::make_shared<SerializerRegistry>();
    services.migrations = std::make_shared<MigrationRegistry>();
    services.archives = std::make_shared<InMemoryArchiveFactory>();
    return foundation::Result<SerializationServices>::Success(std::move(services));
}
} // namespace epidemic::runtime
