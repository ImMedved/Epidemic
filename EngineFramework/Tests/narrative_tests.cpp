#include "Epidemic/GameFramework/Narrative/narrative.h"
#include <cstdlib>
#include <iostream>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::narrative;

namespace
{
void Check(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
GameplayObjectRef Ref(const char* domain,const char* id){return {GameplayDomainId::FromString(domain),GameplayObjectId::FromString(id)};}

class EventTagResolver final:public INarrativeConditionResolver
{
public:
    NarrativeConditionResult Evaluate(const NarrativeConditionDefinition& definition,const NarrativeEvaluationContext& context) const override
    {
        NarrativeConditionResult result;
        result.condition=definition.id;
        const auto required=definition.payload.empty()?TagId::FromString("event.match"):TagId::FromString(std::string_view(reinterpret_cast<const char*>(definition.payload.data()), definition.payload.size()));
        result.state=context.event.tags.HasExact(required)?ConditionEvaluationState::Satisfied:ConditionEvaluationState::Unsatisfied;
        result.score=result.state==ConditionEvaluationState::Satisfied?1'000'000:0;
        return result;
    }
};
class RecordingHandler final:public INarrativeConsequenceHandler
{
public:
    NarrativeConsequenceResult Execute(const NarrativeConsequenceDefinition&,const NarrativeConsequenceExecution&,const NarrativeExecutionContext&) const override
    {
        ++count;
        return {ConsequenceExecutionState::Applied,{}};
    }
    mutable int count=0;
};
std::vector<std::byte> Bytes(const char* s){std::vector<std::byte> out;while(*s){out.push_back(static_cast<std::byte>(*s));++s;}return out;}
}

int main()
{
    NarrativeService service;
    EventTagResolver tag_resolver;
    RecordingHandler reward_handler;
    Check(static_cast<bool>(service.RegisterConditionResolver(NarrativeConditionTypeId::FromString("condition.event_tag"),tag_resolver)),"register condition resolver");
    Check(static_cast<bool>(service.RegisterConsequenceHandler(NarrativeConsequenceTypeId::FromString("narrative.reward"),reward_handler)),"register consequence handler");

    auto thread= NarrativeThreadId::FromString("thread.bridge");
    auto objective= NarrativeObjectiveId::FromString("objective.inspect_bridge");
    auto beat= NarrativeBeatId::FromString("beat.bridge_discovered");
    auto condition= NarrativeConditionId::FromString("condition.bridge_destroyed_seen");
    auto consequence= NarrativeConsequenceId::FromString("consequence.reward");
    auto journal_consequence= NarrativeConsequenceId::FromString("consequence.journal");

    NarrativeConditionDefinition cond;
    cond.id=condition;
    cond.type=NarrativeConditionTypeId::FromString("condition.event_tag");
    cond.payload=Bytes("world.bridge.destroyed");
    Check(static_cast<bool>(service.RegisterConditionDefinition(cond)),"register condition");

    NarrativeConsequenceDefinition reward;
    reward.id=consequence;
    reward.type=NarrativeConsequenceTypeId::FromString("narrative.reward");
    reward.priority=20;
    Check(static_cast<bool>(service.RegisterConsequenceDefinition(reward)),"register reward consequence");
    NarrativeConsequenceDefinition journal;
    journal.id=journal_consequence;
    journal.type=NarrativeConsequenceTypeId::FromString("narrative.add_journal");
    journal.priority=10;
    Check(static_cast<bool>(service.RegisterConsequenceDefinition(journal)),"register journal consequence");

    NarrativeBeatDefinition b;
    b.id=beat;
    b.thread=thread;
    b.activation_conditions.push_back(condition);
    b.completion_conditions.push_back(condition);
    b.consequences={consequence,journal_consequence};
    Check(static_cast<bool>(service.RegisterBeatDefinition(b)),"register beat");

    NarrativeObjectiveDefinition obj;
    obj.id=objective;
    obj.thread=thread;
    obj.start_conditions.push_back(condition);
    obj.completion_conditions.push_back(condition);
    obj.player_visible=true;
    Check(static_cast<bool>(service.RegisterObjectiveDefinition(obj)),"register objective");

    NarrativeThreadDefinition t;
    t.id=thread;
    t.beats.push_back(beat);
    t.root_objectives.push_back(objective);
    t.initial_state=NarrativeRuntimeState::Hidden;
    Check(static_cast<bool>(service.RegisterThreadDefinition(t)),"register thread");
    Check(static_cast<bool>(service.FreezeDefinitions()),"freeze definitions");
    Check(!static_cast<bool>(service.RegisterThreadDefinition(t)),"frozen rejects duplicate/additional registration");

    auto player=Ref("game.actor","player");
    auto bridge=Ref("world.feature","bridge");
    auto area=Ref("world.area","north_road");
    NarrativeEvent hidden_event;
    hidden_event.type=NarrativeEventTypeId::FromString("world.alteration");
    hidden_event.subject=bridge;
    hidden_event.area=area;
    hidden_event.instigator={};
    hidden_event.time=GameplayTimePoint{10};
    hidden_event.correlation=CorrelationId::FromString("event.hidden");
    hidden_event.tags.Add(TagId::FromString("world.house.destroyed"));
    Check(static_cast<bool>(service.ProcessNarrativeEvent(hidden_event)),"process irrelevant hidden event");
    Check(service.GetJournal(player).empty(),"no omniscient journal for unrelated/unseen state");

    NarrativeEvent event;
    event.type=NarrativeEventTypeId::FromString("world.alteration.discovered");
    event.subject=bridge;
    event.instigator=player;
    event.area=area;
    event.time=GameplayTimePoint{20};
    event.correlation=CorrelationId::FromString("event.bridge.discovery");
    event.tags.Add(TagId::FromString("world.bridge.destroyed"));
    Check(static_cast<bool>(service.ProcessNarrativeEvent(event)),"process discovery event");
    const auto* thread_state=service.GetThreadState(thread);
    Check(thread_state!=nullptr&&thread_state->state==NarrativeRuntimeState::Active,"thread active after discovery");
    const auto* objective_state=service.GetObjectiveState(objective);
    Check(objective_state!=nullptr&&objective_state->state==NarrativeObjectiveRuntimeState::Completed,"objective completed");
    Check(service.FindConsequences(ConsequenceExecutionState::Pending).size()==2,"two consequences planned");

    auto applied=service.ExecutePendingConsequences({.default_owner=player,.now=GameplayTimePoint{25}},16);
    Check(applied.size()==2,"two consequences applied");
    Check(reward_handler.count==1,"external reward handler called once");
    Check(service.GetJournal(player).size()==1,"builtin journal consequence added entry");

    auto snapshot=service.CaptureSnapshot();
    NarrativeService restored;
    Check(static_cast<bool>(restored.RegisterConditionResolver(NarrativeConditionTypeId::FromString("condition.event_tag"),tag_resolver)),"restored resolver");
    Check(static_cast<bool>(restored.RegisterConsequenceHandler(NarrativeConsequenceTypeId::FromString("narrative.reward"),reward_handler)),"restored handler");
    Check(static_cast<bool>(restored.RestoreSnapshot(snapshot)),"restore snapshot");
    auto pending_after_restore=restored.FindConsequences(ConsequenceExecutionState::Pending);
    Check(pending_after_restore.empty(),"no pending reward after restore");
    Check(restored.GetJournal(player).size()==1,"journal restored");
    Check(static_cast<bool>(restored.ProcessNarrativeEvent(event)),"dedupe restored event");
    Check(restored.FindConsequences(ConsequenceExecutionState::Pending).empty(),"dedupe prevents duplicated consequences");
    return 0;
}
