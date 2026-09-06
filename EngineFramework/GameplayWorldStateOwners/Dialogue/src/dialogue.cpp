#include "Epidemic/GameFramework/Dialogue/dialogue.h"
#include "Epidemic/Foundation/error.h"
#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_set>
#include <utility>
namespace epidemic::gameplay::dialogue
{
namespace
{
foundation::Error Error(std::string_view c, std::string_view m)
{
    return foundation::Error::Create(c, m);
}
bool IsTerminal(ConversationState s) noexcept
{
    return s == ConversationState::Completed || s == ConversationState::Interrupted || s == ConversationState::Failed;
}
bool IsLiveConsequence(DialogueConsequenceState s) noexcept
{
    return s == DialogueConsequenceState::Pending || s == DialogueConsequenceState::Deferred;
}
bool SameObject(GameplayObjectRef a, GameplayObjectRef b) noexcept
{
    return a.domain == b.domain && a.id == b.id;
}
GameplayContext MergeContext(GameplayContext base, const GameplayContext &overlay) noexcept
{
    if (overlay.tick.value != 0) base.tick = overlay.tick;
    if (overlay.time.ticks != 0) base.time = overlay.time;
    if (overlay.actor.IsValid()) base.actor = overlay.actor;
    if (overlay.instigator.IsValid()) base.instigator = overlay.instigator;
    if (overlay.source.IsValid()) base.source = overlay.source;
    if (overlay.operation.IsValid()) base.operation = overlay.operation;
    if (overlay.correlation.IsValid()) base.correlation = overlay.correlation;
    if (overlay.parent_operation.IsValid()) base.parent_operation = overlay.parent_operation;
    if (overlay.cause_event.IsValid()) base.cause_event = overlay.cause_event;
    return base;
}
} // namespace
foundation::Result<void> DialogueService::RegisterCondition(DialogueConditionDefinition d)
{
    if (frozen_ || !d.id.IsValid() || !d.type.IsValid() || conditions_.contains(d.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.dialogue.invalid_condition", "invalid/duplicate dialogue condition"));
    conditions_.emplace(d.id, std::move(d));
    return foundation::Result<void>::Success();
}
foundation::Result<void> DialogueService::RegisterConsequence(DialogueConsequenceDefinition d)
{
    if (frozen_ || !d.id.IsValid() || !d.type.IsValid() || consequence_defs_.contains(d.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.dialogue.invalid_consequence", "invalid/duplicate dialogue consequence"));
    consequence_defs_.emplace(d.id, std::move(d));
    return foundation::Result<void>::Success();
}
foundation::Result<ConversationDefinitionId> DialogueService::RegisterConversation(ConversationDefinition d)
{
    if (frozen_)
        return foundation::Result<ConversationDefinitionId>::Failure(
            Error("gameplay.dialogue.frozen", "dialogue definitions frozen"));
    if (d.canonical_name.empty())
        return foundation::Result<ConversationDefinitionId>::Failure(
            Error("gameplay.dialogue.invalid_definition", "conversation canonical name missing"));
    const auto expected = ConversationDefinitionId::FromString(d.canonical_name);
    if (!d.id.IsValid())
        d.id = expected;
    if (!d.id.IsValid() || d.id != expected || !d.entry_node.IsValid() || definitions_.contains(d.id))
        return foundation::Result<ConversationDefinitionId>::Failure(
            Error("gameplay.dialogue.invalid_definition", "invalid/duplicate conversation definition"));
    std::unordered_set<TypeId> roles;
    for (auto role : d.participant_roles)
        if (!role.IsValid() || !roles.insert(role).second)
            return foundation::Result<ConversationDefinitionId>::Failure(
                Error("gameplay.dialogue.invalid_roles", "invalid/duplicate participant role"));
    std::unordered_set<DialogueNodeId, IdHash> nodes;
    std::unordered_set<DialogueOptionId, IdHash> options;
    for (const auto &n : d.nodes)
    {
        if (!n.id.IsValid() || !nodes.insert(n.id).second)
            return foundation::Result<ConversationDefinitionId>::Failure(
                Error("gameplay.dialogue.invalid_graph", "duplicate/invalid dialogue node"));
        if (n.speaker_role.IsValid() && !roles.empty() && !roles.contains(n.speaker_role))
            return foundation::Result<ConversationDefinitionId>::Failure(
                Error("gameplay.dialogue.invalid_graph", "node references unknown speaker role"));
        if (!n.options.empty() && n.automatic_next.IsValid())
            return foundation::Result<ConversationDefinitionId>::Failure(
                Error("gameplay.dialogue.invalid_graph", "node cannot have options and automatic next"));
        for (const auto &o : n.options)
            if (!o.id.IsValid() || !options.insert(o.id).second)
                return foundation::Result<ConversationDefinitionId>::Failure(
                    Error("gameplay.dialogue.invalid_graph", "duplicate/invalid dialogue option"));
    }
    if (!nodes.contains(d.entry_node))
        return foundation::Result<ConversationDefinitionId>::Failure(
            Error("gameplay.dialogue.invalid_graph", "entry node missing"));
    d.revision = Revision{1};
    const auto id = d.id;
    definitions_.emplace(id, std::move(d));
    diagnostics_.definitions = definitions_.size();
    return foundation::Result<ConversationDefinitionId>::Success(id);
}
foundation::Result<void> DialogueService::RegisterConditionResolver(DialogueConditionTypeId t,
                                                                    const IDialogueConditionResolver &r)
{
    if (frozen_ || !t.IsValid() || condition_resolvers_.contains(t))
        return foundation::Result<void>::Failure(
            Error("gameplay.dialogue.invalid_resolver", "invalid/duplicate condition resolver"));
    condition_resolvers_[t] = &r;
    return foundation::Result<void>::Success();
}
foundation::Result<void> DialogueService::RegisterConsequenceHandler(DialogueConsequenceTypeId t,
                                                                     const IDialogueConsequenceHandler &h)
{
    if (frozen_ || !t.IsValid() || consequence_handlers_.contains(t))
        return foundation::Result<void>::Failure(
            Error("gameplay.dialogue.invalid_handler", "invalid/duplicate consequence handler"));
    consequence_handlers_[t] = &h;
    return foundation::Result<void>::Success();
}
foundation::Result<void> DialogueService::FreezeDefinitions()
{
    for (const auto &[id, d] : definitions_)
    {
        (void)id;
        for (const auto &n : d.nodes)
        {
            for (auto c : n.conditions)
            {
                auto it = conditions_.find(c);
                if (it == conditions_.end())
                    return foundation::Result<void>::Failure(
                        Error("gameplay.dialogue.unknown_condition", "dialogue node references unknown condition"));
                if (!condition_resolvers_.contains(it->second.type))
                    return foundation::Result<void>::Failure(
                        Error("gameplay.dialogue.missing_resolver", "dialogue condition resolver missing"));
            }
            for (auto c : n.consequences)
            {
                auto it = consequence_defs_.find(c);
                if (it == consequence_defs_.end())
                    return foundation::Result<void>::Failure(
                        Error("gameplay.dialogue.unknown_consequence", "dialogue node references unknown consequence"));
                if (!consequence_handlers_.contains(it->second.type))
                    return foundation::Result<void>::Failure(
                        Error("gameplay.dialogue.missing_handler", "dialogue consequence handler missing"));
            }
            for (const auto &o : n.options)
            {
                for (auto c : o.conditions)
                {
                    auto it = conditions_.find(c);
                    if (it == conditions_.end())
                        return foundation::Result<void>::Failure(Error("gameplay.dialogue.unknown_condition",
                                                                       "dialogue option references unknown condition"));
                    if (!condition_resolvers_.contains(it->second.type))
                        return foundation::Result<void>::Failure(
                            Error("gameplay.dialogue.missing_resolver", "dialogue condition resolver missing"));
                }
                for (auto c : o.consequences)
                {
                    auto it = consequence_defs_.find(c);
                    if (it == consequence_defs_.end())
                        return foundation::Result<void>::Failure(Error(
                            "gameplay.dialogue.unknown_consequence", "dialogue option references unknown consequence"));
                    if (!consequence_handlers_.contains(it->second.type))
                        return foundation::Result<void>::Failure(
                            Error("gameplay.dialogue.missing_handler", "dialogue consequence handler missing"));
                }
                if (o.next_node.IsValid() && !FindNode(d, o.next_node))
                    return foundation::Result<void>::Failure(
                        Error("gameplay.dialogue.unknown_node", "dialogue option references unknown node"));
            }
            if (n.automatic_next.IsValid() && !FindNode(d, n.automatic_next))
                return foundation::Result<void>::Failure(
                    Error("gameplay.dialogue.unknown_node", "dialogue automatic transition references unknown node"));
        }
    }
    frozen_ = true;
    return foundation::Result<void>::Success();
}
const DialogueNodeDefinition *DialogueService::FindNode(const ConversationDefinition &d,
                                                        DialogueNodeId id) const noexcept
{
    auto it = std::find_if(d.nodes.begin(), d.nodes.end(), [id](const auto &n) { return n.id == id; });
    return it == d.nodes.end() ? nullptr : &*it;
}
const ConversationDefinition *DialogueService::GetDefinition(ConversationDefinitionId id) const noexcept
{
    auto it = definitions_.find(id);
    return it == definitions_.end() ? nullptr : &it->second;
}
std::optional<ConversationSession> DialogueService::GetSession(ConversationSessionId id) const noexcept
{
    auto it = sessions_.find(id);
    return it == sessions_.end() ? std::nullopt : std::optional<ConversationSession>{it->second};
}
GameplayObjectRef DialogueService::ResolveRole(const ConversationSession &s, TypeId role) const noexcept
{
    if (!role.IsValid())
        return s.current_speaker.IsValid() ? s.current_speaker :
               (!s.participant_bindings.empty() ? s.participant_bindings.front().object : GameplayObjectRef{});
    auto it = std::find_if(s.participant_bindings.begin(), s.participant_bindings.end(),
                           [role](const auto &p) { return p.role == role; });
    return it == s.participant_bindings.end() ? GameplayObjectRef{} : it->object;
}
DialogueConditionContext DialogueService::MakeConditionContext(const ConversationSession &s,
                                                               GameplayObjectRef actor) const noexcept
{
    GameplayObjectRef listener{};
    if (s.current_speaker.IsValid() && !SameObject(s.current_speaker, actor))
        listener = s.current_speaker;
    else
        for (const auto& participant : s.participant_bindings)
            if (!SameObject(participant.object, actor)) { listener = participant.object; break; }
    return {s.id, actor, listener, s.context};
}

bool DialogueService::ConditionsPass(const std::vector<TypeId> &ids, const DialogueConditionContext &ctx) const
{
    for (auto id : ids)
    {
        auto d = conditions_.find(id);
        if (d == conditions_.end())
            return false;
        auto r = condition_resolvers_.find(d->second.type);
        if (r == condition_resolvers_.end())
            return false;
        try
        {
            if (r->second->Evaluate(d->second, ctx).state != DialogueConditionState::Satisfied)
                return false;
        }
        catch (...)
        {
            return false;
        }
    }
    return true;
}
foundation::Result<std::vector<DialogueConsequenceExecution>> DialogueService::PrepareConsequences(
    ConversationSessionId session, const std::vector<TypeId> &ids, DialogueNodeId source_node,
    DialogueOptionId source_option, GameplayContext context)
{
    std::vector<const DialogueConsequenceDefinition *> ordered;
    ordered.reserve(ids.size());
    for (auto id : ids)
    {
        auto it = consequence_defs_.find(id);
        if (it == consequence_defs_.end())
            return foundation::Result<std::vector<DialogueConsequenceExecution>>::Failure(
                Error("gameplay.dialogue.unknown_consequence", "dialogue consequence missing"));
        ordered.push_back(&it->second);
    }
    std::sort(ordered.begin(), ordered.end(), [](auto *a, auto *b) {
        if (a->priority != b->priority)
            return a->priority > b->priority;
        return a->id < b->id;
    });
    std::vector<DialogueConsequenceExecution> prepared;
    prepared.reserve(ordered.size());
    for (auto *d : ordered)
    {
        DialogueConsequenceExecution e;
        e.id = DialogueConsequenceExecutionId{consequence_ids_.Next()};
        if (!e.id.IsValid())
            return foundation::Result<std::vector<DialogueConsequenceExecution>>::Failure(
                Error("gameplay.dialogue.id_exhausted", "dialogue consequence id exhausted"));
        e.session = session;
        e.consequence = d->id;
        e.source_node = source_node;
        e.source_option = source_option;
        e.context = context;
        prepared.push_back(e);
    }
    return foundation::Result<std::vector<DialogueConsequenceExecution>>::Success(std::move(prepared));
}
void DialogueService::CommitPreparedConsequences(std::vector<DialogueConsequenceExecution> prepared)
{
    for (auto &e : prepared)
    {
        Bump();
        e.revision = revision_;
        const auto session = e.session;
        const auto consequence = e.consequence;
        const auto execution = e.id;
        const auto node = e.source_node;
        const auto option = e.source_option;
        const auto context = e.context;
        consequences_.emplace(e.id, std::move(e));
        ++diagnostics_.consequences;
        DialogueChange c;
        c.kind = DialogueChangeKind::ConsequencePlanned;
        c.session = session;
        c.node = node;
        c.option = option;
        c.consequence_execution = execution;
        c.consequence = consequence;
        c.consequence_state = DialogueConsequenceState::Pending;
        c.context = context;
        c.revision = revision_;
        Record(c);
    }
}
foundation::Result<void> DialogueService::EnterNode(ConversationSession &s, DialogueNodeId node,
                                                    GameplayContext context,
                                                    std::optional<std::vector<DialogueConsequenceExecution>> prepared_consequences)
{
    const auto *d = GetDefinition(s.definition);
    const auto *n = d ? FindNode(*d, node) : nullptr;
    if (!n)
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.node_missing", "dialogue node missing"));
    auto speaker = ResolveRole(s, n->speaker_role);
    if (!speaker.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.speaker_missing", "dialogue speaker missing"));
    if (!ConditionsPass(n->conditions, MakeConditionContext(s, speaker)))
        return foundation::Result<void>::Failure(
            Error("gameplay.dialogue.node_unavailable", "dialogue node conditions failed"));
    std::vector<DialogueConsequenceExecution> consequences;
    if (prepared_consequences.has_value())
        consequences = std::move(*prepared_consequences);
    else
    {
        auto prepared = PrepareConsequences(s.id, n->consequences, node, {}, MergeContext(s.context.gameplay, context));
        if (!prepared) return foundation::Result<void>::Failure(prepared.GetError());
        consequences = std::move(prepared.Value());
    }
    const bool was_active = !IsTerminal(s.state);
    Bump();
    s.current_speaker = speaker;
    s.current_node = node;
    s.state = n->options.empty()
                  ? (n->automatic_next.IsValid() ? ConversationState::Active : ConversationState::Completed)
                  : ConversationState::WaitingForChoice;
    s.revision = revision_;
    CommitPreparedConsequences(std::move(consequences));
    DialogueChange entered;
    entered.kind = DialogueChangeKind::NodeEntered;
    entered.session = s.id;
    entered.node = node;
    entered.context = context;
    entered.revision = revision_;
    Record(entered);
    if (s.state == ConversationState::Completed)
    {
        if (was_active && diagnostics_.active_sessions > 0)
            --diagnostics_.active_sessions;
        DialogueChange completed;
        completed.kind = DialogueChangeKind::ConversationCompleted;
        completed.session = s.id;
        completed.node = node;
        completed.context = context;
        completed.revision = revision_;
        Record(completed);
    }
    return foundation::Result<void>::Success();
}
foundation::Result<ConversationSessionId> DialogueService::StartConversation(
    ConversationDefinitionId id, std::vector<GameplayObjectRef> participants, ConversationContext context)
{
    std::vector<ConversationParticipant> bindings;
    const auto *d = GetDefinition(id);
    if (d && !d->participant_roles.empty())
    {
        if (participants.size() != d->participant_roles.size())
            return foundation::Result<ConversationSessionId>::Failure(
                Error("gameplay.dialogue.invalid_participant", "participant role count mismatch"));
        for (std::size_t i = 0; i < participants.size(); ++i)
            bindings.push_back({d->participant_roles[i], participants[i]});
    }
    else
    {
        for (std::size_t i = 0; i < participants.size(); ++i)
            bindings.push_back({TypeId::FromString(i == 0 ? "dialogue.participant.primary" : "dialogue.participant.secondary"), participants[i]});
    }
    return StartConversation(id, std::move(bindings), std::move(context));
}
foundation::Result<ConversationSessionId> DialogueService::StartConversation(
    ConversationDefinitionId id, std::vector<ConversationParticipant> bindings, ConversationContext context)
{
    if (!frozen_)
        return foundation::Result<ConversationSessionId>::Failure(
            Error("gameplay.dialogue.not_frozen", "dialogue definitions are not frozen"));
    const auto *d = GetDefinition(id);
    if (!d || bindings.size() < 2)
        return foundation::Result<ConversationSessionId>::Failure(
            Error("gameplay.dialogue.cannot_start", "conversation definition missing or participants invalid"));
    std::unordered_set<TypeId> roles;
    std::vector<GameplayObjectRef> objects;
    for (const auto &p : bindings)
    {
        if (!p.role.IsValid() || !p.object.IsValid() || !roles.insert(p.role).second)
            return foundation::Result<ConversationSessionId>::Failure(
                Error("gameplay.dialogue.invalid_participant", "invalid/duplicate conversation participant role"));
        if (std::any_of(objects.begin(), objects.end(), [p](auto object) { return SameObject(object, p.object); }))
            return foundation::Result<ConversationSessionId>::Failure(
                Error("gameplay.dialogue.invalid_participant", "duplicate conversation participant object"));
        objects.push_back(p.object);
    }
    for (auto role : d->participant_roles)
        if (!roles.contains(role))
            return foundation::Result<ConversationSessionId>::Failure(
                Error("gameplay.dialogue.invalid_participant", "required conversation participant role missing"));
    ConversationSession s;
    s.id = ConversationSessionId{session_ids_.Next()};
    if (!s.id.IsValid())
        return foundation::Result<ConversationSessionId>::Failure(
            Error("gameplay.dialogue.id_exhausted", "dialogue session id exhausted"));
    s.definition = id;
    s.participants = std::move(objects);
    s.participant_bindings = std::move(bindings);
    s.context = std::move(context);
    s.started_at = s.context.gameplay.time;
    s.state = ConversationState::Active;
    const auto starting_revision = revision_;
    auto entry = EnterNode(s, d->entry_node, s.context.gameplay);
    if (!entry)
    {
        revision_ = starting_revision;
        return foundation::Result<ConversationSessionId>::Failure(entry.GetError());
    }
    const auto sid = s.id;
    sessions_.emplace(sid, std::move(s));
    if (!IsTerminal(sessions_.at(sid).state))
        ++diagnostics_.active_sessions;
    DialogueChange started;
    started.kind = DialogueChangeKind::ConversationStarted;
    started.session = sid;
    started.node = d->entry_node;
    started.context = sessions_.at(sid).context.gameplay;
    started.revision = sessions_.at(sid).revision;
    Record(started);
    return foundation::Result<ConversationSessionId>::Success(sid);
}
std::vector<DialogueOptionDefinition> DialogueService::GetAvailableOptions(ConversationSessionId id,
                                                                           GameplayObjectRef actor) const
{
    std::vector<DialogueOptionDefinition> out;
    auto so = GetSession(id);
    const auto *s = so ? &so.value() : nullptr;
    const auto *d = s ? GetDefinition(s->definition) : nullptr;
    const auto *n = d ? FindNode(*d, s->current_node) : nullptr;
    if (!s || !n || s->state != ConversationState::WaitingForChoice || !actor.IsValid())
        return out;
    if (std::none_of(s->participant_bindings.begin(), s->participant_bindings.end(),
                     [actor](const auto& p) { return SameObject(p.object, actor); }))
        return out;
    auto ctx = MakeConditionContext(*s, actor);
    for (const auto &o : n->options)
        if ((o.repeat_policy == DialogueOptionRepeatPolicy::Repeatable ||
             std::find(s->resolved_once_per_conversation_options.begin(), s->resolved_once_per_conversation_options.end(), o.id.value) == s->resolved_once_per_conversation_options.end()) &&
            ConditionsPass(o.conditions, ctx))
            out.push_back(o);
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return out;
}
foundation::Result<void> DialogueService::SelectOption(ConversationSessionId id, DialogueOptionId option,
                                                       GameplayObjectRef actor, GameplayContext context)
{
    auto it = sessions_.find(id);
    if (it == sessions_.end() || it->second.state != ConversationState::WaitingForChoice)
        return foundation::Result<void>::Failure(
            Error("gameplay.dialogue.session_not_waiting", "conversation is not waiting for a choice"));
    if (std::none_of(it->second.participant_bindings.begin(), it->second.participant_bindings.end(),
                     [actor](const auto& p) { return SameObject(p.object, actor); }))
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.actor_invalid", "actor is not a participant"));
    const auto *d = GetDefinition(it->second.definition);
    const auto *n = d ? FindNode(*d, it->second.current_node) : nullptr;
    if (!n)
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.node_missing", "dialogue node missing"));
    auto oit = std::find_if(n->options.begin(), n->options.end(), [option](const auto &o) { return o.id == option; });
    if (oit == n->options.end() ||
        (oit->repeat_policy == DialogueOptionRepeatPolicy::OncePerConversation &&
         std::find(it->second.resolved_once_per_conversation_options.begin(), it->second.resolved_once_per_conversation_options.end(), option.value) != it->second.resolved_once_per_conversation_options.end()) ||
        !ConditionsPass(oit->conditions, MakeConditionContext(it->second, actor)))
        return foundation::Result<void>::Failure(
            Error("gameplay.dialogue.option_unavailable", "dialogue option unavailable"));
    std::optional<std::vector<DialogueConsequenceExecution>> next_prepared;
    if (oit->next_node.IsValid())
    {
        const auto *next = FindNode(*d, oit->next_node);
        if (!next)
            return foundation::Result<void>::Failure(Error("gameplay.dialogue.node_missing", "dialogue node missing"));
        auto speaker = ResolveRole(it->second, next->speaker_role);
        if (!speaker.IsValid() || !ConditionsPass(next->conditions, MakeConditionContext(it->second, speaker)))
            return foundation::Result<void>::Failure(Error("gameplay.dialogue.node_unavailable", "dialogue node conditions failed"));
        auto prepared = PrepareConsequences(id, next->consequences, next->id, {}, MergeContext(it->second.context.gameplay, context));
        if (!prepared) return foundation::Result<void>::Failure(prepared.GetError());
        next_prepared = std::move(prepared.Value());
    }
    auto option_consequences = PrepareConsequences(id, oit->consequences, it->second.current_node, option,
                                                   MergeContext(it->second.context.gameplay, context));
    if (!option_consequences)
        return foundation::Result<void>::Failure(option_consequences.GetError());
    Bump();
    if (oit->repeat_policy == DialogueOptionRepeatPolicy::OncePerConversation)
        {
            it->second.resolved_once_per_conversation_options.push_back(option.value);
            std::sort(it->second.resolved_once_per_conversation_options.begin(), it->second.resolved_once_per_conversation_options.end());
            it->second.resolved_once_per_conversation_options.erase(
                std::unique(it->second.resolved_once_per_conversation_options.begin(),
                            it->second.resolved_once_per_conversation_options.end()),
                it->second.resolved_once_per_conversation_options.end());
        }
    it->second.revision = revision_;
    ++diagnostics_.options_selected;
    DialogueChange selected;
    selected.kind = DialogueChangeKind::OptionSelected;
    selected.session = id;
    selected.node = it->second.current_node;
    selected.option = option;
    selected.context = context;
    selected.revision = revision_;
    Record(selected);
    CommitPreparedConsequences(std::move(option_consequences.Value()));
    if (oit->next_node.IsValid())
        return EnterNode(it->second, oit->next_node, context, std::move(next_prepared));
    it->second.state = ConversationState::Completed;
    if (diagnostics_.active_sessions > 0)
        --diagnostics_.active_sessions;
    DialogueChange completed;
    completed.kind = DialogueChangeKind::ConversationCompleted;
    completed.session = id;
    completed.node = it->second.current_node;
    completed.option = option;
    completed.context = context;
    completed.revision = revision_;
    Record(completed);
    MaybeCleanupTerminalSession(id);
    return foundation::Result<void>::Success();
}
foundation::Result<void> DialogueService::Advance(ConversationSessionId id, GameplayContext context)
{
    auto it = sessions_.find(id);
    if (it == sessions_.end() || it->second.state != ConversationState::Active)
        return foundation::Result<void>::Failure(
            Error("gameplay.dialogue.session_not_active", "conversation session is not active"));
    const auto *d = GetDefinition(it->second.definition);
    const auto *n = d ? FindNode(*d, it->second.current_node) : nullptr;
    if (!n || !n->automatic_next.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.dialogue.no_automatic_transition", "dialogue node has no automatic transition"));
    return EnterNode(it->second, n->automatic_next, context);
}
foundation::Result<void> DialogueService::Interrupt(ConversationSessionId id, TypeId reason, GameplayContext context)
{
    auto it = sessions_.find(id);
    if (it == sessions_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.dialogue.session_missing", "conversation session missing"));
    if (IsTerminal(it->second.state))
        return foundation::Result<void>::Success();
    Bump();
    it->second.state = ConversationState::Interrupted;
    it->second.revision = revision_;
    if (diagnostics_.active_sessions > 0)
        --diagnostics_.active_sessions;
    ++diagnostics_.interruptions;
    DialogueChange interrupted;
    interrupted.kind = DialogueChangeKind::ConversationInterrupted;
    interrupted.session = id;
    interrupted.node = it->second.current_node;
    interrupted.reason = reason;
    interrupted.context = context;
    interrupted.revision = revision_;
    Record(interrupted);
    MaybeCleanupTerminalSession(id);
    return foundation::Result<void>::Success();
}
std::vector<DialogueConsequenceExecutionId> DialogueService::ExecutePendingConsequences(std::size_t budget)
{
    std::vector<DialogueConsequenceExecutionId> ids;
    for (const auto &[id, e] : consequences_)
        if (e.state == DialogueConsequenceState::Pending)
            ids.push_back(id);
    std::sort(ids.begin(), ids.end(), [this](auto a, auto b) {
        const auto &ea = consequences_.at(a);
        const auto &eb = consequences_.at(b);
        const auto da = consequence_defs_.find(ea.consequence);
        const auto db = consequence_defs_.find(eb.consequence);
        const auto pa = da == consequence_defs_.end() ? std::numeric_limits<std::int32_t>::min() : da->second.priority;
        const auto pb = db == consequence_defs_.end() ? std::numeric_limits<std::int32_t>::min() : db->second.priority;
        if (pa != pb)
            return pa > pb;
        return a < b;
    });
    if (ids.size() > budget)
        ids.resize(budget);
    std::vector<DialogueConsequenceExecutionId> applied;
    for (auto id : ids)
    {
        auto &e = consequences_.at(id);
        auto d = consequence_defs_.find(e.consequence);
        auto s = sessions_.find(e.session);
        DialogueConsequenceState state = DialogueConsequenceState::Failed;
        if (d != consequence_defs_.end() && s != sessions_.end())
        {
            auto h = consequence_handlers_.find(d->second.type);
            if (h == consequence_handlers_.end())
                state = DialogueConsequenceState::Deferred;
            else
            {
                try
                {
                    state = h->second->Execute(d->second, e, s->second);
                }
                catch (...)
                {
                    state = DialogueConsequenceState::Failed;
                }
            }
        }
        if (state == e.state)
            continue;
        Bump();
        e.state = state;
        e.revision = revision_;
        DialogueChange c;
        c.kind = state == DialogueConsequenceState::Applied   ? DialogueChangeKind::ConsequenceApplied
                 : state == DialogueConsequenceState::Deferred ? DialogueChangeKind::ConsequenceDeferred
                                                               : DialogueChangeKind::ConsequenceFailed;
        c.session = e.session;
        c.node = e.source_node;
        c.option = e.source_option;
        c.consequence_execution = e.id;
        c.consequence = e.consequence;
        c.consequence_state = e.state;
        c.context = e.context;
        c.revision = revision_;
        Record(c);
        if (state == DialogueConsequenceState::Deferred)
        {
            auto session = sessions_.find(e.session);
            if (session != sessions_.end() && session->second.state != ConversationState::WaitingForExternalAction)
            {
                const bool was_terminal = IsTerminal(session->second.state);
                session->second.state = ConversationState::WaitingForExternalAction;
                session->second.revision = revision_;
                if (was_terminal) ++diagnostics_.active_sessions;
            }
        }
        else
        {
            RefreshExternalWaitState(e.session, e.context);
        }
        if (state == DialogueConsequenceState::Applied)
            applied.push_back(id);
    }
    for (auto it = consequences_.begin(); it != consequences_.end();)
    {
        if (!IsLiveConsequence(it->second.state))
            it = consequences_.erase(it);
        else
            ++it;
    }
    diagnostics_.consequences = consequences_.size();
    std::vector<ConversationSessionId> terminal_sessions;
    for (const auto& [sid, session] : sessions_) if (IsTerminal(session.state)) terminal_sessions.push_back(sid);
    for (const auto sid : terminal_sessions) MaybeCleanupTerminalSession(sid);
    return applied;
}
foundation::Result<void> DialogueService::ResumeConsequence(DialogueConsequenceExecutionId id, GameplayContext context)
{
    auto it = consequences_.find(id);
    if (it == consequences_.end())
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.consequence_missing", "dialogue consequence execution missing"));
    if (it->second.state != DialogueConsequenceState::Deferred)
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.consequence_not_deferred", "dialogue consequence is not deferred"));
    Bump();
    it->second.state = DialogueConsequenceState::Pending;
    it->second.context = MergeContext(it->second.context, context);
    it->second.revision = revision_;
    return foundation::Result<void>::Success();
}
foundation::Result<void> DialogueService::FailConsequence(DialogueConsequenceExecutionId id, TypeId reason, GameplayContext context)
{
    auto it = consequences_.find(id);
    if (it == consequences_.end())
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.consequence_missing", "dialogue consequence execution missing"));
    if (it->second.state != DialogueConsequenceState::Deferred)
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.consequence_not_deferred", "dialogue consequence is not deferred"));
    Bump();
    const auto session_id = it->second.session;
    it->second.state = DialogueConsequenceState::Failed;
    it->second.context = MergeContext(it->second.context, context);
    it->second.revision = revision_;
    DialogueChange c;
    c.kind = DialogueChangeKind::ConsequenceFailed;
    c.session = session_id;
    c.node = it->second.source_node;
    c.option = it->second.source_option;
    c.consequence_execution = it->second.id;
    c.consequence = it->second.consequence;
    c.consequence_state = DialogueConsequenceState::Failed;
    c.reason = reason;
    c.context = it->second.context;
    c.revision = revision_;
    Record(c);
    consequences_.erase(it);
    diagnostics_.consequences = consequences_.size();
    RefreshExternalWaitState(session_id, context);
    MaybeCleanupTerminalSession(session_id);
    return foundation::Result<void>::Success();
}
DialogueChangeBatch DialogueService::ReadChangesSince(std::uint64_t seq) const
{
    DialogueChangeBatch batch;
    batch.latest_sequence = next_change_sequence_ > 1 ? next_change_sequence_ - 1 : 0;
    batch.oldest_available_sequence = changes_.empty() ? next_change_sequence_ : changes_.front().sequence;
    if (!changes_.empty() && seq < batch.oldest_available_sequence &&
        batch.oldest_available_sequence - seq > 1)
    {
        batch.snapshot_required = true;
        return batch;
    }
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(batch.changes),
                 [seq](const auto &c) { return c.sequence > seq; });
    return batch;
}
std::vector<DialogueChange> DialogueService::ChangesSince(std::uint64_t seq) const
{
    return ReadChangesSince(seq).changes;
}
DialogueSnapshot DialogueService::CaptureSnapshot() const
{
    DialogueSnapshot s;
    for (const auto &[id, v] : sessions_)
    {
        const bool has_live_consequence = std::any_of(consequences_.begin(), consequences_.end(), [&](const auto &pair) {
            return pair.second.session == id && IsLiveConsequence(pair.second.state);
        });
        if (!IsTerminal(v.state) || has_live_consequence)
            s.sessions.push_back(v);
    }
    for (const auto &[id, v] : consequences_)
    {
        (void)id;
        if (IsLiveConsequence(v.state))
            s.consequences.push_back(v);
    }
    std::sort(s.sessions.begin(), s.sessions.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.consequences.begin(), s.consequences.end(), [](auto &a, auto &b) { return a.id < b.id; });
    s.session_ids = session_ids_.GetSnapshot();
    s.consequence_ids = consequence_ids_.GetSnapshot();
    s.revision = revision_;
    return s;
}
foundation::Result<void> DialogueService::RestoreSnapshot(DialogueSnapshot s)
{
    std::unordered_map<ConversationSessionId, ConversationSession, IdHash> sessions;
    for (auto &v : s.sessions)
    {
        const auto *d = GetDefinition(v.definition);
        const auto *node = d ? FindNode(*d, v.current_node) : nullptr;
        if (!v.id.IsValid() || !d || !node || sessions.contains(v.id) || v.revision > s.revision)
            return foundation::Result<void>::Failure(
                Error("gameplay.dialogue.restore_invalid", "invalid dialogue session snapshot"));
        std::unordered_set<TypeId> roles;
        std::vector<GameplayObjectRef> objects;
        for (const auto &p : v.participant_bindings)
        {
            if (!p.role.IsValid() || !p.object.IsValid() || !roles.insert(p.role).second ||
                std::any_of(objects.begin(), objects.end(), [p](auto object) { return SameObject(object, p.object); }))
                return foundation::Result<void>::Failure(
                    Error("gameplay.dialogue.restore_invalid", "invalid dialogue participant snapshot"));
            objects.push_back(p.object);
        }
        std::sort(v.resolved_once_per_conversation_options.begin(), v.resolved_once_per_conversation_options.end());
        if (std::adjacent_find(v.resolved_once_per_conversation_options.begin(), v.resolved_once_per_conversation_options.end()) != v.resolved_once_per_conversation_options.end())
            return foundation::Result<void>::Failure(Error("gameplay.dialogue.restore_invalid", "duplicate resolved dialogue option"));
        v.participants = objects;
        for (auto role : d->participant_roles)
            if (!roles.contains(role))
                return foundation::Result<void>::Failure(
                    Error("gameplay.dialogue.restore_invalid", "dialogue participant role missing"));
        if (node->speaker_role.IsValid() && !SameObject(v.current_speaker, ResolveRole(v, node->speaker_role)))
            return foundation::Result<void>::Failure(
                Error("gameplay.dialogue.restore_invalid", "dialogue speaker snapshot invalid"));
        sessions.emplace(v.id, std::move(v));
    }
    std::unordered_map<DialogueConsequenceExecutionId, DialogueConsequenceExecution, IdHash> consequences;
    for (auto &v : s.consequences)
    {
        if (!v.id.IsValid() || !sessions.contains(v.session) || !consequence_defs_.contains(v.consequence) ||
            !IsLiveConsequence(v.state) || consequences.contains(v.id) || v.revision > s.revision)
            return foundation::Result<void>::Failure(
                Error("gameplay.dialogue.restore_invalid", "invalid dialogue consequence snapshot"));
        consequences.emplace(v.id, std::move(v));
    }
    for (const auto &[sid, session] : sessions)
    {
        const bool has_live = std::any_of(consequences.begin(), consequences.end(), [&](const auto &pair) {
            return pair.second.session == sid && IsLiveConsequence(pair.second.state);
        });
        const bool has_deferred = std::any_of(consequences.begin(), consequences.end(), [&](const auto &pair) {
            return pair.second.session == sid && pair.second.state == DialogueConsequenceState::Deferred;
        });
        if ((IsTerminal(session.state) && !has_live) ||
            (session.state == ConversationState::WaitingForExternalAction && !has_deferred) ||
            (has_deferred && session.state != ConversationState::WaitingForExternalAction))
            return foundation::Result<void>::Failure(
                Error("gameplay.dialogue.restore_invalid", "dialogue session/consequence lifecycle mismatch"));
    }
    const auto valid_generator = [](MonotonicIdGenerator<GameplayObjectId>::Snapshot snapshot,
                                    std::uint64_t expected_scope, std::uint64_t max_low) noexcept {
        if (!MonotonicIdGenerator<GameplayObjectId>::IsValidSnapshot(snapshot) || snapshot.scope != expected_scope)
            return false;
        return snapshot.next == 0 || snapshot.next > max_low;
    };
    std::uint64_t max_session_low = 0;
    for (const auto &[id, value] : sessions) { (void)value; max_session_low = std::max(max_session_low, id.value.Low()); }
    std::uint64_t max_consequence_low = 0;
    for (const auto &[id, value] : consequences) { (void)value; max_consequence_low = std::max(max_consequence_low, id.value.Low()); }
    if (!valid_generator(s.session_ids, 0x3600, max_session_low) ||
        !valid_generator(s.consequence_ids, 0x3601, max_consequence_low))
        return foundation::Result<void>::Failure(
            Error("gameplay.dialogue.restore_invalid", "invalid dialogue id generator snapshot"));

    sessions_ = std::move(sessions);
    consequences_ = std::move(consequences);
    session_ids_.Restore(s.session_ids);
    consequence_ids_.Restore(s.consequence_ids);
    revision_ = s.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    diagnostics_.active_sessions = static_cast<std::uint64_t>(std::count_if(
        sessions_.begin(), sessions_.end(), [](const auto &pair) { return !IsTerminal(pair.second.state); }));
    diagnostics_.consequences = consequences_.size();
    diagnostics_.definitions = definitions_.size();
    return foundation::Result<void>::Success();
}
void DialogueService::MaybeCleanupTerminalSession(ConversationSessionId id)
{
    const auto session = sessions_.find(id);
    if (session == sessions_.end() || !IsTerminal(session->second.state)) return;
    const bool has_live = std::any_of(consequences_.begin(), consequences_.end(), [&](const auto& pair){
        return pair.second.session == id && IsLiveConsequence(pair.second.state);
    });
    if (!has_live) sessions_.erase(session);
}
void DialogueService::RefreshExternalWaitState(ConversationSessionId id, GameplayContext context)
{
    auto session = sessions_.find(id);
    if (session == sessions_.end() || session->second.state != ConversationState::WaitingForExternalAction) return;
    const bool has_deferred = std::any_of(consequences_.begin(), consequences_.end(), [&](const auto &pair) {
        return pair.second.session == id && pair.second.state == DialogueConsequenceState::Deferred;
    });
    if (has_deferred) return;
    const auto *definition = GetDefinition(session->second.definition);
    const auto *node = definition ? FindNode(*definition, session->second.current_node) : nullptr;
    if (!node)
    {
        session->second.state = ConversationState::Failed;
    }
    else if (!node->options.empty())
    {
        session->second.state = ConversationState::WaitingForChoice;
    }
    else if (node->automatic_next.IsValid())
    {
        session->second.state = ConversationState::Active;
    }
    else
    {
        session->second.state = ConversationState::Completed;
        if (diagnostics_.active_sessions > 0) --diagnostics_.active_sessions;
    }
    Bump();
    session->second.context.gameplay = MergeContext(session->second.context.gameplay, context);
    session->second.revision = revision_;
}
DialogueDiagnostics DialogueService::GetDiagnostics() const noexcept
{
    return diagnostics_;
}
void DialogueService::Record(DialogueChange c)
{
    if (next_change_sequence_ != std::numeric_limits<std::uint64_t>::max())
        c.sequence = next_change_sequence_++;
    else
        c.sequence = next_change_sequence_;
    changes_.push_back(std::move(c));
    while (changes_.size() > kChangeJournalCapacity) changes_.pop_front();
}
} // namespace epidemic::gameplay::dialogue
