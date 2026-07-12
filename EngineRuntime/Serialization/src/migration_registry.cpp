#include "migration_registry.h"

// File note:
// Implementation file for the surrounding runtime type or test fixture. The comments
// below describe responsibilities, data flow and relationships between local helpers.
#include "Epidemic/Runtime/Serialization/serialization_error.h"

namespace epidemic::runtime
{
// Function note: Registers migration.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> MigrationRegistry::RegisterMigration(IMigration& migration)
{
    const MigrationKey key = migration.GetKey();
    if (!key.type_id.IsValid())
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates serialization error.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            CreateSerializationError("serialization.migration.invalid_type", "migration must declare a valid type id"));
    }

    if (migrations_.contains(key))
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates serialization error.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            CreateSerializationError("serialization.migration.duplicate_key", "migration key is already registered"));
    }

    migrations_.emplace(key, &migration);
    return foundation::Result<void>::Success();
}

// Function note: Finds migration.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
IMigration* MigrationRegistry::FindMigration(const MigrationKey& key)
{
    const auto iterator = migrations_.find(key);
    if (iterator == migrations_.end())
    {
        return nullptr;
    }

    return iterator->second;
}

// Function note: Finds migration.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
const IMigration* MigrationRegistry::FindMigration(const MigrationKey& key) const
{
    const auto iterator = migrations_.find(key);
    if (iterator == migrations_.end())
    {
        return nullptr;
    }

    return iterator->second;
}

// Function note: Checks migration.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
bool MigrationRegistry::HasMigration(const MigrationKey& key) const
{
    return FindMigration(key) != nullptr;
}
} 
