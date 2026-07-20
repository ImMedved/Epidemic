#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/World/world_commands.h"
#include "Epidemic/Runtime/World/world_object.h"

namespace epidemic::runtime
{
class IWorldObjectRegistry
{
  public:
    virtual ~IWorldObjectRegistry() = default;

    [[nodiscard]] virtual foundation::Result<WorldCommandResult> Apply(const CreateObjectCommand& command) = 0;
    [[nodiscard]] virtual foundation::Result<WorldCommandResult> Apply(const ChangePlacementCommand& command) = 0;
    [[nodiscard]] virtual foundation::Result<WorldCommandResult> Apply(const ChangeResidencyCommand& command) = 0;
    [[nodiscard]] virtual foundation::Result<WorldCommandResult> Apply(const PromotePersistenceTierCommand& command) = 0;
    [[nodiscard]] virtual foundation::Result<WorldCommandResult> Apply(const DestroyObjectCommand& command) = 0;
};
} 
