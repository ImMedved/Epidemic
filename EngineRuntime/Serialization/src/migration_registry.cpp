#include "migration_registry.h"

#include "Epidemic/Runtime/Serialization/serialization_error.h"

namespace epidemic::runtime
{
foundation::Result<void> MigrationRegistry::RegisterMigration(IMigration& migration)
{
    const MigrationKey key = migration.GetKey();
    if (!key.type_id.IsValid())
    {
        return foundation::Result<void>::Failure(
            CreateSerializationError("serialization.migration.invalid_type", "migration must declare a valid type id"));
    }

    if (migrations_.contains(key))
    {
        return foundation::Result<void>::Failure(
            CreateSerializationError("serialization.migration.duplicate_key", "migration key is already registered"));
    }

    migrations_.emplace(key, &migration);
    return foundation::Result<void>::Success();
}

IMigration* MigrationRegistry::FindMigration(const MigrationKey& key)
{
    const auto iterator = migrations_.find(key);
    if (iterator == migrations_.end())
    {
        return nullptr;
    }

    return iterator->second;
}

const IMigration* MigrationRegistry::FindMigration(const MigrationKey& key) const
{
    const auto iterator = migrations_.find(key);
    if (iterator == migrations_.end())
    {
        return nullptr;
    }

    return iterator->second;
}

bool MigrationRegistry::HasMigration(const MigrationKey& key) const
{
    return FindMigration(key) != nullptr;
}
} // namespace epidemic::runtime
