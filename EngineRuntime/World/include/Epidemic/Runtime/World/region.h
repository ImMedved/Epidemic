#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Foundation/string_id.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

#include <optional>

namespace epidemic::runtime
{
struct RegionDescriptor
{
    RegionId id{};
    foundation::StringId name{};

    [[nodiscard]] constexpr bool operator==(const RegionDescriptor&) const noexcept = default;
};

class IRegionRegistry
{
  public:
    // Function note: Handles ~iregion registry.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    virtual ~IRegionRegistry() = default;

    // Function note: Registers region.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual foundation::Result<void> RegisterRegion(RegionDescriptor region) = 0;
    // Function note: Finds region.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual std::optional<RegionDescriptor> FindRegion(RegionId id) const = 0;
};
} 
