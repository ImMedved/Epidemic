#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Persistence/lazy_rule_record.h"
#include "Epidemic/Runtime/Persistence/persistence_location.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

#include <optional>
#include <vector>

namespace epidemic::runtime
{
struct ZoneOverrideSnapshot
{
    PersistenceLocation location{};
    std::vector<PersistentObjectId> record_ids;
    std::vector<PersistentObjectId> tombstones;
    std::vector<LazyRuleRecord> lazy_rules;
};

class IZoneOverrideStore
{
  public:
    // Function note: Handles ~izone override store.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    virtual ~IZoneOverrideStore() = default;

    // Function note: Handles upsert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual foundation::Result<void> Upsert(ZoneOverrideSnapshot snapshot) = 0;
    // Function note: Finds the associated runtime state.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual std::optional<ZoneOverrideSnapshot> Find(const PersistenceLocation& location) const = 0;
    // Function note: Handles remove.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual foundation::Result<void> Remove(const PersistenceLocation& location) = 0;
};
} 
