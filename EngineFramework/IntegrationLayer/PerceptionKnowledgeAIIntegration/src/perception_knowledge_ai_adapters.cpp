#include "Epidemic/GameFramework/PerceptionKnowledgeAIIntegration/perception_knowledge_ai_adapters.h"

namespace epidemic::gameplay::integration
{
namespace
{
[[nodiscard]] knowledge::KnowledgeConfidence Convert(perception::PerceptionConfidence c) noexcept
{
    switch (c)
    {
    case perception::PerceptionConfidence::Certain:
        return knowledge::KnowledgeConfidence::Certain;
    case perception::PerceptionConfidence::High:
        return knowledge::KnowledgeConfidence::High;
    case perception::PerceptionConfidence::Medium:
        return knowledge::KnowledgeConfidence::Medium;
    case perception::PerceptionConfidence::Low:
        return knowledge::KnowledgeConfidence::Low;
    case perception::PerceptionConfidence::None:
        return knowledge::KnowledgeConfidence::None;
    }
    return knowledge::KnowledgeConfidence::None;
}
} // namespace
PerceptionKnowledgeAdapter::PerceptionKnowledgeAdapter(knowledge::KnowledgeService &k,
                                                       PerceptionKnowledgeMapping mapping)
    : knowledge_(k), mapping_(mapping)
{
}
foundation::Result<knowledge::KnowledgeRecordId> PerceptionKnowledgeAdapter::LearnFromObservation(
    const perception::PerceptionObservation &o)
{
    auto memory = knowledge_.CreateMemory({o.perceiver,
                                           mapping_.observation_memory,
                                           o.perceived_subject,
                                           {},
                                           knowledge::MemoryImportance::Normal,
                                           knowledge::MemoryPersistencePolicy::Session,
                                           {},
                                           {},
                                           o.context});
    (void)memory;
    knowledge::KnowledgeTopic topic;
    topic.id = mapping_.observed_topic;
    topic.primary_subject = o.perceived_subject;
    return knowledge_.Learn({o.perceiver,
                             mapping_.observed_belief,
                             topic,
                             knowledge::KnowledgeSourceId::FromString("framework.knowledge.direct_perception"),
                             o.perceived_subject,
                             Convert(o.confidence),
                             {},
                             o.context});
}
KnowledgeAIAdapter::KnowledgeAIAdapter(const knowledge::KnowledgeService &k) : knowledge_(k)
{
}
ai::AIContextSnapshot KnowledgeAIAdapter::BuildContext(GameplayObjectRef agent,
                                                       const perception::PerceptionService *perception_service,
                                                       GameplayTimePoint now) const
{
    ai::AIContextSnapshot context;
    context.now = now;
    auto records = knowledge_.FindKnowledgeByOwner(agent);
    for (const auto &r : records)
    {
        if (r.confidence != knowledge::KnowledgeConfidence::None &&
            r.truth_state != knowledge::KnowledgeTruthState::Contradicted &&
            r.truth_state != knowledge::KnowledgeTruthState::Outdated)
        {
            context.known_topics.push_back(TypeId{r.topic.id.value.Raw()});
            if (r.subject.IsValid())
                context.perceived_targets.push_back(r.subject);
        }
    }
    if (perception_service)
    {
        auto obs = perception_service->FindObservationsByPerceiver(agent);
        for (const auto &o : obs)
        {
            if (o.perceived_subject.IsValid())
                context.perceived_targets.push_back(o.perceived_subject);
        }
    }
    std::sort(context.known_topics.begin(), context.known_topics.end());
    context.known_topics.erase(std::unique(context.known_topics.begin(), context.known_topics.end()),
                               context.known_topics.end());
    std::sort(context.perceived_targets.begin(), context.perceived_targets.end());
    context.perceived_targets.erase(std::unique(context.perceived_targets.begin(), context.perceived_targets.end()),
                                    context.perceived_targets.end());
    return context;
}
foundation::Result<void> AIIntentExecutionLog::Accept(const ai::AIIntent &intent, ai::AIService &service)
{
    accepted_.push_back(intent);
    std::sort(accepted_.begin(), accepted_.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return service.MarkIntentAccepted(intent.id, intent.context);
}
foundation::Result<void> AIIntentExecutionLog::Succeed(ai::AIIntentId intent, ai::AIService &service)
{
    return service.MarkIntentSucceeded(intent);
}
} // namespace epidemic::gameplay::integration
