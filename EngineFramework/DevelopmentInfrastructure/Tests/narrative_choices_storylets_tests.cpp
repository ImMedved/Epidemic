#include "Epidemic/GameFramework/Narrative/narrative.h"
#include <cstdlib>
#include <iostream>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::narrative;

namespace
{
void Check(bool value, const char *message)
{
    if (!value)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
GameplayObjectRef Ref(const char *domain, const char *id)
{
    return {GameplayDomainId::FromString(domain), GameplayObjectId::FromString(id)};
}
class AlwaysResolver final : public INarrativeConditionResolver
{
  public:
    NarrativeConditionResult Evaluate(const NarrativeConditionDefinition &d,
                                      const NarrativeEvaluationContext &) const override
    {
        return {d.id, ConditionEvaluationState::Satisfied, 1'000'000, {}, {}};
    }
};
class Recorder final : public INarrativeConsequenceHandler
{
  public:
    NarrativeConsequenceResult Execute(const NarrativeConsequenceDefinition &d, const NarrativeConsequenceExecution &e,
                                       const NarrativeExecutionContext &c) const override
    {
        executions.push_back(e.id);
        owners.push_back(c.default_owner);
        types.push_back(d.type);
        return {ConsequenceExecutionState::Applied, {}};
    }
    mutable std::vector<NarrativeConsequenceExecutionId> executions;
    mutable std::vector<GameplayObjectRef> owners;
    mutable std::vector<NarrativeConsequenceTypeId> types;
};
} // namespace

int main()
{
    NarrativeService service;
    AlwaysResolver always;
    Recorder recorder;
    auto always_type = NarrativeConditionTypeId::FromString("condition.always");
    auto effect_type = NarrativeConsequenceTypeId::FromString("narrative.semantic_effect");
    Check(static_cast<bool>(service.RegisterConditionResolver(always_type, always)), "always resolver");
    Check(static_cast<bool>(service.RegisterConsequenceHandler(effect_type, recorder)), "recorder handler");

    auto thread = NarrativeThreadId::FromString("thread.bandit_choice");
    NarrativeThreadDefinition t;
    t.id = thread;
    t.initial_state = NarrativeRuntimeState::Available;
    Check(static_cast<bool>(service.RegisterThreadDefinition(t)), "thread");
    NarrativeConditionDefinition c;
    c.id = NarrativeConditionId::FromString("condition.available");
    c.type = always_type;
    Check(static_cast<bool>(service.RegisterConditionDefinition(c)), "condition");
    NarrativeConsequenceDefinition cons;
    cons.id = NarrativeConsequenceId::FromString("consequence.spare");
    cons.type = effect_type;
    Check(static_cast<bool>(service.RegisterConsequenceDefinition(cons)), "consequence");
    StoryletDefinition story;
    story.id = StoryletId::FromString("storylet.rumor");
    story.availability_conditions.push_back(c.id);
    story.consequences.push_back(cons.id);
    story.max_activations = 1;
    story.cooldown = GameplayDuration{100};
    Check(static_cast<bool>(service.RegisterStoryletDefinition(story)), "storylet");
    Check(static_cast<bool>(service.FreezeDefinitions()), "freeze");

    auto player = Ref("game.actor", "player");
    NarrativeChoice choice;
    choice.thread = thread;
    choice.actor = player;
    NarrativeChoiceOption option;
    option.id = NarrativeChoiceOptionId::FromString("choice.spare");
    option.availability_conditions.push_back(c.id);
    option.consequences.push_back(cons.id);
    choice.options.push_back(option);
    auto choice_id = service.CreateChoice(choice, {.time = GameplayTimePoint{10}, .actor = player});
    Check(static_cast<bool>(choice_id), "create choice");
    Check(static_cast<bool>(service.ResolveChoice(choice_id.Value(), option.id,
                                                  {.time = GameplayTimePoint{11},
                                                   .correlation = CorrelationId::FromString("choice.spare"),
                                                   .actor = player})),
          "resolve choice");
    Check(!static_cast<bool>(
              service.ResolveChoice(choice_id.Value(), option.id, {.time = GameplayTimePoint{12}, .actor = player})),
          "choice cannot resolve twice");
    auto applied = service.ExecutePendingConsequences({.default_owner = player, .now = GameplayTimePoint{12}}, 8);
    Check(applied.size() == 1, "choice consequence applied");

    NarrativeEvent event;
    event.type = NarrativeEventTypeId::FromString("storylet.event");
    event.instigator = player;
    event.subject = player;
    event.time = GameplayTimePoint{20};
    event.correlation = CorrelationId::FromString("storylet.once");
    Check(static_cast<bool>(service.ProcessNarrativeEvent(event, {.max_condition_evaluations = 32,
                                                                  .max_storylets_evaluated = 8,
                                                                  .max_storylets_activated = 8,
                                                                  .max_consequences_planned = 8})),
          "storylet event");
    Check(service.FindConsequences(ConsequenceExecutionState::Pending).size() == 1,
          "storylet consequence planned once");
    Check(service.ExecutePendingConsequences({.default_owner = player, .now = GameplayTimePoint{21}}, 8).size() == 1,
          "storylet consequence applied");
    NarrativeEvent second = event;
    second.time = GameplayTimePoint{30};
    second.correlation = CorrelationId::FromString("storylet.twice");
    Check(static_cast<bool>(service.ProcessNarrativeEvent(second)), "second storylet event");
    Check(service.FindConsequences(ConsequenceExecutionState::Pending).empty(),
          "max activations prevents repeated storylet");

    auto diagnostics = service.GetDiagnostics();
    Check(diagnostics.storylets_activated == 1, "diagnostics storylet activation");
    Check(recorder.executions.size() == 2, "recorded choice and storylet effects");

    NarrativeChoice invalid_choice;
    invalid_choice.thread = thread;
    invalid_choice.actor = player;
    NarrativeChoiceOption invalid_option;
    invalid_option.id = NarrativeChoiceOptionId::FromString("choice.invalid");
    invalid_option.consequences.push_back(NarrativeConsequenceId::FromString("consequence.missing"));
    invalid_choice.options.push_back(invalid_option);
    auto invalid_choice_id = service.CreateChoice(invalid_choice, {.time = GameplayTimePoint{40}, .actor = player});
    Check(static_cast<bool>(invalid_choice_id), "create invalid consequence choice for atomicity test");
    Check(!static_cast<bool>(service.ResolveChoice(invalid_choice_id.Value(), invalid_option.id,
                                                   {.time = GameplayTimePoint{41}, .actor = player})),
          "choice consequence planning failure reported");
    Check(service.GetChoice(invalid_choice_id.Value())->state == NarrativeChoiceState::Open,
          "failed consequence planning leaves choice open");

    NarrativeChoice expiring;
    expiring.thread = thread;
    expiring.actor = player;
    expiring.expires_at = GameplayTimePoint{50};
    auto expiring_id = service.CreateChoice(expiring, {.time = GameplayTimePoint{45}, .actor = player});
    Check(static_cast<bool>(expiring_id), "create expiring choice");
    Check(service.ExpireChoices(GameplayTimePoint{50}, {.time = GameplayTimePoint{50}, .actor = player}).size() == 1,
          "choice expiry lifecycle");
    Check(service.GetChoice(expiring_id.Value())->state == NarrativeChoiceState::Expired, "choice expired state");
    return 0;
}
