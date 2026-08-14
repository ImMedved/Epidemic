#include "Epidemic/GameFramework/NarrativeIntegration/narrative_adapters.h"

namespace epidemic::gameplay::narrative_integration
{
foundation::Result<void> NarrativeSemanticEventAdapter::ProcessDiscovery(narrative::NarrativeService& service,GameplayObjectRef discoverer,GameplayObjectRef subject,GameplayObjectRef area,TypeId topic,GameplayContext context) const
{
    narrative::ClueRecord clue;
    clue.owner=discoverer;
    clue.topic=topic;
    clue.area=area;
    clue.confidence=1'000'000;
    auto discovered=service.DiscoverClue(clue,context);
    if(!discovered)return foundation::Result<void>::Failure(discovered.GetError());
    narrative::NarrativeEvent event;
    event.type=narrative::NarrativeEventTypeId::FromString("narrative.discovery");
    event.subject=subject;
    event.instigator=discoverer;
    event.area=area;
    event.time=context.time;
    event.correlation=context.correlation;
    event.tags.Add(TagId::FromString("narrative.discovery"));
    return service.ProcessNarrativeEvent(event);
}

foundation::Result<void> NarrativeSemanticEventAdapter::ProcessWorldEvent(narrative::NarrativeService& service,GameplayObjectRef subject,GameplayObjectRef area,TagId event_tag,GameplayContext context) const
{
    narrative::NarrativeEvent event;
    event.type=narrative::NarrativeEventTypeId::FromString("narrative.world_event");
    event.subject=subject;
    event.instigator=context.actor;
    event.area=area;
    event.time=context.time;
    event.correlation=context.correlation;
    event.tags.Add(event_tag);
    return service.ProcessNarrativeEvent(event);
}

foundation::Result<void> NarrativeSemanticEventAdapter::ProcessEncounterCompleted(narrative::NarrativeService& service,GameplayObjectRef encounter,GameplayObjectRef actor,GameplayObjectRef area,GameplayContext context) const
{
    narrative::NarrativeEvent event;
    event.type=narrative::NarrativeEventTypeId::FromString("narrative.encounter_completed");
    event.subject=encounter;
    event.instigator=actor;
    event.area=area;
    event.time=context.time;
    event.correlation=context.correlation;
    event.tags.Add(TagId::FromString("encounter.completed"));
    return service.ProcessNarrativeEvent(event);
}

narrative::NarrativeConsequenceResult NarrativeConsequenceRecorder::Execute(const narrative::NarrativeConsequenceDefinition& definition,const narrative::NarrativeConsequenceExecution& execution,const narrative::NarrativeExecutionContext& context) const
{
    recorded_.push_back(RecordedNarrativeConsequence{execution.id,definition.id,execution.thread,context.default_owner});
    return {narrative::ConsequenceExecutionState::Applied,execution.revision};
}

foundation::Result<narrative::ClueId> NarrativeKnowledgeAdapter::CreateClueFromKnowledge(narrative::NarrativeService& service,GameplayObjectRef owner,TypeId topic,GameplayObjectRef area,std::int64_t confidence,GameplayContext context) const
{
    narrative::ClueRecord clue;
    clue.owner=owner;
    clue.topic=topic;
    clue.area=area;
    clue.confidence=confidence;
    return service.DiscoverClue(clue,context);
}

foundation::Result<narrative::RumorId> NarrativeKnowledgeAdapter::CreateRumorFromSharedKnowledge(narrative::NarrativeService& service,GameplayObjectRef scope,TypeId topic,std::int64_t confidence,GameplayDuration lifetime,GameplayContext context) const
{
    narrative::RumorRecord rumor;
    rumor.owner_or_scope=scope;
    rumor.topic=topic;
    rumor.confidence=confidence;
    rumor.expires_at=context.time+lifetime;
    return service.CreateRumor(rumor,context);
}
} // namespace epidemic::gameplay::narrative_integration
