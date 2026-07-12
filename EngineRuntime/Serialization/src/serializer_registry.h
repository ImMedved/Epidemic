#pragma once

#include "Epidemic/Runtime/Serialization/serializer_registry.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

#include <unordered_map>

namespace epidemic::runtime
{
class SerializerRegistry final : public ISerializerRegistry
{
  public:
    // Function note: Registers serializer.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] foundation::Result<void> RegisterSerializer(ISerializer& serializer) override;
    // Function note: Finds serializer.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] ISerializer* FindSerializer(foundation::StringId type_id) override;
    // Function note: Finds serializer.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] const ISerializer* FindSerializer(foundation::StringId type_id) const override;
    // Function note: Checks serializer.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] bool HasSerializer(foundation::StringId type_id) const override;

  private:
    std::unordered_map<foundation::StringId, ISerializer*> serializers_;
};
} 
