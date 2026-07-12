#pragma once

#include "Epidemic/Runtime/Serialization/migration_registry.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

#include <unordered_map>

namespace epidemic::runtime
{
class MigrationRegistry final : public IMigrationRegistry
{
  public:
    // Function note: Registers migration.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] foundation::Result<void> RegisterMigration(IMigration& migration) override;
    // Function note: Finds migration.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] IMigration* FindMigration(const MigrationKey& key) override;
    // Function note: Finds migration.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] const IMigration* FindMigration(const MigrationKey& key) const override;
    // Function note: Checks migration.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] bool HasMigration(const MigrationKey& key) const override;

  private:
    std::unordered_map<MigrationKey, IMigration*> migrations_;
};
} 
