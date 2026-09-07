#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/AI/ai.h"
#include "Epidemic/GameFramework/Knowledge/knowledge.h"
#include "Epidemic/GameFramework/Perception/perception.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace epidemic::gameplay::integration
{
struct PerceptionKnowledgeMapping
{
    knowledge::BeliefTypeId observed_belief{knowledge::BeliefTypeId::FromString("framework.knowledge.observed")};
    knowledge::KnowledgeTopicId observed_topic{
        knowledge::KnowledgeTopicId::FromString("framework.knowledge.observed_subject")};
    knowledge::MemoryTypeId observation_memory{
        knowledge::MemoryTypeId::FromString("framework.memory.perception_observation")};
};

struct ObservationKnowledgeResult
{
    // Knowledge is semantically mandatory on success; optional is retained for source compatibility.
    std::optional<knowledge::KnowledgeRecordId> knowledge;
    std::optional<knowledge::MemoryRecordId> memory;
    bool memory_creation_failed = false;
    std::optional<foundation::Error> memory_error;
};

using ObservationLearningResult = ObservationKnowledgeResult;

class PerceptionKnowledgeAdapter
{
  public:
    explicit PerceptionKnowledgeAdapter(knowledge::KnowledgeService &knowledge_service,
                                        PerceptionKnowledgeMapping mapping = {});
    [[nodiscard]] foundation::Result<ObservationKnowledgeResult>
    LearnFromObservation(const perception::PerceptionObservation &observation);

  private:
    knowledge::KnowledgeService &knowledge_;
    PerceptionKnowledgeMapping mapping_;
};

struct AIExecutionAvailability
{
    bool materialized = false;
    bool runtime_projection_available = false;
};

struct KnowledgeAIInputKeys
{
    ai::AIInputKeyId knowledge_confidence{
        ai::AIInputKeyId::FromString("framework.ai.target.knowledge_confidence")};
    ai::AIInputKeyId current_perception{
        ai::AIInputKeyId::FromString("framework.ai.target.source.current_perception")};
    ai::AIInputKeyId knowledge_source{ai::AIInputKeyId::FromString("framework.ai.target.source.knowledge")};
    ai::AIInputKeyId knowledge_semantics{
        ai::AIInputKeyId::FromString("framework.ai.target.knowledge_semantics")};
    ai::AIInputKeyId last_known_source{ai::AIInputKeyId::FromString("framework.ai.target.source.last_known")};
    ai::AIInputKeyId perception_confidence{
        ai::AIInputKeyId::FromString("framework.ai.target.perception_confidence")};
    ai::AIInputKeyId identity_confidence{
        ai::AIInputKeyId::FromString("framework.ai.target.identity_confidence")};
    ai::AIInputKeyId position_uncertainty{
        ai::AIInputKeyId::FromString("framework.ai.target.position_uncertainty")};
    ai::AIInputKeyId perception_sense{ai::AIInputKeyId::FromString("framework.ai.target.perception_sense")};
    ai::AIInputKeyId perception_semantics{
        ai::AIInputKeyId::FromString("framework.ai.target.perception_semantics")};
};

class KnowledgeAIAdapter
{
  public:
    explicit KnowledgeAIAdapter(const knowledge::KnowledgeService &knowledge_service,
                                KnowledgeAIInputKeys input_keys = {});
    [[nodiscard]] ai::AIContextSnapshot BuildContext(
        GameplayObjectRef agent, AIExecutionAvailability availability,
        const perception::PerceptionService *perception_service = nullptr, GameplayTimePoint now = {}) const;
    // Compatibility overload: availability is conservatively unknown/unavailable.
    [[nodiscard]] ai::AIContextSnapshot BuildContext(GameplayObjectRef agent,
                                                     const perception::PerceptionService *perception_service = nullptr,
                                                     GameplayTimePoint now = {}) const;

  private:
    const knowledge::KnowledgeService &knowledge_;
    KnowledgeAIInputKeys input_keys_;
};

class AIIntentExecutionRecorder
{
  public:
    explicit AIIntentExecutionRecorder(std::size_t retention_capacity = 1024) noexcept;

    [[nodiscard]] foundation::Result<void> Accept(const ai::AIIntent &intent, ai::AIService &ai_service);
    [[nodiscard]] foundation::Result<void> Succeed(ai::AIIntentId intent, ai::AIService &ai_service);
    [[nodiscard]] const std::vector<ai::AIIntent> &AcceptedIntents() const noexcept { return accepted_; }
    [[nodiscard]] std::size_t RetentionCapacity() const noexcept { return retention_capacity_; }
    void Clear() noexcept { accepted_.clear(); }

  private:
    void RecordAccepted(const ai::AIIntent &intent);

    std::size_t retention_capacity_ = 1024;
    std::vector<ai::AIIntent> accepted_;
};

// This object is diagnostic only; AIService remains the authoritative intent state owner.
using AIIntentExecutionLog = AIIntentExecutionRecorder;
} // namespace epidemic::gameplay::integration
