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
bool IsValidConversationState(ConversationState s) noexcept
{
    switch (s)
    {
    case ConversationState::Active:
    case ConversationState::WaitingForChoice:
    case ConversationState::WaitingForExternalAction:
    case ConversationState::Completed:
    case ConversationState::Interrupted:
    case ConversationState::Failed:
        return true;
    }
    return false;
}
bool IsValidConsequenceState(DialogueConsequenceState s) noexcept
{
    switch (s)
    {
    case DialogueConsequenceState::Pending:
    case DialogueConsequenceState::Applied:
    case DialogueConsequenceState::Deferred:
    case DialogueConsequenceState::Failed:
    case DialogueConsequenceState::ReconciliationRequired:
        return true;
    }
    return false;
}
bool IsValidRepeatPolicy(DialogueOptionRepeatPolicy p) noexcept
{
    return p == DialogueOptionRepeatPolicy::Repeatable || p == DialogueOptionRepeatPolicy::OncePerConversation;
}
bool IsLiveConsequence(DialogueConsequenceState s) noexcept
{
    return s == DialogueConsequenceState::Pending || s == DialogueConsequenceState::Deferred ||
           s == DialogueConsequenceState::ReconciliationRequired;
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
            if (!o.id.IsValid() || !IsValidRepeatPolicy(o.repeat_policy) || !options.insert(o.id).second)
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
    DialogueOptionId source_option, GameplayContext context, MonotonicIdGenerator<GameplayObjectId> &staged_generator)
{
    try
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
            if (a->priority != b->priority) return a->priority > b->priority;
            return a->id < b->id;
        });
        std::vector<DialogueConsequenceExecution> prepared;
        prepared.reserve(ordered.size());
        for (auto *d : ordered)
        {
            DialogueConsequenceExecution e;
            e.id = DialogueConsequenceExecutionId{staged_generator.Next()};
            if (!e.id.IsValid())
                return foundation::Result<std::vector<DialogueConsequenceExecution>>::Failure(
                    Error("gameplay.dialogue.id_exhausted", "dialogue consequence id exhausted"));
            e.session = session;
            e.consequence = d->id;
            e.source_node = source_node;
            e.source_option = source_option;
            e.context = context;
            prepared.push_back(std::move(e));
        }
        return foundation::Result<std::vector<DialogueConsequenceExecution>>::Success(std::move(prepared));
    }
    catch (...)
    {
        return foundation::Result<std::vector<DialogueConsequenceExecution>>::Failure(
            Error("gameplay.dialogue.allocation_failed", "failed to prepare dialogue consequences"));
    }
}
foundation::Result<void> DialogueService::CommitPreparedConsequences(
    std::vector<DialogueConsequenceExecution> prepared, Revision revision)
{
    try
    {
        auto staged = consequences_;
        for (auto &e : prepared)
        {
            e.revision = revision;
            if (!staged.emplace(e.id, e).second)
                return foundation::Result<void>::Failure(
                    Error("gameplay.dialogue.duplicate_consequence", "duplicate dialogue consequence execution"));
        }
        consequences_.swap(staged);
        diagnostics_.consequences = consequences_.size();
        for (const auto &e : prepared)
        {
            DialogueChange c;
            c.kind = DialogueChangeKind::ConsequencePlanned;
            c.session = e.session;
            c.node = e.source_node;
            c.option = e.source_option;
            c.consequence_execution = e.id;
            c.consequence = e.consequence;
            c.consequence_state = DialogueConsequenceState::Pending;
            c.context = e.context;
            c.revision = revision;
            Record(c);
        }
        return foundation::Result<void>::Success();
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.dialogue.allocation_failed", "failed to publish dialogue consequences"));
    }
}
foundation::Result<void> DialogueService::EnterNode(ConversationSession &session, DialogueNodeId node,
                                                    GameplayContext context,
                                                    std::optional<std::vector<DialogueConsequenceExecution>> prepared_consequences)
{
    const auto *definition = GetDefinition(session.definition);
    const auto *target = definition ? FindNode(*definition, node) : nullptr;
    if (!target)
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.node_missing", "dialogue node missing"));
    const auto speaker = ResolveRole(session, target->speaker_role);
    if (!speaker.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.speaker_missing", "dialogue speaker missing"));
    if (!ConditionsPass(target->conditions, MakeConditionContext(session, speaker)))
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.node_unavailable", "dialogue node conditions failed"));
    const auto next_revision = NextRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(Error("gameplay.revision_exhausted", "dialogue revision is exhausted"));
    auto staged_generator = consequence_ids_;
    std::vector<DialogueConsequenceExecution> prepared;
    if (prepared_consequences)
        prepared = std::move(*prepared_consequences);
    else
    {
        auto result = PrepareConsequences(session.id, target->consequences, node, {},
                                          MergeContext(session.context.gameplay, context), staged_generator);
        if (!result) return foundation::Result<void>::Failure(result.GetError());
        prepared = std::move(result.Value());
    }
    try
    {
        auto staged_consequences = consequences_;
        for (auto &e : prepared)
        {
            e.revision = *next_revision;
            if (!staged_consequences.emplace(e.id, e).second)
                return foundation::Result<void>::Failure(
                    Error("gameplay.dialogue.duplicate_consequence", "duplicate dialogue consequence execution"));
        }
        ConversationSession staged_session = session;
        const bool was_active = !IsTerminal(staged_session.state);
        staged_session.current_speaker = speaker;
        staged_session.current_node = node;
        staged_session.state = target->options.empty()
                                   ? (target->automatic_next.IsValid() ? ConversationState::Active : ConversationState::Completed)
                                   : ConversationState::WaitingForChoice;
        staged_session.revision = *next_revision;
        consequences_.swap(staged_consequences);
        session = std::move(staged_session);
        consequence_ids_ = staged_generator;
        revision_ = *next_revision;
        diagnostics_.consequences = consequences_.size();
        if (session.state == ConversationState::Completed && was_active && diagnostics_.active_sessions > 0)
            --diagnostics_.active_sessions;
        for (const auto &e : prepared)
        {
            DialogueChange c;
            c.kind = DialogueChangeKind::ConsequencePlanned;
            c.session = e.session; c.node = e.source_node; c.option = e.source_option;
            c.consequence_execution = e.id; c.consequence = e.consequence;
            c.consequence_state = DialogueConsequenceState::Pending; c.context = e.context; c.revision = revision_;
            Record(c);
        }
        DialogueChange entered;
        entered.kind = DialogueChangeKind::NodeEntered; entered.session = session.id; entered.node = node;
        entered.context = context; entered.revision = revision_; Record(entered);
        if (session.state == ConversationState::Completed)
        {
            DialogueChange completed;
            completed.kind = DialogueChangeKind::ConversationCompleted; completed.session = session.id; completed.node = node;
            completed.context = context; completed.revision = revision_; Record(completed);
        }
        return foundation::Result<void>::Success();
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.allocation_failed", "failed to enter dialogue node"));
    }
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
        return foundation::Result<ConversationSessionId>::Failure(Error("gameplay.dialogue.not_frozen", "dialogue definitions are not frozen"));
    const auto *definition = GetDefinition(id);
    if (!definition || bindings.size() < 2)
        return foundation::Result<ConversationSessionId>::Failure(Error("gameplay.dialogue.cannot_start", "conversation definition missing or participants invalid"));
    try
    {
        std::unordered_set<TypeId> roles;
        std::vector<GameplayObjectRef> objects;
        objects.reserve(bindings.size());
        for (const auto &participant : bindings)
        {
            if (!participant.role.IsValid() || !participant.object.IsValid() || !roles.insert(participant.role).second)
                return foundation::Result<ConversationSessionId>::Failure(Error("gameplay.dialogue.invalid_participant", "invalid/duplicate conversation participant role"));
            if (std::any_of(objects.begin(), objects.end(), [participant](auto object) { return SameObject(object, participant.object); }))
                return foundation::Result<ConversationSessionId>::Failure(Error("gameplay.dialogue.invalid_participant", "duplicate conversation participant object"));
            objects.push_back(participant.object);
        }
        for (auto role : definition->participant_roles)
            if (!roles.contains(role))
                return foundation::Result<ConversationSessionId>::Failure(Error("gameplay.dialogue.invalid_participant", "required conversation participant role missing"));
        const auto next_revision = NextRevision();
        if (!next_revision)
            return foundation::Result<ConversationSessionId>::Failure(Error("gameplay.revision_exhausted", "dialogue revision is exhausted"));
        auto staged_session_ids = session_ids_;
        auto staged_consequence_ids = consequence_ids_;
        ConversationSession staged_session;
        staged_session.id = ConversationSessionId{staged_session_ids.Next()};
        if (!staged_session.id.IsValid())
            return foundation::Result<ConversationSessionId>::Failure(Error("gameplay.dialogue.id_exhausted", "dialogue session id exhausted"));
        staged_session.definition = id;
        staged_session.participants = std::move(objects);
        staged_session.participant_bindings = std::move(bindings);
        staged_session.context = std::move(context);
        staged_session.started_at = staged_session.context.gameplay.time;
        staged_session.state = ConversationState::Active;
        const auto *entry = FindNode(*definition, definition->entry_node);
        if (!entry)
            return foundation::Result<ConversationSessionId>::Failure(Error("gameplay.dialogue.node_missing", "dialogue entry node missing"));
        const auto speaker = ResolveRole(staged_session, entry->speaker_role);
        if (!speaker.IsValid() || !ConditionsPass(entry->conditions, MakeConditionContext(staged_session, speaker)))
            return foundation::Result<ConversationSessionId>::Failure(Error("gameplay.dialogue.node_unavailable", "dialogue entry node unavailable"));
        auto prepared_result = PrepareConsequences(staged_session.id, entry->consequences, entry->id, {},
                                                   staged_session.context.gameplay, staged_consequence_ids);
        if (!prepared_result) return foundation::Result<ConversationSessionId>::Failure(prepared_result.GetError());
        auto prepared = std::move(prepared_result.Value());
        staged_session.current_speaker = speaker;
        staged_session.current_node = entry->id;
        staged_session.state = entry->options.empty()
                                   ? (entry->automatic_next.IsValid() ? ConversationState::Active : ConversationState::Completed)
                                   : ConversationState::WaitingForChoice;
        staged_session.revision = *next_revision;
        auto staged_sessions = sessions_;
        auto staged_consequences = consequences_;
        if (!staged_sessions.emplace(staged_session.id, staged_session).second)
            return foundation::Result<ConversationSessionId>::Failure(Error("gameplay.dialogue.duplicate_session", "duplicate dialogue session id"));
        for (auto &execution : prepared)
        {
            execution.revision = *next_revision;
            if (!staged_consequences.emplace(execution.id, execution).second)
                return foundation::Result<ConversationSessionId>::Failure(Error("gameplay.dialogue.duplicate_consequence", "duplicate dialogue consequence execution"));
        }
        const auto sid = staged_session.id;
        sessions_.swap(staged_sessions);
        consequences_.swap(staged_consequences);
        session_ids_ = staged_session_ids;
        consequence_ids_ = staged_consequence_ids;
        revision_ = *next_revision;
        diagnostics_.active_sessions += IsTerminal(staged_session.state) ? 0u : 1u;
        diagnostics_.consequences = consequences_.size();
        DialogueChange started; started.kind = DialogueChangeKind::ConversationStarted; started.session = sid;
        started.node = entry->id; started.context = staged_session.context.gameplay; started.revision = revision_; Record(started);
        DialogueChange entered; entered.kind = DialogueChangeKind::NodeEntered; entered.session = sid; entered.node = entry->id;
        entered.context = staged_session.context.gameplay; entered.revision = revision_; Record(entered);
        for (const auto &execution : prepared)
        {
            DialogueChange c; c.kind = DialogueChangeKind::ConsequencePlanned; c.session = sid; c.node = execution.source_node;
            c.option = execution.source_option; c.consequence_execution = execution.id; c.consequence = execution.consequence;
            c.consequence_state = DialogueConsequenceState::Pending; c.context = execution.context; c.revision = revision_; Record(c);
        }
        if (staged_session.state == ConversationState::Completed)
        {
            DialogueChange completed; completed.kind = DialogueChangeKind::ConversationCompleted; completed.session = sid;
            completed.node = entry->id; completed.context = staged_session.context.gameplay; completed.revision = revision_; Record(completed);
        }
        return foundation::Result<ConversationSessionId>::Success(sid);
    }
    catch (...)
    {
        return foundation::Result<ConversationSessionId>::Failure(Error("gameplay.dialogue.allocation_failed", "failed to start conversation"));
    }
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
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.session_not_waiting", "conversation is not waiting for a choice"));
    if (std::none_of(it->second.participant_bindings.begin(), it->second.participant_bindings.end(),
                     [actor](const auto &p) { return SameObject(p.object, actor); }))
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.actor_invalid", "actor is not a participant"));
    const auto *definition = GetDefinition(it->second.definition);
    const auto *node = definition ? FindNode(*definition, it->second.current_node) : nullptr;
    if (!node)
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.node_missing", "dialogue node missing"));
    auto option_it = std::find_if(node->options.begin(), node->options.end(), [option](const auto &value) { return value.id == option; });
    if (option_it == node->options.end() ||
        (option_it->repeat_policy == DialogueOptionRepeatPolicy::OncePerConversation &&
         std::find(it->second.resolved_once_per_conversation_options.begin(), it->second.resolved_once_per_conversation_options.end(), option.value) != it->second.resolved_once_per_conversation_options.end()) ||
        !ConditionsPass(option_it->conditions, MakeConditionContext(it->second, actor)))
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.option_unavailable", "dialogue option unavailable"));
    const DialogueNodeDefinition *next_node = nullptr;
    GameplayObjectRef next_speaker{};
    if (option_it->next_node.IsValid())
    {
        next_node = FindNode(*definition, option_it->next_node);
        if (!next_node)
            return foundation::Result<void>::Failure(Error("gameplay.dialogue.node_missing", "dialogue node missing"));
        next_speaker = ResolveRole(it->second, next_node->speaker_role);
        if (!next_speaker.IsValid() || !ConditionsPass(next_node->conditions, MakeConditionContext(it->second, next_speaker)))
            return foundation::Result<void>::Failure(Error("gameplay.dialogue.node_unavailable", "dialogue node conditions failed"));
    }
    const auto next_revision = NextRevision();
    if (!next_revision)
        return foundation::Result<void>::Failure(Error("gameplay.revision_exhausted", "dialogue revision is exhausted"));
    try
    {
        auto staged_generator = consequence_ids_;
        auto option_result = PrepareConsequences(id, option_it->consequences, it->second.current_node, option,
                                                 MergeContext(it->second.context.gameplay, context), staged_generator);
        if (!option_result) return foundation::Result<void>::Failure(option_result.GetError());
        auto option_prepared = std::move(option_result.Value());
        std::vector<DialogueConsequenceExecution> next_prepared;
        if (next_node)
        {
            auto next_result = PrepareConsequences(id, next_node->consequences, next_node->id, {},
                                                   MergeContext(it->second.context.gameplay, context), staged_generator);
            if (!next_result) return foundation::Result<void>::Failure(next_result.GetError());
            next_prepared = std::move(next_result.Value());
        }
        auto staged_session = it->second;
        if (option_it->repeat_policy == DialogueOptionRepeatPolicy::OncePerConversation)
        {
            staged_session.resolved_once_per_conversation_options.push_back(option.value);
            std::sort(staged_session.resolved_once_per_conversation_options.begin(), staged_session.resolved_once_per_conversation_options.end());
            staged_session.resolved_once_per_conversation_options.erase(
                std::unique(staged_session.resolved_once_per_conversation_options.begin(), staged_session.resolved_once_per_conversation_options.end()),
                staged_session.resolved_once_per_conversation_options.end());
        }
        if (next_node)
        {
            staged_session.current_speaker = next_speaker;
            staged_session.current_node = next_node->id;
            staged_session.state = next_node->options.empty()
                                       ? (next_node->automatic_next.IsValid() ? ConversationState::Active : ConversationState::Completed)
                                       : ConversationState::WaitingForChoice;
        }
        else
            staged_session.state = ConversationState::Completed;
        staged_session.revision = *next_revision;
        auto staged_consequences = consequences_;
        for (auto &execution : option_prepared)
        {
            execution.revision = *next_revision;
            if (!staged_consequences.emplace(execution.id, execution).second)
                return foundation::Result<void>::Failure(Error("gameplay.dialogue.duplicate_consequence", "duplicate dialogue consequence execution"));
        }
        for (auto &execution : next_prepared)
        {
            execution.revision = *next_revision;
            if (!staged_consequences.emplace(execution.id, execution).second)
                return foundation::Result<void>::Failure(Error("gameplay.dialogue.duplicate_consequence", "duplicate dialogue consequence execution"));
        }
        const bool was_active = !IsTerminal(it->second.state);
        it->second = std::move(staged_session);
        consequences_.swap(staged_consequences);
        consequence_ids_ = staged_generator;
        revision_ = *next_revision;
        ++diagnostics_.options_selected;
        diagnostics_.consequences = consequences_.size();
        if (IsTerminal(it->second.state) && was_active && diagnostics_.active_sessions > 0) --diagnostics_.active_sessions;
        DialogueChange selected; selected.kind = DialogueChangeKind::OptionSelected; selected.session = id;
        selected.node = node->id; selected.option = option; selected.context = context; selected.revision = revision_; Record(selected);
        for (const auto &execution : option_prepared)
        {
            DialogueChange c; c.kind = DialogueChangeKind::ConsequencePlanned; c.session = id; c.node = execution.source_node;
            c.option = execution.source_option; c.consequence_execution = execution.id; c.consequence = execution.consequence;
            c.consequence_state = DialogueConsequenceState::Pending; c.context = execution.context; c.revision = revision_; Record(c);
        }
        if (next_node)
        {
            DialogueChange entered; entered.kind = DialogueChangeKind::NodeEntered; entered.session = id; entered.node = next_node->id;
            entered.context = context; entered.revision = revision_; Record(entered);
            for (const auto &execution : next_prepared)
            {
                DialogueChange c; c.kind = DialogueChangeKind::ConsequencePlanned; c.session = id; c.node = execution.source_node;
                c.consequence_execution = execution.id; c.consequence = execution.consequence;
                c.consequence_state = DialogueConsequenceState::Pending; c.context = execution.context; c.revision = revision_; Record(c);
            }
        }
        if (it->second.state == ConversationState::Completed)
        {
            DialogueChange completed; completed.kind = DialogueChangeKind::ConversationCompleted; completed.session = id;
            completed.node = it->second.current_node; completed.option = option; completed.context = context; completed.revision = revision_; Record(completed);
            MaybeCleanupTerminalSession(id);
        }
        return foundation::Result<void>::Success();
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.allocation_failed", "failed to select dialogue option"));
    }
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
    const auto next = NextRevision();
    if (!next)
        return foundation::Result<void>::Failure(Error("gameplay.revision_exhausted", "dialogue revision is exhausted"));
    it->second.state = ConversationState::Interrupted;
    it->second.revision = *next;
    revision_ = *next;
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
    try
    {
        for (const auto &[id, execution] : consequences_)
            if (execution.state == DialogueConsequenceState::Pending) ids.push_back(id);
        std::sort(ids.begin(), ids.end(), [this](auto a, auto b) {
            const auto &ea = consequences_.at(a); const auto &eb = consequences_.at(b);
            const auto da = consequence_defs_.find(ea.consequence); const auto db = consequence_defs_.find(eb.consequence);
            const auto pa = da == consequence_defs_.end() ? std::numeric_limits<std::int32_t>::min() : da->second.priority;
            const auto pb = db == consequence_defs_.end() ? std::numeric_limits<std::int32_t>::min() : db->second.priority;
            return pa != pb ? pa > pb : a < b;
        });
        if (ids.size() > budget) ids.resize(budget);
    }
    catch (...)
    {
        return {};
    }
    std::vector<DialogueConsequenceExecutionId> applied;
    applied.reserve(ids.size());
    for (auto id : ids)
    {
        auto execution_it = consequences_.find(id);
        if (execution_it == consequences_.end() || execution_it->second.state != DialogueConsequenceState::Pending) continue;
        auto definition = consequence_defs_.find(execution_it->second.consequence);
        auto session = sessions_.find(execution_it->second.session);
        DialogueConsequenceState outcome = DialogueConsequenceState::Failed;
        bool ambiguous = false;
        if (definition != consequence_defs_.end() && session != sessions_.end())
        {
            auto handler = consequence_handlers_.find(definition->second.type);
            if (handler == consequence_handlers_.end()) outcome = DialogueConsequenceState::Deferred;
            else
            {
                try
                {
                    outcome = handler->second->Execute(definition->second, execution_it->second, session->second);
                    if (!IsValidConsequenceState(outcome) || outcome == DialogueConsequenceState::Pending ||
                        outcome == DialogueConsequenceState::ReconciliationRequired)
                        ambiguous = true;
                }
                catch (...)
                {
                    ambiguous = true;
                }
            }
        }
        if (ambiguous) outcome = DialogueConsequenceState::ReconciliationRequired;
        if (outcome == execution_it->second.state) continue;
        const auto next_revision = NextRevision();
        if (!next_revision) break;
        const auto session_id = execution_it->second.session;
        execution_it->second.state = outcome;
        execution_it->second.revision = *next_revision;
        revision_ = *next_revision;
        DialogueChange change;
        change.kind = outcome == DialogueConsequenceState::Applied ? DialogueChangeKind::ConsequenceApplied
                    : outcome == DialogueConsequenceState::Deferred ? DialogueChangeKind::ConsequenceDeferred
                    : outcome == DialogueConsequenceState::ReconciliationRequired ? DialogueChangeKind::ConsequenceReconciliationRequired
                    : DialogueChangeKind::ConsequenceFailed;
        change.session = session_id; change.node = execution_it->second.source_node; change.option = execution_it->second.source_option;
        change.consequence_execution = execution_it->second.id; change.consequence = execution_it->second.consequence;
        change.consequence_state = outcome; change.context = execution_it->second.context; change.revision = revision_; Record(change);
        if (outcome == DialogueConsequenceState::Deferred || outcome == DialogueConsequenceState::ReconciliationRequired)
        {
            auto sit = sessions_.find(session_id);
            if (sit != sessions_.end() && sit->second.state != ConversationState::WaitingForExternalAction)
            {
                const bool was_terminal = IsTerminal(sit->second.state);
                sit->second.state = ConversationState::WaitingForExternalAction;
                sit->second.revision = revision_;
                if (was_terminal) ++diagnostics_.active_sessions;
            }
        }
        else
            RefreshExternalWaitState(session_id, execution_it->second.context);
        if (outcome == DialogueConsequenceState::Applied) applied.push_back(id);
    }
    for (auto it = consequences_.begin(); it != consequences_.end();)
        if (!IsLiveConsequence(it->second.state)) it = consequences_.erase(it); else ++it;
    diagnostics_.consequences = consequences_.size();
    std::vector<ConversationSessionId> terminal_sessions;
    try
    {
        for (const auto &[sid, session] : sessions_) if (IsTerminal(session.state)) terminal_sessions.push_back(sid);
    }
    catch (...) { return applied; }
    for (auto sid : terminal_sessions) MaybeCleanupTerminalSession(sid);
    return applied;
}
foundation::Result<void> DialogueService::ResumeConsequence(DialogueConsequenceExecutionId id, GameplayContext context)
{
    auto it = consequences_.find(id);
    if (it == consequences_.end())
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.consequence_missing", "dialogue consequence execution missing"));
    if (it->second.state != DialogueConsequenceState::Deferred)
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.consequence_not_deferred", "dialogue consequence is not deferred"));
    const auto next = NextRevision();
    if (!next) return foundation::Result<void>::Failure(Error("gameplay.revision_exhausted", "dialogue revision is exhausted"));
    it->second.state = DialogueConsequenceState::Pending;
    it->second.context = MergeContext(it->second.context, context);
    it->second.revision = *next; revision_ = *next;
    DialogueChange change; change.kind = DialogueChangeKind::ConsequenceResumed; change.session = it->second.session;
    change.node = it->second.source_node; change.option = it->second.source_option; change.consequence_execution = id;
    change.consequence = it->second.consequence; change.consequence_state = DialogueConsequenceState::Pending;
    change.context = it->second.context; change.revision = revision_; Record(change);
    return foundation::Result<void>::Success();
}

foundation::Result<void> DialogueService::ResolveConsequenceReconciliation(
    DialogueConsequenceExecutionId id, bool confirmed_applied, GameplayContext context)
{
    auto it = consequences_.find(id);
    if (it == consequences_.end())
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.consequence_missing", "dialogue consequence execution missing"));
    if (it->second.state != DialogueConsequenceState::ReconciliationRequired)
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.consequence_not_reconciling", "dialogue consequence is not awaiting reconciliation"));
    const auto next = NextRevision();
    if (!next) return foundation::Result<void>::Failure(Error("gameplay.revision_exhausted", "dialogue revision is exhausted"));
    const auto session_id = it->second.session;
    it->second.state = confirmed_applied ? DialogueConsequenceState::Applied : DialogueConsequenceState::Pending;
    it->second.context = MergeContext(it->second.context, context);
    it->second.revision = *next; revision_ = *next;
    DialogueChange change; change.kind = confirmed_applied ? DialogueChangeKind::ConsequenceApplied : DialogueChangeKind::ConsequenceResumed;
    change.session = session_id; change.node = it->second.source_node; change.option = it->second.source_option;
    change.consequence_execution = id; change.consequence = it->second.consequence; change.consequence_state = it->second.state;
    change.context = it->second.context; change.revision = revision_; Record(change);
    if (confirmed_applied)
    {
        consequences_.erase(it);
        diagnostics_.consequences = consequences_.size();
        RefreshExternalWaitState(session_id, context);
        MaybeCleanupTerminalSession(session_id);
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> DialogueService::FailConsequence(DialogueConsequenceExecutionId id, TypeId reason, GameplayContext context)
{
    auto it = consequences_.find(id);
    if (it == consequences_.end())
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.consequence_missing", "dialogue consequence execution missing"));
    if (it->second.state != DialogueConsequenceState::Deferred)
        return foundation::Result<void>::Failure(Error("gameplay.dialogue.consequence_not_deferred", "dialogue consequence is not deferred"));
    const auto next = NextRevision();
    if (!next)
        return foundation::Result<void>::Failure(Error("gameplay.revision_exhausted", "dialogue revision is exhausted"));
    const auto session_id = it->second.session;
    it->second.state = DialogueConsequenceState::Failed;
    it->second.context = MergeContext(it->second.context, context);
    it->second.revision = *next;
    revision_ = *next;
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
DialogueChangeBatch DialogueService::ReadChangesSinceSequence(std::uint64_t seq) const
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
std::vector<DialogueChange> DialogueService::ChangesSinceSequence(std::uint64_t seq) const
{
    return ReadChangesSinceSequence(seq).changes;
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
    s.change_epoch = journal_epoch_;
    return s;
}
foundation::Result<void> DialogueService::RestoreSnapshot(DialogueSnapshot s)
{
    const auto next_journal_epoch = CheckedNextChangeEpoch(s.change_epoch > journal_epoch_ ? s.change_epoch : journal_epoch_);
    if (!next_journal_epoch)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.change_journal.epoch_exhausted", "change journal epoch is exhausted"));
    }
    std::unordered_map<ConversationSessionId, ConversationSession, IdHash> sessions;
    for (auto &v : s.sessions)
    {
        const auto *d = GetDefinition(v.definition);
        const auto *node = d ? FindNode(*d, v.current_node) : nullptr;
        if (!v.id.IsValid() || !d || !node || !IsValidConversationState(v.state) || sessions.contains(v.id) || v.revision > s.revision)
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
            !IsValidConsequenceState(v.state) || !IsLiveConsequence(v.state) ||
            consequences.contains(v.id) || v.revision > s.revision)
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
    journal_epoch_ = *next_journal_epoch;
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
    session->second.context.gameplay = MergeContext(session->second.context.gameplay, context);
    session->second.revision = revision_;
}
DialogueDiagnostics DialogueService::GetDiagnostics() const noexcept
{
    return diagnostics_;
}
std::optional<Revision> DialogueService::NextRevision() const noexcept
{
    return CheckedNext(revision_);
}
void DialogueService::Record(DialogueChange c) noexcept
{
    auto invalidate = [this]() noexcept {
        const auto epoch = CheckedNextChangeEpoch(journal_epoch_);
        changes_.clear();
        if (epoch) { journal_epoch_ = *epoch; next_change_sequence_ = 1; }
        else next_change_sequence_ = 0;
    };
    if (next_change_sequence_ == 0 || next_change_sequence_ == std::numeric_limits<std::uint64_t>::max())
    {
        invalidate();
        if (next_change_sequence_ == 0) return;
    }
    c.sequence = next_change_sequence_;
    try { changes_.push_back(std::move(c)); }
    catch (...) { invalidate(); return; }
    ++next_change_sequence_;
    while (changes_.size() > kChangeJournalCapacity) changes_.pop_front();
}
} // namespace epidemic::gameplay::dialogue
