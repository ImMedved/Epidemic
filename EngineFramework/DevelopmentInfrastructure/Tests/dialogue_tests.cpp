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
        return {DialogueConditionState::Satisfied, Revision{1}};
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
    return 0;
}
