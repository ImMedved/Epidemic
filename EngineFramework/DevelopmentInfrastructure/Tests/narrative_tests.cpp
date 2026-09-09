#include "Epidemic/GameFramework/Narrative/narrative.h"
#include <cstdlib>
#include <iostream>
#include <limits>

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
    journal.journal_template = NarrativeConsequenceDefinition::JournalTemplate{
        .type = JournalEntryTypeId::FromString("journal.narrative"),
        .visibility = JournalVisibilityState::Discovered,
        .payload = {}};
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
    Check(!snapshot.activated_beats.empty(),"activated beats explicitly persisted");
    NarrativeService restored;
    Check(static_cast<bool>(restored.RegisterConditionResolver(NarrativeConditionTypeId::FromString("condition.event_tag"),tag_resolver)),"restored resolver");
    Check(static_cast<bool>(restored.RegisterConsequenceHandler(NarrativeConsequenceTypeId::FromString("narrative.reward"),reward_handler)),"restored handler");
    Check(static_cast<bool>(restored.RegisterConditionDefinition(cond)),"restored condition definition");
    Check(static_cast<bool>(restored.RegisterConsequenceDefinition(reward)),"restored reward definition");
    Check(static_cast<bool>(restored.RegisterConsequenceDefinition(journal)),"restored journal definition");
    Check(static_cast<bool>(restored.RegisterBeatDefinition(b)),"restored beat definition");
    Check(static_cast<bool>(restored.RegisterObjectiveDefinition(obj)),"restored objective definition");
    Check(static_cast<bool>(restored.RegisterThreadDefinition(t)),"restored thread definition");
    Check(static_cast<bool>(restored.FreezeDefinitions()),"restored current-build definitions frozen");
    Check(static_cast<bool>(restored.RestoreSnapshot(snapshot)),"restore snapshot");
    auto pending_after_restore=restored.FindConsequences(ConsequenceExecutionState::Pending);
    Check(pending_after_restore.empty(),"no pending reward after restore");
    Check(restored.GetJournal(player).size()==1,"journal restored");
    Check(static_cast<bool>(restored.ProcessNarrativeEvent(event)),"dedupe restored event");
    Check(restored.FindConsequences(ConsequenceExecutionState::Pending).empty(),"dedupe prevents duplicated consequences");
    auto corrupt_snapshot = snapshot;
    corrupt_snapshot.journal_ids.scope ^= 1u;
    Check(!static_cast<bool>(restored.RestoreSnapshot(corrupt_snapshot)),"invalid generator scope rejected");
    Check(restored.GetJournal(player).size()==1,"failed restore leaves current narrative state unchanged");

    NarrativeService cyclic;
    NarrativeConditionDefinition cycle_a;
    cycle_a.id = NarrativeConditionId::FromString("condition.cycle_a");
    cycle_a.all_of.push_back(NarrativeConditionId::FromString("condition.cycle_b"));
    NarrativeConditionDefinition cycle_b;
    cycle_b.id = NarrativeConditionId::FromString("condition.cycle_b");
    cycle_b.any_of.push_back(cycle_a.id);
    Check(static_cast<bool>(cyclic.RegisterConditionDefinition(cycle_a)),"cycle a registered");
    Check(static_cast<bool>(cyclic.RegisterConditionDefinition(cycle_b)),"cycle b registered");
    Check(!static_cast<bool>(cyclic.FreezeDefinitions()),"condition cycle rejected at freeze");

    NarrativeService resumable;
    Check(static_cast<bool>(resumable.RegisterConditionResolver(NarrativeConditionTypeId::FromString("condition.event_tag"),tag_resolver)),"resumable resolver");
    Check(static_cast<bool>(resumable.RegisterConsequenceHandler(NarrativeConsequenceTypeId::FromString("narrative.reward"),reward_handler)),"resumable handler");
    auto r_thread = NarrativeThreadId::FromString("thread.resumable");
    auto r_beat = NarrativeBeatId::FromString("beat.resumable");
    auto r_condition = NarrativeConditionId::FromString("condition.resumable");
    auto r_consequence = NarrativeConsequenceId::FromString("consequence.resumable");
    NarrativeConditionDefinition r_cond;
    r_cond.id = r_condition;
    r_cond.type = NarrativeConditionTypeId::FromString("condition.event_tag");
    r_cond.payload = Bytes("world.bridge.destroyed");
    NarrativeConsequenceDefinition r_cons;
    r_cons.id = r_consequence;
    r_cons.type = NarrativeConsequenceTypeId::FromString("narrative.reward");
    NarrativeBeatDefinition r_b;
    r_b.id = r_beat;
    r_b.thread = r_thread;
    r_b.activation_conditions.push_back(r_condition);
    r_b.consequences.push_back(r_consequence);
    NarrativeThreadDefinition r_t;
    r_t.id = r_thread;
    r_t.beats.push_back(r_beat);
    Check(static_cast<bool>(resumable.RegisterConditionDefinition(r_cond)),"resumable condition");
    Check(static_cast<bool>(resumable.RegisterConsequenceDefinition(r_cons)),"resumable consequence");
    Check(static_cast<bool>(resumable.RegisterBeatDefinition(r_b)),"resumable beat");
    Check(static_cast<bool>(resumable.RegisterThreadDefinition(r_t)),"resumable thread");
    Check(static_cast<bool>(resumable.FreezeDefinitions()),"resumable freeze");
    NarrativeEvent resumable_event = event;
    resumable_event.correlation = CorrelationId::FromString("event.resumable");
    Check(static_cast<bool>(resumable.ProcessNarrativeEvent(resumable_event,
          {.max_condition_evaluations=32,.max_storylets_evaluated=8,.max_storylets_activated=8,.max_consequences_planned=0})),
          "budget stop is resumable success");
    Check(resumable.FindConsequences(ConsequenceExecutionState::Pending).empty(),"budget stop does not partially plan consequence");
    NarrativeFlag epoch_flag_a; epoch_flag_a.id = NarrativeFlagId::FromString("test.epoch.a"); epoch_flag_a.value = true;
    NarrativeFlag epoch_flag_b; epoch_flag_b.id = NarrativeFlagId::FromString("test.epoch.b"); epoch_flag_b.value = true;
    Check(static_cast<bool>(resumable.SetFlag(epoch_flag_a)),"first pre-restore narrative journal change");
    Check(static_cast<bool>(resumable.SetFlag(epoch_flag_b)),"second pre-restore narrative journal change");
    const auto pre_restore_cursor = resumable.LatestChangeCursor();
    Check(pre_restore_cursor.sequence >= 2,"pre-restore narrative journal has multiple changes");
    auto pending_snapshot = resumable.CaptureSnapshot();
    Check(pending_snapshot.event_executions.size()==1 &&
          pending_snapshot.event_executions.front().state==NarrativeEventExecutionState::Pending,
          "pending event execution persisted");
    NarrativeService resumed;
    Check(static_cast<bool>(resumed.RegisterConditionResolver(NarrativeConditionTypeId::FromString("condition.event_tag"),tag_resolver)),"resumed resolver");
    Check(static_cast<bool>(resumed.RegisterConsequenceHandler(NarrativeConsequenceTypeId::FromString("narrative.reward"),reward_handler)),"resumed handler");
    Check(static_cast<bool>(resumed.RegisterConditionDefinition(r_cond)),"resumed condition");
    Check(static_cast<bool>(resumed.RegisterConsequenceDefinition(r_cons)),"resumed consequence");
    Check(static_cast<bool>(resumed.RegisterBeatDefinition(r_b)),"resumed beat");
    Check(static_cast<bool>(resumed.RegisterThreadDefinition(r_t)),"resumed thread");
    Check(static_cast<bool>(resumed.FreezeDefinitions()),"resumed freeze");
    Check(static_cast<bool>(resumed.RestoreSnapshot(pending_snapshot)),"restore pending event");
    Check(resumed.ReadChangesSince(pre_restore_cursor).snapshot_required,"pre-restore narrative cursor requires snapshot");
    Check(resumed.ReadChangesSince(resumed.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required,"future narrative cursor incompatible");
    Check(static_cast<bool>(resumed.ProcessNarrativeEvent(resumable_event)),"resume event after restore");
    Check(resumed.FindConsequences(ConsequenceExecutionState::Pending).size()==1,"resumed event plans consequence exactly once");
    Check(static_cast<bool>(resumed.ProcessNarrativeEvent(resumable_event)),"completed event deduplicates");
    Check(resumed.FindConsequences(ConsequenceExecutionState::Pending).size()==1,"dedupe does not duplicate resumed consequence");
    Check(resumed.ReadChangesSince(pre_restore_cursor).snapshot_required,"old narrative cursor remains incompatible after new changes");
    const auto narrative_epoch = resumed.ReadChangesSince(ChangeCursor{});
    Check(!narrative_epoch.snapshot_required && !narrative_epoch.changes.empty(),"new narrative epoch readable from zero");
    const auto narrative_current = resumed.ReadChangesSince(narrative_epoch.latest_cursor);
    Check(!narrative_current.snapshot_required && narrative_current.changes.empty(),"exact narrative cursor current");

    Check(!static_cast<bool>(resumed.CompleteThread(NarrativeThreadId::FromString("thread.unknown"))),
          "mutation cannot create phantom thread state");

    NarrativeService bounded;
    for (int i=0;i<4200;++i)
    {
        NarrativeFlag flag;
        flag.id = NarrativeFlagId::FromRaw(0x9911, static_cast<std::uint64_t>(i+1));
        Check(static_cast<bool>(bounded.SetFlag(flag)),"bounded journal mutation");
    }
    Check(bounded.ReadChangesSince(ChangeCursor{}).snapshot_required,"bounded narrative journal reports snapshot-required gap");
    Check(bounded.ReadChangesSince(bounded.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required,"max narrative cursor never wraps");
    NarrativeService empty_journal;
    Check(empty_journal.ReadChangesSince(empty_journal.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required,"max narrative cursor incompatible with empty journal");
    return 0;
}
