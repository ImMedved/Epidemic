#pragma once

#include "Epidemic/Foundation/string_id.h"
#include "Epidemic/Runtime/Serialization/schema_version.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

namespace epidemic::runtime
{
enum class SerializationState
{
    SchemaUnknown,
    SchemaKnown,
    Reading,
    Writing,
    Migrating,
    Valid,
    Invalid,
    Failed,
};

class ISerializer
{
  public:
    // Function note: Handles ~iserializer.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    virtual ~ISerializer() = default;

    // Function note: Gets type id.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual foundation::StringId GetTypeId() const = 0;
    // Function note: Gets schema version.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual SchemaVersion GetSchemaVersion() const = 0;
};
} 
