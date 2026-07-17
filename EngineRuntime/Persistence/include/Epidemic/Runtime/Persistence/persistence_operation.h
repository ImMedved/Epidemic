#pragma once

#include "Epidemic/Runtime/Persistence/lazy_rule_record.h"
#include "Epidemic/Runtime/Persistence/persistent_record.h"
#include "Epidemic/Runtime/Persistence/tombstone_store.h"
#include "Epidemic/Runtime/Persistence/zone_override_store.h"

#include <variant>

namespace epidemic::runtime
{
struct UpsertObjectOperation
{
    PersistentObjectRecord record{};
};

struct RemoveObjectOperation
{
    PersistentObjectId id{};
};

struct DeleteObjectOperation
{
    TombstoneRecord tombstone{};
};

struct AddTombstoneOperation
{
    TombstoneRecord tombstone{};
};

struct UpsertLazyRuleOperation
{
    LazyRuleRecord record{};
};

struct RemoveLazyRuleOperation
{
    LazyRuleId id{};
};

struct UpsertZoneOverrideOperation
{
    ZoneOverrideSnapshot snapshot{};
};

struct RemoveZoneOverrideOperation
{
    PersistenceLocation location{};
};

using PersistenceOperation = std::variant<
    UpsertObjectOperation,
    RemoveObjectOperation,
    DeleteObjectOperation,
    AddTombstoneOperation,
    UpsertLazyRuleOperation,
    RemoveLazyRuleOperation,
    UpsertZoneOverrideOperation,
    RemoveZoneOverrideOperation>;
} // namespace epidemic::runtime
