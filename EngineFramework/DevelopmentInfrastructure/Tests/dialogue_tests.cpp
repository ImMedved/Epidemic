#include "Epidemic/GameFramework/Dialogue/dialogue.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::dialogue;
namespace
{
void Check(bool v, const char *m)
{
    if (!v)
    {
        std::cerr << m << '\n';
        std::exit(1);
    }
}
GameplayObjectRef Ref(const char *d, const char *i)
{
    return {GameplayDomainId::FromString(d), GameplayObjectId::FromString(i)};
}
class Always final : public IDialogueConditionResolver
{
  public:
    DialogueConditionResult Evaluate(const DialogueConditionDefinition &,
                                     const DialogueConditionContext &) const override
    {
        return {DialogueConditionState::Satisfied};
    }
};
class Handler final : public IDialogueConsequenceHandler
{
  public:
    mutable int count = 0;
    DialogueConsequenceState Execute(const DialogueConsequenceDefinition &, const DialogueConsequenceExecution &,
                                     const ConversationSession &) const override
    {
        ++count;
        return DialogueConsequenceState::Applied;
    }
};
class DeferredHandler final : public IDialogueConsequenceHandler
{
  public:
    mutable int count = 0;
    mutable bool apply = false;
    DialogueConsequenceState Execute(const DialogueConsequenceDefinition &, const DialogueConsequenceExecution &,
                                     const ConversationSession &) const override
    {
        ++count;
        return apply ? DialogueConsequenceState::Applied : DialogueConsequenceState::Deferred;
    }
};
class ThrowingCondition final : public IDialogueConditionResolver
{
  public:
    DialogueConditionResult Evaluate(const DialogueConditionDefinition &, const DialogueConditionContext &) const override
    {
        throw 7;
    }
};
class ThrowingHandler final : public IDialogueConsequenceHandler
{
  public:
    DialogueConsequenceState Execute(const DialogueConsequenceDefinition &, const DialogueConsequenceExecution &,
                                     const ConversationSession &) const override
    {
        throw 9;
    }
};
} // namespace
int main()
{
    DialogueService s;
    Always cond;
    Handler handler;
    auto cond_type = DialogueConditionTypeId::FromString("condition.always");
    auto cons_type = DialogueConsequenceTypeId::FromString("consequence.test");
    Check(static_cast<bool>(s.RegisterConditionResolver(cond_type, cond)), "resolver");
    Check(static_cast<bool>(s.RegisterConsequenceHandler(cons_type, handler)), "handler");
    TypeId condition = TypeId::FromString("dialogue.condition.available");
    TypeId consequence = TypeId::FromString("dialogue.consequence.record");
    Check(static_cast<bool>(s.RegisterCondition({condition, cond_type, {}})), "condition");
    Check(static_cast<bool>(s.RegisterConsequence({consequence, cons_type, {}, 10})), "consequence");
    ConversationDefinition d;
    d.canonical_name = "conversation.test";
    d.entry_node = DialogueNodeId::FromString("node.start");
    DialogueNodeDefinition start;
    start.id = d.entry_node;
    DialogueOptionDefinition option;
    option.id = DialogueOptionId::FromString("option.ask");
    option.conditions.push_back(condition);
    option.consequences.push_back(consequence);
    option.next_node = DialogueNodeId::FromString("node.end");
    start.options.push_back(option);
    DialogueNodeDefinition end;
    end.id = option.next_node;
    // Keep one consequence on the option and one on the destination node. This
    // lets the exhaustion regression below prove that destination consequences
    // prepared during preflight are reused during commit instead of allocating twice.
    end.consequences.push_back(consequence);
    d.nodes = {start, end};
    auto did = s.RegisterConversation(d);
    ConversationDefinition terminal;
    terminal.canonical_name = "conversation.terminal";
    terminal.entry_node = DialogueNodeId::FromString("node.terminal");
    DialogueNodeDefinition terminal_node;
    terminal_node.id = terminal.entry_node;
    terminal.nodes = {terminal_node};
    auto terminal_id = s.RegisterConversation(terminal);
    Check(static_cast<bool>(did), "conversation def");
    Check(static_cast<bool>(terminal_id), "terminal conversation def");
    Check(static_cast<bool>(s.FreezeDefinitions()), "freeze");
    auto player = Ref("actor", "player"), npc = Ref("actor", "npc");
    auto session = s.StartConversation(did.Value(), {npc, player});
    Check(static_cast<bool>(session), "start");
    Check(s.GetAvailableOptions(session.Value(), player).size() == 1, "option visible");
    Check(static_cast<bool>(s.SelectOption(session.Value(), option.id, player)), "select option");
    Check(s.GetSession(session.Value())->state == ConversationState::Completed, "conversation completed");
    auto applied = s.ExecutePendingConsequences();
    Check(applied.size() == 2 && handler.count == 2, "option and destination consequences executed once");
    auto terminal_session = s.StartConversation(terminal_id.Value(), {npc, player});
    Check(static_cast<bool>(terminal_session), "start terminal conversation");
    Check(s.GetDiagnostics().active_sessions == 0, "terminal conversation not counted active");
    auto snap = s.CaptureSnapshot();
    Check(snap.sessions.empty(), "terminal dialogue omitted from snapshot");
    DialogueService restored;
    Check(static_cast<bool>(restored.RegisterConditionResolver(cond_type, cond)), "restored resolver");
    Check(static_cast<bool>(restored.RegisterConsequenceHandler(cons_type, handler)), "restored handler");
    Check(static_cast<bool>(restored.RegisterCondition({condition, cond_type, {}})), "restored condition");
    Check(static_cast<bool>(restored.RegisterConsequence({consequence, cons_type, {}, 10})), "restored consequence");
    Check(static_cast<bool>(restored.RegisterConversation(d)), "restored conversation def");
    Check(static_cast<bool>(restored.FreezeDefinitions()), "restored freeze");
    Check(static_cast<bool>(restored.RestoreSnapshot(std::move(snap))), "restore dialogue");
    Check(!restored.GetSession(session.Value()).has_value(), "terminal session not restored");

    // A terminal node with still-pending consequences must remain restorable.
    DialogueService pending_restore_source;
    Handler pending_handler;
    Check(static_cast<bool>(pending_restore_source.RegisterConsequenceHandler(cons_type, pending_handler)), "pending restore handler");
    Check(static_cast<bool>(pending_restore_source.RegisterConsequence({consequence, cons_type, {}, 10})), "pending restore consequence");
    ConversationDefinition pending_def;
    pending_def.canonical_name = "conversation.pending.restore";
    pending_def.entry_node = DialogueNodeId::FromString("node.pending.restore");
    DialogueNodeDefinition pending_node;
    pending_node.id = pending_def.entry_node;
    pending_node.consequences.push_back(consequence);
    pending_def.nodes.push_back(pending_node);
    auto pending_def_id = pending_restore_source.RegisterConversation(pending_def);
    Check(static_cast<bool>(pending_def_id), "pending restore definition");
    Check(static_cast<bool>(pending_restore_source.FreezeDefinitions()), "pending restore freeze");
    auto pending_session = pending_restore_source.StartConversation(pending_def_id.Value(), {npc, player});
    Check(static_cast<bool>(pending_session), "pending restore session");
    auto pending_snapshot = pending_restore_source.CaptureSnapshot();
    Check(pending_snapshot.sessions.size() == 1 && pending_snapshot.consequences.size() == 1, "pending terminal session retained in snapshot");
    DialogueService pending_restored;
    Check(static_cast<bool>(pending_restored.RegisterConsequenceHandler(cons_type, pending_handler)), "pending restored handler");
    Check(static_cast<bool>(pending_restored.RegisterConsequence({consequence, cons_type, {}, 10})), "pending restored consequence");
    Check(static_cast<bool>(pending_restored.RegisterConversation(pending_def)), "pending restored definition");
    Check(static_cast<bool>(pending_restored.FreezeDefinitions()), "pending restored freeze");
    Check(static_cast<bool>(pending_restored.RestoreSnapshot(std::move(pending_snapshot))), "pending terminal restore");
    auto pending_applied = pending_restored.ExecutePendingConsequences();
    Check(pending_applied.size() == 1 && !pending_restored.GetSession(pending_session.Value()).has_value(), "pending consequence survives restore and cleans session");

    // B26 regression: leave exactly two consequence IDs available. Selecting the
    // option needs exactly two IDs (option + destination node). A second allocation
    // of destination consequences after mutation would exhaust the generator and
    // turn the selection into a partial commit.
    DialogueService edge;
    Handler edge_handler;
    Check(static_cast<bool>(edge.RegisterConditionResolver(cond_type, cond)), "edge resolver");
    Check(static_cast<bool>(edge.RegisterConsequenceHandler(cons_type, edge_handler)), "edge handler");
    Check(static_cast<bool>(edge.RegisterCondition({condition, cond_type, {}})), "edge condition");
    Check(static_cast<bool>(edge.RegisterConsequence({consequence, cons_type, {}, 10})), "edge consequence");
    auto edge_definition = edge.RegisterConversation(d);
    Check(static_cast<bool>(edge_definition), "edge conversation");
    Check(static_cast<bool>(edge.FreezeDefinitions()), "edge freeze");
    auto edge_session = edge.StartConversation(edge_definition.Value(), {npc, player});
    Check(static_cast<bool>(edge_session), "edge start");
    auto edge_snapshot = edge.CaptureSnapshot();
    edge_snapshot.consequence_ids.next = std::numeric_limits<std::uint64_t>::max() - 1;
    Check(static_cast<bool>(edge.RestoreSnapshot(std::move(edge_snapshot))), "edge restore near exhaustion");
    Check(static_cast<bool>(edge.SelectOption(edge_session.Value(), option.id, player)), "select remains atomic near consequence ID exhaustion");
    const auto edge_completed = edge.GetSession(edge_session.Value());
    Check(edge_completed.has_value() && edge_completed->state == ConversationState::Completed, "edge conversation completed");
    Check(edge.CaptureSnapshot().consequence_ids.next == 0, "exactly two consequence IDs consumed");

    // Deferred consequences are parked until an explicit resume. Repeated pumps
    // must not busy-retry the handler.
    DialogueService deferred;
    DeferredHandler deferred_handler;
    auto deferred_type = DialogueConsequenceTypeId::FromString("consequence.deferred");
    TypeId deferred_consequence = TypeId::FromString("dialogue.consequence.deferred");
    Check(static_cast<bool>(deferred.RegisterConsequenceHandler(deferred_type, deferred_handler)), "deferred handler");
    Check(static_cast<bool>(deferred.RegisterConsequence({deferred_consequence, deferred_type, {}, 5})), "deferred consequence");
    ConversationDefinition deferred_def;
    deferred_def.canonical_name = "conversation.deferred";
    deferred_def.entry_node = DialogueNodeId::FromString("node.deferred");
    DialogueNodeDefinition deferred_node;
    deferred_node.id = deferred_def.entry_node;
    deferred_node.consequences.push_back(deferred_consequence);
    deferred_def.nodes.push_back(deferred_node);
    auto deferred_def_id = deferred.RegisterConversation(deferred_def);
    Check(static_cast<bool>(deferred_def_id), "deferred definition");
    Check(static_cast<bool>(deferred.FreezeDefinitions()), "deferred freeze");
    auto deferred_session = deferred.StartConversation(deferred_def_id.Value(), {npc, player});
    Check(static_cast<bool>(deferred_session), "deferred session");
    auto first_pump = deferred.ExecutePendingConsequences();
    Check(first_pump.empty() && deferred_handler.count == 1, "deferred first pump");
    auto waiting = deferred.GetSession(deferred_session.Value());
    Check(waiting.has_value() && waiting->state == ConversationState::WaitingForExternalAction, "session waits for external action");
    auto repeated_pump = deferred.ExecutePendingConsequences();
    Check(repeated_pump.empty() && deferred_handler.count == 1, "deferred consequence not busy retried");
    auto deferred_snapshot = deferred.CaptureSnapshot();
    Check(deferred_snapshot.consequences.size() == 1 && deferred_snapshot.consequences.front().state == DialogueConsequenceState::Deferred,
          "deferred consequence persisted");
    auto deferred_execution = deferred_snapshot.consequences.front().id;
    Check(static_cast<bool>(deferred.ResumeConsequence(deferred_execution)), "resume deferred consequence");
    deferred_handler.apply = true;
    auto resumed = deferred.ExecutePendingConsequences();
    Check(resumed.size() == 1 && deferred_handler.count == 2, "resumed consequence applied once");
    Check(!deferred.GetSession(deferred_session.Value()).has_value(), "completed deferred session cleaned up");

    // Explicit failure also releases the external wait and cleans a terminal session.
    DialogueService failed_external;
    DeferredHandler failed_handler;
    Check(static_cast<bool>(failed_external.RegisterConsequenceHandler(deferred_type, failed_handler)), "failed handler");
    Check(static_cast<bool>(failed_external.RegisterConsequence({deferred_consequence, deferred_type, {}, 5})), "failed consequence");
    auto failed_def_id = failed_external.RegisterConversation(deferred_def);
    Check(static_cast<bool>(failed_def_id), "failed external definition");
    Check(static_cast<bool>(failed_external.FreezeDefinitions()), "failed external freeze");
    auto failed_session = failed_external.StartConversation(failed_def_id.Value(), {npc, player});
    Check(static_cast<bool>(failed_session), "failed external session");
    auto failed_pump = failed_external.ExecutePendingConsequences();
    Check(failed_pump.empty(), "failed external deferred pump");
    auto failed_snap = failed_external.CaptureSnapshot();
    auto failed_execution = failed_snap.consequences.front().id;
    Check(static_cast<bool>(failed_external.FailConsequence(failed_execution, TypeId::FromString("external.failed"))), "fail deferred consequence");
    Check(!failed_external.GetSession(failed_session.Value()).has_value(), "failed deferred session cleaned up");

    // Callback exceptions are contained inside Dialogue. Condition exceptions fail closed,
    // consequence exceptions become Failed and do not escape the framework boundary.
    DialogueService callbacks;
    ThrowingCondition throwing_condition;
    ThrowingHandler throwing_handler;
    auto throw_cond_type = DialogueConditionTypeId::FromString("condition.throw");
    auto throw_cons_type = DialogueConsequenceTypeId::FromString("consequence.throw");
    TypeId throw_condition_id = TypeId::FromString("dialogue.condition.throw");
    TypeId throw_consequence_id = TypeId::FromString("dialogue.consequence.throw");
    Check(static_cast<bool>(callbacks.RegisterConditionResolver(throw_cond_type, throwing_condition)), "throwing condition resolver");
    Check(static_cast<bool>(callbacks.RegisterConsequenceHandler(throw_cons_type, throwing_handler)), "throwing consequence handler");
    Check(static_cast<bool>(callbacks.RegisterCondition({throw_condition_id, throw_cond_type, {}})), "throwing condition definition");
    Check(static_cast<bool>(callbacks.RegisterConsequence({throw_consequence_id, throw_cons_type, {}, 1})), "throwing consequence definition");
    ConversationDefinition callback_def;
    callback_def.canonical_name = "conversation.callback.exception";
    callback_def.entry_node = DialogueNodeId::FromString("node.callback");
    DialogueNodeDefinition callback_node;
    callback_node.id = callback_def.entry_node;
    DialogueOptionDefinition callback_option;
    callback_option.id = DialogueOptionId::FromString("option.callback");
    callback_option.conditions.push_back(throw_condition_id);
    callback_node.options.push_back(callback_option);
    callback_def.nodes.push_back(callback_node);
    auto callback_def_id = callbacks.RegisterConversation(callback_def);
    Check(static_cast<bool>(callback_def_id), "callback definition");
    Check(static_cast<bool>(callbacks.FreezeDefinitions()), "callback freeze");
    auto callback_session = callbacks.StartConversation(callback_def_id.Value(), {npc, player});
    Check(static_cast<bool>(callback_session), "callback session");
    Check(callbacks.GetAvailableOptions(callback_session.Value(), player).empty(), "condition exception fails closed");

    DialogueService throwing_consequence_service;
    Check(static_cast<bool>(throwing_consequence_service.RegisterConsequenceHandler(throw_cons_type, throwing_handler)), "throwing handler registration");
    Check(static_cast<bool>(throwing_consequence_service.RegisterConsequence({throw_consequence_id, throw_cons_type, {}, 1})), "throwing consequence registration");
    ConversationDefinition throwing_def;
    throwing_def.canonical_name = "conversation.throwing.consequence";
    throwing_def.entry_node = DialogueNodeId::FromString("node.throwing.consequence");
    DialogueNodeDefinition throwing_node;
    throwing_node.id = throwing_def.entry_node;
    throwing_node.consequences.push_back(throw_consequence_id);
    throwing_def.nodes.push_back(throwing_node);
    auto throwing_def_id = throwing_consequence_service.RegisterConversation(throwing_def);
    Check(static_cast<bool>(throwing_def_id), "throwing consequence definition");
    Check(static_cast<bool>(throwing_consequence_service.FreezeDefinitions()), "throwing consequence freeze");
    auto throwing_session = throwing_consequence_service.StartConversation(throwing_def_id.Value(), {npc, player});
    Check(static_cast<bool>(throwing_session), "throwing consequence session");
    auto throwing_result = throwing_consequence_service.ExecutePendingConsequences();
    Check(throwing_result.empty() && !throwing_consequence_service.GetSession(throwing_session.Value()).has_value(),
          "consequence exception contained as failure");

    // Generator snapshots are validated before state replacement.
    auto valid_snapshot = deferred.CaptureSnapshot();
    DialogueService restore_guard;
    Check(static_cast<bool>(restore_guard.RegisterConsequenceHandler(deferred_type, deferred_handler)), "restore guard handler");
    Check(static_cast<bool>(restore_guard.RegisterConsequence({deferred_consequence, deferred_type, {}, 5})), "restore guard consequence");
    Check(static_cast<bool>(restore_guard.RegisterConversation(deferred_def)), "restore guard definition");
    Check(static_cast<bool>(restore_guard.FreezeDefinitions()), "restore guard freeze");
    auto guard_session = restore_guard.StartConversation(deferred_def_id.Value(), {npc, player});
    Check(static_cast<bool>(guard_session), "restore guard initial state");
    auto bad_scope = valid_snapshot;
    bad_scope.session_ids.scope = 0x9999;
    Check(!restore_guard.RestoreSnapshot(std::move(bad_scope)), "foreign session generator scope rejected");
    Check(restore_guard.GetSession(guard_session.Value()).has_value(), "failed restore preserves current dialogue state");

    // Bounded journal reports that a full snapshot is required once a consumer falls behind.
    DialogueService journal;
    auto journal_def_id = journal.RegisterConversation(terminal);
    Check(static_cast<bool>(journal_def_id), "journal definition");
    Check(static_cast<bool>(journal.FreezeDefinitions()), "journal freeze");
    for (int i = 0; i < 2100; ++i)
        Check(static_cast<bool>(journal.StartConversation(journal_def_id.Value(), {npc, player})), "journal session");
    auto journal_batch = journal.ReadChangesSince(0);
    Check(journal_batch.snapshot_required, "journal overflow requires snapshot");
    Check(journal_batch.changes.empty(), "stale journal read does not return partial history");

    return 0;
}
