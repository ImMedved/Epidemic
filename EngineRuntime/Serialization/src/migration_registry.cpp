#include "migration_registry.h"

#include "Epidemic/Runtime/Serialization/serialization_error.h"

#include <algorithm>
#include <exception>
#include <vector>

namespace epidemic::runtime
{
namespace
{
[[nodiscard]] foundation::Result<void> MigrationFailure(std::string_view code, std::string_view message, std::string_view context = {})
{
    return foundation::Result<void>::Failure(CreateSerializationError(code, message, context));
}

template <typename TValue>
[[nodiscard]] foundation::Result<TValue> MigrationFailureValue(std::string_view code, std::string_view message, std::string_view context = {})
{
    return foundation::Result<TValue>::Failure(CreateSerializationError(code, message, context));
}

[[nodiscard]] bool ContainsVersion(const std::vector<SchemaVersion>& versions, SchemaVersion version)
{
    return std::find(versions.begin(), versions.end(), version) != versions.end();
}
} // namespace

foundation::Result<void> MigrationRegistry::RegisterMigration(std::shared_ptr<const IMigration> migration)
{
    if (frozen_)
    {
        return MigrationFailure("serialization.registry_frozen", "migration registry is frozen");
    }
    if (!migration)
    {
        return MigrationFailure("serialization.migration.null", "migration pointer must not be null");
    }

    try
    {
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
    catch (const std::exception& error)
    {
        return MigrationFailure("serialization.migration.exception", "migration metadata callback threw", error.what());
    }
    catch (...)
    {
        return MigrationFailure("serialization.migration.exception", "migration metadata callback threw an unknown exception");
    }
}

foundation::Result<void> MigrationRegistry::Freeze()
{
    frozen_ = true;
    return foundation::Result<void>::Success();
}

bool MigrationRegistry::IsFrozen() const noexcept
{
    return frozen_;
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

    struct WorkItem
    {
        SchemaVersion current{};
        MigrationPath path{};
        std::vector<SchemaVersion> visited{};
    };

    std::vector<WorkItem> stack;
    try
    {
        stack.push_back(WorkItem{from, {}, {from}});
    }
    catch (const std::bad_alloc&)
    {
        return MigrationFailureValue<MigrationPath>("serialization.out_of_memory", "migration path search could not allocate work stack");
    }

    std::vector<MigrationPath> paths;
    bool cycle_detected = false;
    while (!stack.empty() && paths.size() <= 1)
    {
        WorkItem item = std::move(stack.back());
        stack.pop_back();

        for (const auto& [key, migration] : migrations_)
        {
            if (key.type_id != type_id || !(key.from == item.current))
            {
                continue;
            }

            if (ContainsVersion(item.visited, key.to))
            {
                cycle_detected = true;
                continue;
            }

            try
            {
                MigrationPath next_path = item.path;
                next_path.push_back(migration);
                if (key.to == to)
                {
                    paths.push_back(std::move(next_path));
                }
                else
                {
                    std::vector<SchemaVersion> next_visited = item.visited;
                    next_visited.push_back(key.to);
                    stack.push_back(WorkItem{key.to, std::move(next_path), std::move(next_visited)});
                }
            }
            catch (const std::bad_alloc&)
            {
                return MigrationFailureValue<MigrationPath>("serialization.out_of_memory", "migration path search could not allocate path state");
            }
        }
    }

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
