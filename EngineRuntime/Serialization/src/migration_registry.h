#pragma once

#include "Epidemic/Runtime/Serialization/migration_registry.h"

#include <unordered_map>

namespace epidemic::runtime
{
class MigrationRegistry final : public IMigrationRegistry
{
  public:
    [[nodiscard]] foundation::Result<void> RegisterMigration(std::shared_ptr<const IMigration> migration) override;
    [[nodiscard]] foundation::Result<void> Freeze() override;
    [[nodiscard]] bool IsFrozen() const noexcept override;
    [[nodiscard]] std::shared_ptr<const IMigration> FindMigration(const MigrationKey& key) const override;
    [[nodiscard]] bool HasMigration(const MigrationKey& key) const override;
    [[nodiscard]] foundation::Result<std::vector<std::shared_ptr<const IMigration>>> FindMigrationPath(
        foundation::StringId type_id,
        SchemaVersion from,
        SchemaVersion to) const override;

  private:
    std::unordered_map<MigrationKey, std::shared_ptr<const IMigration>> migrations_;
    bool frozen_ = false;
};
} // namespace epidemic::runtime
