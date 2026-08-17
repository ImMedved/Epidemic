#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Narrative/narrative.h"

#include <vector>

namespace epidemic::gameplay::narrative_integration
{
class NarrativeSemanticEventAdapter
{
public:
    [[nodiscard]] foundation::Result<void> ProcessDiscovery(
        narrative::NarrativeService& service,
        GameplayObjectRef discoverer,
        GameplayObjectRef subject,
        GameplayObjectRef area,
        TypeId topic,
        GameplayContext context) const;

    [[nodiscard]] foundation::Result<void> ProcessWorldEvent(
        narrative::NarrativeService& service,
        GameplayObjectRef subject,
        GameplayObjectRef area,
        TagId event_tag,
        GameplayContext context) const;

    [[nodiscard]] foundation::Result<void> ProcessEncounterCompleted(
        narrative::NarrativeService& service,
        GameplayObjectRef encounter,
        GameplayObjectRef actor,
        GameplayObjectRef area,
        GameplayContext context) const;
};

struct RecordedNarrativeConsequence
{
    narrative::NarrativeConsequenceExecutionId execution{};
    narrative::NarrativeConsequenceId consequence{};
    narrative::NarrativeThreadId thread{};
    GameplayObjectRef owner{};
};

class NarrativeConsequenceRecorder final : public narrative::INarrativeConsequenceHandler
{
public:
    [[nodiscard]] narrative::NarrativeConsequenceResult Execute(
        const narrative::NarrativeConsequenceDefinition& definition,
        const narrative::NarrativeConsequenceExecution& execution,
        const narrative::NarrativeExecutionContext& context) const override;

    [[nodiscard]] const std::vector<RecordedNarrativeConsequence>& Recorded() const noexcept { return recorded_; }
    void Clear() const { recorded_.clear(); }
private:
    mutable std::vector<RecordedNarrativeConsequence> recorded_;
};

class NarrativeKnowledgeAdapter
{
public:
    [[nodiscard]] foundation::Result<narrative::ClueId> CreateClueFromKnowledge(
        narrative::NarrativeService& service,
        GameplayObjectRef owner,
        TypeId topic,
        GameplayObjectRef area,
        std::int64_t confidence,
        GameplayContext context) const;

    [[nodiscard]] foundation::Result<narrative::RumorId> CreateRumorFromSharedKnowledge(
        narrative::NarrativeService& service,
        GameplayObjectRef scope,
        TypeId topic,
        std::int64_t confidence,
        GameplayDuration lifetime,
        GameplayContext context) const;
};
}
