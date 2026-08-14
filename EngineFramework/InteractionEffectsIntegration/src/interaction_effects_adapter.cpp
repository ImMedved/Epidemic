#include "Epidemic/GameFramework/InteractionEffectsIntegration/interaction_effects_adapter.h"
namespace epidemic::gameplay::interaction_effects_integration
{
foundation::Result<void> InteractionEffectExecutor::Validate(const interaction::InteractionPlan& plan)const{if(!effects_.FindDefinition(definition_))return foundation::Result<void>::Failure(foundation::Error::Create("gameplay.interaction.effect_definition_missing","interaction effect definition is missing"));if(!plan.candidate.target.IsValid())return foundation::Result<void>::Failure(foundation::Error::Create("gameplay.interaction.target_invalid","interaction target is invalid"));return foundation::Result<void>::Success();}
foundation::Result<void> InteractionEffectExecutor::Commit(const interaction::InteractionPlan&plan,interaction::InteractionExecutionId){effects::EffectRequest r;r.definition=definition_;r.source=plan.context.actor;r.instigator=plan.context.actor;r.targets={plan.context.target};r.context=plan.context.gameplay;auto x=effects_.Execute(std::move(r));if(!x)return foundation::Result<void>::Failure(x.GetError());return foundation::Result<void>::Success();}
}
