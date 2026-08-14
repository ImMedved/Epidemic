#pragma once
#include "Epidemic/GameFramework/Effects/effects.h"
#include "Epidemic/GameFramework/Interaction/interaction.h"
namespace epidemic::gameplay::interaction_effects_integration
{
class InteractionEffectExecutor final : public interaction::IInteractionExecutor
{
public:
    InteractionEffectExecutor(effects::EffectService& effects,effects::EffectDefinitionId definition):effects_(effects),definition_(definition){}
    [[nodiscard]] foundation::Result<void> Validate(const interaction::InteractionPlan& plan)const override;
    [[nodiscard]] foundation::Result<void> Commit(const interaction::InteractionPlan& plan,interaction::InteractionExecutionId execution)override;
private: effects::EffectService&effects_;effects::EffectDefinitionId definition_{};
};
}
