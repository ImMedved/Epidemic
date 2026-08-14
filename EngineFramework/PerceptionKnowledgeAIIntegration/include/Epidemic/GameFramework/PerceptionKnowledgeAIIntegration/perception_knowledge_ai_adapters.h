#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/AI/ai.h"
#include "Epidemic/GameFramework/Knowledge/knowledge.h"
#include "Epidemic/GameFramework/Perception/perception.h"

#include <vector>

namespace epidemic::gameplay::integration
{
struct PerceptionKnowledgeMapping
{
    knowledge::BeliefTypeId observed_belief{knowledge::BeliefTypeId::FromString("framework.knowledge.observed")};
    knowledge::KnowledgeTopicId observed_topic{knowledge::KnowledgeTopicId::FromString("framework.knowledge.observed_subject")};
    knowledge::MemoryTypeId observation_memory{knowledge::MemoryTypeId::FromString("framework.memory.perception_observation")};
};

class PerceptionKnowledgeAdapter
{
public:
    explicit PerceptionKnowledgeAdapter(knowledge::KnowledgeService& knowledge_service,PerceptionKnowledgeMapping mapping={});
    [[nodiscard]] foundation::Result<knowledge::KnowledgeRecordId> LearnFromObservation(const perception::PerceptionObservation& observation);
private:
    knowledge::KnowledgeService& knowledge_;
    PerceptionKnowledgeMapping mapping_;
};

class KnowledgeAIAdapter
{
public:
    explicit KnowledgeAIAdapter(const knowledge::KnowledgeService& knowledge_service);
    [[nodiscard]] ai::AIContextSnapshot BuildContext(GameplayObjectRef agent,const perception::PerceptionService* perception_service=nullptr,GameplayTimePoint now={}) const;
private:
    const knowledge::KnowledgeService& knowledge_;
};

class AIIntentExecutionLog
{
public:
    [[nodiscard]] foundation::Result<void> Accept(const ai::AIIntent& intent,ai::AIService& ai_service);
    [[nodiscard]] foundation::Result<void> Succeed(ai::AIIntentId intent,ai::AIService& ai_service);
    [[nodiscard]] const std::vector<ai::AIIntent>& AcceptedIntents() const noexcept{return accepted_;}
private:
    std::vector<ai::AIIntent> accepted_;
};
} // namespace epidemic::gameplay::integration
