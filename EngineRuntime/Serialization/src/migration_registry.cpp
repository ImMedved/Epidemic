#include "migration_registry.h"

#include "Epidemic/Runtime/Serialization/serialization_error.h"

#include <algorithm>
#include <vector>

namespace epidemic::runtime
{
namespace
{
[[nodiscard]] foundation::Result<void> MigrationFailure(std::string_view code, std::string_view message)
{
    return foundation::Result<void>::Failure(CreateSerializationError(code, message));
}

template <typename TValue>
[[nodiscard]] foundation::Result<TValue> MigrationFailureValue(std::string_view code, std::string_view message)
{
    return foundation::Result<TValue>::Failure(CreateSerializationError(code, message));
}

[[nodiscard]] bool ContainsVersion(const std::vector<SchemaVersion>& versions, SchemaVersion version)
{
    return std::find(versions.begin(), versions.end(), version) != versions.end();
}
} // namespace

foundation::Result<void> MigrationRegistry::RegisterMigration(std::shared_ptr<const IMigration> migration)
{
    if (!migration)
    {
        return MigrationFailure("serialization.migration.null", "migration pointer must not be null");
    }

    const MigrationKey key = migration->GetKey();
    if (!key.type_id.IsValid())
    {
        return MigrationFailure("serialization.migration.invalid_type", "migration must declare a valid type id");
    }
    if (key.from == key.to)
    {
        return MigrationFailure("serialization.migration.invalid_version", "migration must change schema version");
    }
    if (migrations_.contains(key))
    {
        return MigrationFailure("serialization.migration.duplicate_key", "migration key is already registered");
    }

    migrations_.emplace(key, std::move(migration));
    return foundation::Result<void>::Success();
}

std::shared_ptr<const IMigration> MigrationRegistry::FindMigration(const MigrationKey& key) const
{
    const auto iterator = migrations_.find(key);
    if (iterator == migrations_.end())
    {
        return {};
    }
    return iterator->second;
}

bool MigrationRegistry::HasMigration(const MigrationKey& key) const
{
    return FindMigration(key) != nullptr;
}

foundation::Result<std::vector<std::shared_ptr<const IMigration>>> MigrationRegistry::FindMigrationPath(
    foundation::StringId type_id,
    SchemaVersion from,
    SchemaVersion to) const
{
    using MigrationPath = std::vector<std::shared_ptr<const IMigration>>;

    if (!type_id.IsValid())
    {
        return MigrationFailureValue<MigrationPath>("serialization.migration.invalid_type", "migration path type id must be valid");
    }
    if (from == to)
    {
        return foundation::Result<MigrationPath>::Success({});
    }

    std::vector<MigrationPath> paths;
    bool cycle_detected = false;

    auto search = [&](auto&& self, SchemaVersion current, MigrationPath& current_path, std::vector<SchemaVersion>& visited) -> void {
        if (paths.size() > 1)
        {
            return;
        }

        for (const auto& [key, migration] : migrations_)
        {
            if (key.type_id != type_id || !(key.from == current))
            {
                continue;
            }

            if (ContainsVersion(visited, key.to))
            {
                cycle_detected = true;
                continue;
            }

            current_path.push_back(migration);
            if (key.to == to)
            {
                paths.push_back(current_path);
            }
            else
            {
                visited.push_back(key.to);
                self(self, key.to, current_path, visited);
                visited.pop_back();
            }
            current_path.pop_back();
        }
    };

    MigrationPath current_path;
    std::vector<SchemaVersion> visited{from};
    search(search, from, current_path, visited);

    if (paths.empty())
    {
        if (cycle_detected)
        {
            return MigrationFailureValue<MigrationPath>("serialization.migration.cycle", "migration path contains a cycle");
        }
        return MigrationFailureValue<MigrationPath>("serialization.migration.path_missing", "migration path was not found");
    }
    if (paths.size() > 1)
    {
        return MigrationFailureValue<MigrationPath>("serialization.migration.ambiguous_path", "migration path is ambiguous");
    }

    return foundation::Result<MigrationPath>::Success(paths.front());
}
} // namespace epidemic::runtime
