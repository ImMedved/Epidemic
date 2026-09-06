#include "Epidemic/GameFramework/PerceptionKnowledgeAIIntegration/perception_knowledge_ai_adapters.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <type_traits>
#include <utility>

namespace epidemic::gameplay::integration
{
namespace
{
constexpr std::uint32_t kObservationPayloadVersion = 1;
constexpr ai::Fixed kAIUnit = 1'000'000;

[[nodiscard]] knowledge::KnowledgeConfidence Convert(perception::PerceptionConfidence confidence) noexcept
{
    switch (confidence)
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

[[nodiscard]] ai::Fixed ConfidenceMicro(knowledge::KnowledgeConfidence confidence) noexcept
{
    switch (confidence)
    {
    case knowledge::KnowledgeConfidence::None:
        return 0;
    case knowledge::KnowledgeConfidence::Low:
        return 250'000;
    case knowledge::KnowledgeConfidence::Medium:
        return 500'000;
    case knowledge::KnowledgeConfidence::High:
        return 750'000;
    case knowledge::KnowledgeConfidence::Certain:
        return kAIUnit;
    }
    return 0;
}

[[nodiscard]] ai::Fixed ConfidenceMicro(perception::PerceptionConfidence confidence) noexcept
{
    switch (confidence)
    {
    case perception::PerceptionConfidence::None:
        return 0;
    case perception::PerceptionConfidence::Low:
        return 250'000;
    case perception::PerceptionConfidence::Medium:
        return 500'000;
    case perception::PerceptionConfidence::High:
        return 750'000;
    case perception::PerceptionConfidence::Certain:
        return kAIUnit;
    }
    return 0;
}

template <class UInt> void AppendUnsigned(std::vector<std::byte> &bytes, UInt value)
{
    static_assert(std::is_unsigned_v<UInt>);
    for (std::size_t i = 0; i < sizeof(UInt); ++i)
        bytes.push_back(static_cast<std::byte>((value >> (i * 8U)) & static_cast<UInt>(0xffU)));
}

void AppendI64(std::vector<std::byte> &bytes, std::int64_t value)
{
    AppendUnsigned(bytes, static_cast<std::uint64_t>(value));
}

[[nodiscard]] std::vector<std::byte> ObservationPayload(const perception::PerceptionObservation &observation)
{
    std::vector<std::byte> bytes;
    const auto &tags = observation.observed_tags.Values();
    bytes.reserve(4 + 8 * 8 + tags.size() * 8);
    AppendUnsigned(bytes, kObservationPayloadVersion);
    AppendUnsigned(bytes, observation.sense.value.Raw());
    AppendI64(bytes, observation.perceived_position.x_mm);
    AppendI64(bytes, observation.perceived_position.y_mm);
    AppendI64(bytes, observation.perceived_position.z_mm);
    AppendI64(bytes, observation.position_uncertainty_mm);
    AppendI64(bytes, observation.identity_confidence_micro);
    AppendI64(bytes, observation.observed_at.ticks);
    AppendUnsigned(bytes, static_cast<std::uint32_t>(observation.confidence));
    AppendUnsigned(bytes, static_cast<std::uint32_t>(tags.size()));
    for (const auto tag : tags)
        AppendUnsigned(bytes, tag.Raw());
    return bytes;
}

[[nodiscard]] ai::AIInputValue BoolInput(ai::AIInputKeyId key, ai::AIAccessFlag access, bool value = true)
{
    ai::AIInputValue input;
    input.key = key;
    input.kind = ai::AIInputValueKind::Boolean;
    input.access = access;
    input.bool_value = value;
    return input;
}

[[nodiscard]] ai::AIInputValue FixedInput(ai::AIInputKeyId key, ai::AIAccessFlag access, ai::Fixed value)
{
    ai::AIInputValue input;
    input.key = key;
    input.kind = ai::AIInputValueKind::Fixed;
    input.access = access;
    input.fixed_value = value;
    return input;
}

[[nodiscard]] ai::AIInputValue TypeInput(ai::AIInputKeyId key, ai::AIAccessFlag access, TypeId value)
{
    ai::AIInputValue input;
    input.key = key;
    input.kind = ai::AIInputValueKind::Type;
    input.access = access;
    input.type_value = value;
    return input;
}

[[nodiscard]] ai::AIInputValue PayloadInput(ai::AIInputKeyId key, ai::AIAccessFlag access,
                                            std::vector<std::byte> payload)
{
    ai::AIInputValue input;
    input.key = key;
    input.kind = ai::AIInputValueKind::Payload;
    input.access = access;
    input.payload = std::move(payload);
    return input;
}

void UpsertInput(std::vector<ai::AIInputValue> &inputs, ai::AIInputValue input)
{
    auto found = std::find_if(inputs.begin(), inputs.end(), [&](const ai::AIInputValue &existing) {
        return existing.key == input.key;
    });
    if (found == inputs.end())
        inputs.push_back(std::move(input));
    else
        *found = std::move(input);
}

struct CandidateState
{
    ai::AITargetCandidate candidate;
    bool has_current_perception = false;
    bool has_knowledge = false;
    bool has_last_known = false;
    ai::Fixed knowledge_confidence = 0;
    std::vector<TagId> knowledge_tags;
};

void FinalizeCandidate(CandidateState &state, const KnowledgeAIInputKeys &keys)
{
    if (state.has_current_perception)
        state.candidate.source = ai::AITargetSource::Perceived;
    else if (state.has_last_known)
        state.candidate.source = ai::AITargetSource::LastKnown;
    else
        state.candidate.source = ai::AITargetSource::Known;

    if (state.has_current_perception)
        UpsertInput(state.candidate.inputs,
                    BoolInput(keys.current_perception, ai::AIAccessFlag::PerceivedState));
    if (state.has_knowledge)
    {
        UpsertInput(state.candidate.inputs, BoolInput(keys.knowledge_source, ai::AIAccessFlag::KnownState));
        UpsertInput(state.candidate.inputs,
                    FixedInput(keys.knowledge_confidence, ai::AIAccessFlag::KnownState, state.knowledge_confidence));
        std::sort(state.knowledge_tags.begin(), state.knowledge_tags.end());
        state.knowledge_tags.erase(std::unique(state.knowledge_tags.begin(), state.knowledge_tags.end()),
                                   state.knowledge_tags.end());
        std::vector<std::byte> tag_payload;
        AppendUnsigned(tag_payload, static_cast<std::uint32_t>(state.knowledge_tags.size()));
        for (const auto tag : state.knowledge_tags)
            AppendUnsigned(tag_payload, tag.Raw());
        UpsertInput(state.candidate.inputs,
                    PayloadInput(keys.knowledge_semantics, ai::AIAccessFlag::KnownState, std::move(tag_payload)));
    }
    if (state.has_last_known)
        UpsertInput(state.candidate.inputs, BoolInput(keys.last_known_source, ai::AIAccessFlag::KnownState));

    std::sort(state.candidate.inputs.begin(), state.candidate.inputs.end(),
              [](const auto &a, const auto &b) { return a.key < b.key; });
}
} // namespace

PerceptionKnowledgeAdapter::PerceptionKnowledgeAdapter(knowledge::KnowledgeService &knowledge_service,
                                                       PerceptionKnowledgeMapping mapping)
    : knowledge_(knowledge_service), mapping_(mapping)
{
}

foundation::Result<ObservationKnowledgeResult>
PerceptionKnowledgeAdapter::LearnFromObservation(const perception::PerceptionObservation &observation)
{
    if (!observation.perceiver.IsValid() || !observation.sense.IsValid() ||
        observation.confidence == perception::PerceptionConfidence::None)
    {
        return foundation::Result<ObservationKnowledgeResult>::Failure(foundation::Error::Create(
            "gameplay.integration.perception.invalid_observation", "perception observation cannot be learned"));
    }

    ObservationKnowledgeResult result;
    const auto payload = ObservationPayload(observation);

    knowledge::KnowledgeTopic topic;
    topic.id = mapping_.observed_topic;
    topic.primary_subject = observation.perceived_subject;
    topic.tags = observation.observed_tags;
    topic.payload = payload;

    knowledge::LearnKnowledgeRequest learn;
    learn.learner = observation.perceiver;
    learn.type = mapping_.observed_belief;
    learn.topic = std::move(topic);
    learn.source_kind = knowledge::KnowledgeSourceId::FromString("framework.knowledge.direct_perception");
    // PerceptionObservation is already the information boundary. Never consult the stimulus source here.
    learn.source_object = observation.perceived_subject;
    learn.assertion = knowledge::KnowledgeAssertionValue::Affirmed;
    learn.epistemic_state = observation.perceived_subject.IsValid() ? knowledge::KnowledgeEpistemicState::Known
                                                                    : knowledge::KnowledgeEpistemicState::Suspected;
    learn.confidence = Convert(observation.confidence);
    learn.payload = payload;
    learn.context = observation.context;
    auto learned = knowledge_.Learn(std::move(learn));
    if (!learned)
        return foundation::Result<ObservationKnowledgeResult>::Failure(learned.GetError());
    result.knowledge = learned.Value();

    knowledge::CreateMemoryRequest memory;
    memory.owner = observation.perceiver;
    memory.type = mapping_.observation_memory;
    memory.subject = observation.perceived_subject;
    memory.importance = knowledge::MemoryImportance::Normal;
    memory.persistence = knowledge::MemoryPersistencePolicy::Session;
    memory.payload = payload;
    memory.context = observation.context;
    auto created = knowledge_.CreateMemory(std::move(memory));
    if (created)
        result.memory = created.Value();
    else
    {
        result.memory_creation_failed = true;
        result.memory_error = created.GetError();
    }

    return foundation::Result<ObservationKnowledgeResult>::Success(std::move(result));
}

KnowledgeAIAdapter::KnowledgeAIAdapter(const knowledge::KnowledgeService &knowledge_service,
                                       KnowledgeAIInputKeys input_keys)
    : knowledge_(knowledge_service), input_keys_(input_keys)
{
}

ai::AIContextSnapshot KnowledgeAIAdapter::BuildContext(GameplayObjectRef agent,
                                                       const perception::PerceptionService *perception_service,
                                                       GameplayTimePoint now) const
{
    ai::AIContextSnapshot context;
    context.now = now;

    std::map<GameplayObjectRef, CandidateState> candidates;
    const auto last_known_topic = knowledge::KnowledgeTopicId::FromString("framework.knowledge.last_known_position");

    auto records = knowledge_.FindKnowledgeByOwner(agent);
    for (const auto &record : records)
    {
        if (record.confidence == knowledge::KnowledgeConfidence::None ||
            record.assertion == knowledge::KnowledgeAssertionValue::Unknown ||
            record.epistemic_state == knowledge::KnowledgeEpistemicState::Contradicted ||
            record.epistemic_state == knowledge::KnowledgeEpistemicState::Outdated)
            continue;

        auto topic_input = BoolInput(ai::AIInputKeyId::FromType(TypeId{record.topic.id.value.Raw()}),
                                    ai::AIAccessFlag::KnownState);
        context.inputs.push_back(topic_input);

        if (!record.topic.primary_subject.IsValid())
            continue;

        auto &state = candidates[record.topic.primary_subject];
        state.candidate.target = record.topic.primary_subject;
        state.has_knowledge = true;
        state.has_last_known = state.has_last_known || record.topic.id == last_known_topic;
        state.knowledge_confidence = std::max(state.knowledge_confidence, ConfidenceMicro(record.confidence));
        const auto &topic_tags = record.topic.tags.Values();
        state.knowledge_tags.insert(state.knowledge_tags.end(), topic_tags.begin(), topic_tags.end());
        UpsertInput(state.candidate.inputs, std::move(topic_input));
    }

    if (perception_service)
    {
        const auto observations = perception_service->FindActiveObservations(agent, now);
        for (const auto &observation : observations)
        {
            if (!observation.perceived_subject.IsValid())
                continue;

            auto &state = candidates[observation.perceived_subject];
            state.candidate.target = observation.perceived_subject;
            state.has_current_perception = true;
            UpsertInput(state.candidate.inputs,
                        FixedInput(input_keys_.perception_confidence, ai::AIAccessFlag::PerceivedState,
                                   ConfidenceMicro(observation.confidence)));
            UpsertInput(state.candidate.inputs,
                        FixedInput(input_keys_.identity_confidence, ai::AIAccessFlag::PerceivedState,
                                   std::clamp<ai::Fixed>(observation.identity_confidence_micro, 0, kAIUnit)));
            UpsertInput(state.candidate.inputs,
                        FixedInput(input_keys_.position_uncertainty, ai::AIAccessFlag::PerceivedState,
                                   std::max<ai::Fixed>(0, observation.position_uncertainty_mm)));
            UpsertInput(state.candidate.inputs,
                        TypeInput(input_keys_.perception_sense, ai::AIAccessFlag::PerceivedState,
                                  observation.sense.value));
            UpsertInput(state.candidate.inputs,
                        PayloadInput(input_keys_.perception_semantics, ai::AIAccessFlag::PerceivedState,
                                     ObservationPayload(observation)));
        }
    }

    for (auto &[target, state] : candidates)
    {
        (void)target;
        FinalizeCandidate(state, input_keys_);
        context.targets.push_back(std::move(state.candidate));
    }

    std::sort(context.inputs.begin(), context.inputs.end(), [](const auto &a, const auto &b) { return a.key < b.key; });
    context.inputs.erase(std::unique(context.inputs.begin(), context.inputs.end(),
                                     [](const auto &a, const auto &b) { return a.key == b.key; }),
                         context.inputs.end());
    std::sort(context.targets.begin(), context.targets.end(),
              [](const auto &a, const auto &b) { return a.target < b.target; });
    return context;
}

AIIntentExecutionRecorder::AIIntentExecutionRecorder(std::size_t retention_capacity) noexcept
    : retention_capacity_(std::max<std::size_t>(1, retention_capacity))
{
}

void AIIntentExecutionRecorder::RecordAccepted(const ai::AIIntent &intent)
{
    auto existing = std::find_if(accepted_.begin(), accepted_.end(),
                                 [&](const ai::AIIntent &record) { return record.id == intent.id; });
    if (existing != accepted_.end())
        *existing = intent;
    else
        accepted_.push_back(intent);
    std::sort(accepted_.begin(), accepted_.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    if (accepted_.size() > retention_capacity_)
        accepted_.erase(accepted_.begin(), accepted_.begin() + (accepted_.size() - retention_capacity_));
}

foundation::Result<void> AIIntentExecutionRecorder::Accept(const ai::AIIntent &intent, ai::AIService &service)
{
    auto accepted = service.MarkIntentAccepted(intent.id, intent.context);
    if (!accepted)
        return accepted;
    RecordAccepted(intent);
    return foundation::Result<void>::Success();
}

foundation::Result<void> AIIntentExecutionRecorder::Succeed(ai::AIIntentId intent, ai::AIService &service)
{
    auto running = service.MarkIntentRunning(intent);
    if (!running)
        return running;
    return service.MarkIntentSucceeded(intent);
}
} // namespace epidemic::gameplay::integration
