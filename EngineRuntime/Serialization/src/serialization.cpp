#include "Epidemic/Runtime/Serialization/serialization_services.h"

#include "archive_tree.h"
#include "in_memory_archive.h"
#include "migration_registry.h"
#include "serializer_registry.h"

#include <utility>

namespace epidemic::runtime
{
SerializedDocument::SerializedDocument(std::shared_ptr<const Impl> impl) : impl_(std::move(impl))
{
}

foundation::StringId SerializedDocument::GetTypeId() const
{
    return impl_ ? impl_->type_id : foundation::StringId{};
}

SchemaVersion SerializedDocument::GetSchemaVersion() const
{
    return impl_ ? impl_->schema_version : SchemaVersion{};
}

std::uint32_t SerializedDocument::GetFormatVersion() const
{
    return impl_ ? impl_->format_version : 0u;
}

bool SerializedDocument::IsValid() const noexcept
{
    return impl_ != nullptr && impl_->type_id.IsValid() && impl_->format_version == 1u && impl_->root != nullptr;
}

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
