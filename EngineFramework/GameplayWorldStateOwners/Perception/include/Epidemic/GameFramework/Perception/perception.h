#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::perception
{
struct SenseTypeId
{
    TypeId value{};
    static constexpr SenseTypeId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const SenseTypeId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const SenseTypeId &) const noexcept = default;
};
struct PerceiverProfileId
{
    TypeId value{};
    static constexpr PerceiverProfileId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const PerceiverProfileId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PerceiverProfileId &) const noexcept = default;
};
struct SenseEvaluatorId
{
    TypeId value{};
    static constexpr SenseEvaluatorId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const SenseEvaluatorId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const SenseEvaluatorId &) const noexcept = default;
};
struct PerceptionStimulusId
{
    GameplayObjectId value{};
    static constexpr PerceptionStimulusId FromString(std::string_view s) noexcept { return {GameplayObjectId::FromString(s)}; }
    static constexpr PerceptionStimulusId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const PerceptionStimulusId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PerceptionStimulusId &) const noexcept = default;
};
struct PerceptionObservationId
{
    GameplayObjectId value{};
    static constexpr PerceptionObservationId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr PerceptionObservationId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const PerceptionObservationId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PerceptionObservationId &) const noexcept = default;
};
struct AwarenessStateId
{
    TypeId value{};
    static constexpr AwarenessStateId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const AwarenessStateId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const AwarenessStateId &) const noexcept = default;
};

struct IdHash
{
    template <class T> [[nodiscard]] std::size_t operator()(const T &id) const noexcept
    {
        return std::hash<decltype(id.value)>{}(id.value);
    }
};
struct RefHash
{
    [[nodiscard]] std::size_t operator()(GameplayObjectRef r) const noexcept { return std::hash<GameplayObjectRef>{}(r); }
};

using Fixed = std::int64_t;
struct WorldPosition
{
    Fixed x_mm = 0;
    Fixed y_mm = 0;
    Fixed z_mm = 0;
    [[nodiscard]] constexpr bool operator==(const WorldPosition &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const WorldPosition &) const noexcept = default;
};
struct WorldDirection
{
    Fixed x_micro = 1'000'000;
    Fixed y_micro = 0;
    Fixed z_micro = 0;
    [[nodiscard]] constexpr bool operator==(const WorldDirection &) const noexcept = default;
};

enum class PerceptionMaterializationPolicy
{
    AbstractCapable,
    RequiresMaterialized,
    RequiresRuntimeProjection
};
enum class SenseEvaluationModel
{
    Vision,
    Hearing,
    Radial,
    Custom
};
enum class PerceptionVisibilityState
{
    Visible,
    PartiallyVisible,
    Hidden,
    Occluded,
    Unknown,
    Unavailable
};
enum class PerceptionAudibilityState
{
    HeardExactly,
    HeardDirectionOnly,
    HeardVagueNoise,
    NotHeard,
    Unavailable
};
enum class PerceptionConfidence
{
    None,
    Low,
    Medium,
    High,
    Certain
};
enum class AwarenessLevel
{
    Unaware,
    Suspicious,
    Aware,
    Lost,
    Confirmed
};
enum class PerceptionChangeKind
{
    StimulusCreated,
    StimulusExpired,
    Observed,
    Lost,
    AwarenessChanged,
    PerceiverRegistered,
    PerceiverUnregistered,
    BudgetExceeded
};

struct SenseDefinition
{
    SenseTypeId id{};
    std::string canonical_name;
    GameplayTagSet tags;
    SenseEvaluationModel evaluation_model = SenseEvaluationModel::Radial;
    SenseEvaluatorId evaluator{};
    Fixed base_range_mm = 0;
    // Cosine of the half-FOV angle in micro units. -1'000'000 means full sphere, +1'000'000 means exact forward.
    Fixed field_of_view_cosine_micro = -1'000'000;
    Fixed acuity_micro = 1'000'000;
    Fixed identify_threshold_micro = 800'000;
    Fixed direction_threshold_micro = 350'000;
    GameplayDuration reaction_delay{};
    bool requires_spatial_sample = true;
    std::vector<std::byte> payload;
};
struct PerceiverProfileDefinition
{
    PerceiverProfileId id{};
    std::string canonical_name;
    GameplayTagSet tags;
    std::vector<SenseTypeId> senses;
    PerceptionMaterializationPolicy materialization_policy = PerceptionMaterializationPolicy::AbstractCapable;
};
struct PerceiverRecord
{
    GameplayObjectRef subject{};
    PerceiverProfileId profile{};
    Revision revision{};
};
struct PerceptionStimulus
{
    PerceptionStimulusId id{};
    SenseTypeId sense{};
    GameplayObjectRef source{};
    WorldPosition position{};
    GameplayObjectRef area{};
    Fixed strength_micro = 1'000'000;
    GameplayTagSet tags;
    GameplayTimePoint created_at{};
    GameplayDuration lifetime{};
    GameplayContext context{};
    Revision revision{};
};
struct PerceptionObservation
{
    PerceptionObservationId id{};
    GameplayObjectRef perceiver{};
    GameplayObjectRef perceived_subject{};
    PerceptionStimulusId stimulus{};
    SenseTypeId sense{};
    PerceptionConfidence confidence = PerceptionConfidence::None;
    Fixed identity_confidence_micro = 0;
    WorldPosition perceived_position{};
    Fixed position_uncertainty_mm = 0;
    GameplayTimePoint observed_at{};
    GameplayTagSet observed_tags;
    GameplayContext context{};
    Revision revision{};
};
struct PendingPerceptionObservation
{
    PerceptionObservation observation{};
    GameplayTimePoint ready_at{};
    Fixed detection_score_micro = 0;
};
struct AwarenessRecord
{
    GameplayObjectRef perceiver{};
    GameplayObjectRef target{};
    AwarenessLevel level = AwarenessLevel::Unaware;
    Fixed suspicion_micro = 0;
    GameplayTimePoint last_observed_at{};
    GameplayTimePoint last_decay_at{};
    WorldPosition last_known_position{};
    Revision revision{};
};
struct VisibilityResult
{
    PerceptionVisibilityState state = PerceptionVisibilityState::Unknown;
    Fixed score_micro = 0;
    std::vector<TypeId> reasons;
    Revision revision{};
};
struct AudibilityResult
{
    PerceptionAudibilityState state = PerceptionAudibilityState::NotHeard;
    Fixed score_micro = 0;
    std::vector<TypeId> reasons;
    Revision revision{};
};
struct SenseEvaluationResult
{
    Fixed score_micro = 0;
    PerceptionVisibilityState visibility = PerceptionVisibilityState::Unknown;
    PerceptionAudibilityState audibility = PerceptionAudibilityState::Unavailable;
    bool identified_subject = false;
    Fixed identity_confidence_micro = 0;
    WorldPosition perceived_position{};
    Fixed position_uncertainty_mm = 0;
};
struct PerceiverEvaluationSample
{
    GameplayObjectRef subject{};
    WorldPosition position{};
    WorldDirection forward{};
    bool materialized = false;
    bool runtime_projection_available = false;
    Fixed sense_multiplier_micro = 1'000'000;
    // Current stimulus-specific attenuation. 0 means fully occluded/blocked, 1'000'000 means unattenuated.
    Fixed attenuation_micro = 1'000'000;
};
struct PerceptionProcessingContext
{
    GameplayTickId tick{};
    GameplayTimePoint now{};
    GameplayContext gameplay{};
    std::vector<PerceiverEvaluationSample> perceivers;
    PerceptionProcessingContext() = default;
    explicit PerceptionProcessingContext(GameplayTickId t, GameplayTimePoint n, GameplayContext g = {},
                                         std::vector<PerceiverEvaluationSample> samples = {})
        : tick(t), now(n), gameplay(g), perceivers(std::move(samples))
    {
    }
    [[nodiscard]] const PerceiverEvaluationSample *FindPerceiver(GameplayObjectRef subject) const noexcept
    {
        for (const auto &sample : perceivers)
        {
            if (sample.subject == subject)
                return &sample;
        }
        return nullptr;
    }
};
struct SenseEvaluationInput
{
    const SenseDefinition &definition;
    const PerceiverProfileDefinition &profile;
    const PerceiverRecord &perceiver;
    const PerceptionStimulus &stimulus;
    const PerceiverEvaluationSample *sample = nullptr;
    const PerceptionProcessingContext &context;
};
class ISenseEvaluator
{
  public:
    virtual ~ISenseEvaluator() = default;
    [[nodiscard]] virtual foundation::Result<SenseEvaluationResult> Evaluate(const SenseEvaluationInput &input) const = 0;
};

struct PerceptionBudget
{
    std::uint32_t max_stimuli_per_tick = 1024;
    std::uint32_t max_perceivers_per_stimulus = 256;
    std::uint32_t max_detection_tests_per_tick = 4096;
};
struct PerceptionTemporalPolicy
{
    GameplayDuration observation_retention{1};
    GameplayDuration awareness_decay_interval{1};
    Fixed awareness_decay_micro_per_interval = 100'000;
};
struct PerceptionTickBudgetState
{
    GameplayTickId tick{};
    std::uint32_t processed_stimuli = 0;
    std::uint32_t detection_tests = 0;
};
struct PerceptionChange
{
    std::uint64_t sequence = 0;
    PerceptionChangeKind kind = PerceptionChangeKind::StimulusCreated;
    GameplayObjectRef subject{};
    GameplayObjectRef target{};
    PerceptionStimulusId stimulus{};
    PerceptionObservationId observation{};
    AwarenessLevel awareness = AwarenessLevel::Unaware;
    GameplayContext context{};
    Revision revision{};
};
struct PerceptionChangeBatch
{
    std::vector<PerceptionChange> changes;
    std::uint64_t oldest_available_sequence = 0;
    std::uint64_t latest_sequence = 0;
    bool snapshot_required = false;

    ChangeCursor oldest_available_cursor{};
    ChangeCursor latest_cursor{};
};
struct PerceptionSnapshot
{
    std::vector<PerceiverRecord> perceivers;
    std::vector<PerceptionStimulus> stimuli;
    std::vector<PendingPerceptionObservation> pending_observations;
    std::vector<PerceptionObservation> observations;
    std::vector<AwarenessRecord> awareness;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot stimulus_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot observation_ids{};
    std::uint64_t next_change_sequence = 1;
    Revision revision{};

    std::uint64_t change_epoch = 1;
};
struct PerceptionDiagnostics
{
    std::uint64_t perceivers = 0;
    std::uint64_t stimuli = 0;
    std::uint64_t pending_observations = 0;
    std::uint64_t observations = 0;
    std::uint64_t awareness_records = 0;
    std::uint64_t processed_stimuli = 0;
    std::uint64_t dropped_stimuli = 0;
    std::uint64_t detection_tests = 0;
    std::uint64_t visibility_tests = 0;
    std::uint64_t audibility_tests = 0;
    std::uint64_t custom_tests = 0;
    std::uint64_t budget_exhaustions = 0;
    std::uint64_t evaluator_failures = 0;
};

struct AwarenessKey
{
    GameplayObjectRef perceiver{};
    GameplayObjectRef target{};
    [[nodiscard]] constexpr bool operator==(const AwarenessKey &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const AwarenessKey &) const noexcept = default;
};
struct AwarenessKeyHash
{
    [[nodiscard]] std::size_t operator()(const AwarenessKey &k) const noexcept
    {
        const auto a = std::hash<GameplayObjectRef>{}(k.perceiver);
        const auto b = std::hash<GameplayObjectRef>{}(k.target);
        return a ^ (b + 0x9E3779B97F4A7C15ull + (a << 6u) + (a >> 2u));
    }
};
struct PendingKey
{
    GameplayTimePoint ready_at{};
    PerceptionObservationId observation{};
    [[nodiscard]] constexpr bool operator==(const PendingKey &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PendingKey &) const noexcept = default;
};

class PerceptionService
{
  public:
    PerceptionService();
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept
    {
        return GameplayDomainId::FromString("framework.perception");
    }

    [[nodiscard]] foundation::Result<void> RegisterSense(SenseDefinition definition);
    [[nodiscard]] foundation::Result<void> RegisterProfileDefinition(PerceiverProfileDefinition definition);
    [[nodiscard]] foundation::Result<void> RegisterEvaluator(SenseEvaluatorId id,
                                                             std::shared_ptr<const ISenseEvaluator> evaluator);
    void Freeze() noexcept { frozen_ = true; }
    [[nodiscard]] bool IsFrozen() const noexcept { return frozen_; }

    [[nodiscard]] const SenseDefinition *FindSense(SenseTypeId id) const noexcept;
    [[nodiscard]] const PerceiverProfileDefinition *FindProfileDefinition(PerceiverProfileId id) const noexcept;
    [[nodiscard]] const PerceiverRecord *FindPerceiver(GameplayObjectRef subject) const noexcept;
    [[nodiscard]] foundation::Result<void> RegisterPerceiver(GameplayObjectRef subject, PerceiverProfileId profile,
                                                             GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> UnregisterPerceiver(GameplayObjectRef subject, GameplayContext context = {});

    [[nodiscard]] foundation::Result<PerceptionStimulusId> CreateStimulus(PerceptionStimulus stimulus);
    [[nodiscard]] foundation::Result<void> ExpireStimuli(GameplayTimePoint now, GameplayContext context = {});
    [[nodiscard]] foundation::Result<std::vector<PerceptionObservation>> ProcessStimulus(
        PerceptionStimulusId stimulus, const PerceptionProcessingContext &context);
    [[nodiscard]] foundation::Result<std::vector<PerceptionObservation>> AdvanceTime(GameplayTimePoint now,
                                                                                     GameplayContext context = {});

    [[nodiscard]] VisibilityResult EvaluateVisibility(GameplayObjectRef perceiver, GameplayObjectRef target,
                                                      const PerceiverEvaluationSample &sample,
                                                      WorldPosition target_position,
                                                      Fixed stimulus_strength_micro = 1'000'000) const;
    [[nodiscard]] AudibilityResult EvaluateAudibility(GameplayObjectRef perceiver, PerceptionStimulusId stimulus,
                                                       const PerceiverEvaluationSample &sample) const;

    void SetBudget(PerceptionBudget budget) noexcept { budget_ = budget; }
    void SetTemporalPolicy(PerceptionTemporalPolicy policy) noexcept { temporal_policy_ = policy; }
    void SetChangeJournalCapacity(std::size_t capacity) noexcept { change_capacity_ = std::max<std::size_t>(1, capacity); }

    [[nodiscard]] const PerceptionStimulus *FindStimulus(PerceptionStimulusId id) const noexcept;
    [[nodiscard]] const AwarenessRecord *GetAwareness(GameplayObjectRef perceiver,
                                                      GameplayObjectRef target) const noexcept;
    [[nodiscard]] std::vector<PerceptionStimulus> FindStimuliInArea(GameplayObjectRef area) const;
    [[nodiscard]] std::vector<PerceptionObservation> FindObservationsByPerceiver(GameplayObjectRef perceiver) const;
    [[nodiscard]] std::vector<PerceptionObservation> FindActiveObservations(GameplayObjectRef perceiver, GameplayTimePoint now) const;
    private:
        [[nodiscard]] PerceptionChangeBatch ReadChangesSinceSequence(std::uint64_t sequence) const;
    public:
    [[nodiscard]] PerceptionChangeBatch ReadChangesSince(ChangeCursor cursor) const
    {
        auto batch = ReadChangesSinceSequence(cursor.sequence);
        batch.oldest_available_cursor = {journal_epoch_, batch.oldest_available_sequence};
        batch.latest_cursor = {journal_epoch_, next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max()
                                                                    : next_change_sequence_ - 1};
        if ((!cursor.IsValid() && cursor.sequence != 0) || (cursor.IsValid() && cursor.epoch != journal_epoch_))
        {
            batch.changes.clear();
            batch.snapshot_required = true;
        }
        return batch;
    }
    [[nodiscard]] ChangeCursor LatestChangeCursor() const noexcept
    {
        return {journal_epoch_, next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max()
                                                          : next_change_sequence_ - 1};
    }
    [[nodiscard]] PerceptionSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(PerceptionSnapshot snapshot);
    [[nodiscard]] PerceptionDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept { return revision_; }

  private:
    void Bump() noexcept { ++revision_.value; }
    void Record(PerceptionChange change);
    void EnsureBudgetEpoch(GameplayTickId tick) noexcept;
    [[nodiscard]] foundation::Result<SenseEvaluationResult> EvaluateSense(const SenseDefinition &definition,
                                                                          const PerceiverProfileDefinition &profile,
                                                                          const PerceiverRecord &perceiver,
                                                                          const PerceptionStimulus &stimulus,
                                                                          const PerceiverEvaluationSample *sample,
                                                                          const PerceptionProcessingContext &context) const;
    [[nodiscard]] SenseEvaluationResult EvaluateBuiltIn(const SenseDefinition &definition,
                                                        const PerceiverEvaluationSample &sample,
                                                        const PerceptionStimulus &stimulus) const noexcept;
    [[nodiscard]] foundation::Result<void> ActivateObservation(PendingPerceptionObservation pending,
                                                              GameplayContext context,
                                                              std::vector<PerceptionObservation> *activated);
    void ExpireObservations(GameplayTimePoint now, GameplayContext context);
    void DecayAwareness(GameplayTimePoint now, GameplayContext context);
    void CleanupUnaware();
    void IndexStimulus(const PerceptionStimulus &stimulus);
    void UnindexStimulus(const PerceptionStimulus &stimulus);
    void IndexObservation(const PerceptionObservation &observation);
    void UnindexObservation(const PerceptionObservation &observation);
    [[nodiscard]] bool HasObservationFor(GameplayObjectRef perceiver, GameplayObjectRef target) const;
    [[nodiscard]] bool HasPendingFor(GameplayObjectRef perceiver, GameplayObjectRef target) const;
    [[nodiscard]] static Fixed DistanceSquared(WorldPosition a, WorldPosition b) noexcept;
    [[nodiscard]] static Fixed DistanceMm(WorldPosition a, WorldPosition b) noexcept;
    [[nodiscard]] static Fixed DistanceAttenuatedScore(Fixed strength_micro, Fixed range_mm, WorldPosition observer,
                                                       WorldPosition stimulus) noexcept;
    [[nodiscard]] static Fixed MultiplyMicro(Fixed a, Fixed b) noexcept;
    [[nodiscard]] static bool WithinFov(const SenseDefinition &definition, const PerceiverEvaluationSample &sample,
                                        WorldPosition target) noexcept;
    [[nodiscard]] PerceptionConfidence ConfidenceFromScore(Fixed score_micro) const noexcept;
    [[nodiscard]] static bool ValidGeneratorSnapshot(MonotonicIdGenerator<GameplayObjectId>::Snapshot snapshot,
                                                     std::uint64_t expected_scope, std::uint64_t max_low) noexcept;

    bool frozen_ = false;
    Revision revision_{};
    MonotonicIdGenerator<GameplayObjectId> stimulus_ids_{0x2000};
    MonotonicIdGenerator<GameplayObjectId> observation_ids_{0x2001};

    std::unordered_map<SenseTypeId, SenseDefinition, IdHash> senses_;
    std::unordered_map<PerceiverProfileId, PerceiverProfileDefinition, IdHash> profile_definitions_;
    std::unordered_map<SenseEvaluatorId, std::shared_ptr<const ISenseEvaluator>, IdHash> evaluators_;
    std::unordered_map<GameplayObjectRef, PerceiverRecord, RefHash> perceivers_;

    std::unordered_map<PerceptionStimulusId, PerceptionStimulus, IdHash> stimuli_;
    std::unordered_map<GameplayObjectRef, std::vector<PerceptionStimulusId>, RefHash> stimuli_by_area_;

    std::unordered_map<PerceptionObservationId, PendingPerceptionObservation, IdHash> pending_observations_;
    std::map<PendingKey, PerceptionObservationId> pending_by_ready_;
    std::unordered_map<PerceptionObservationId, PerceptionObservation, IdHash> observations_;
    std::unordered_map<GameplayObjectRef, std::vector<PerceptionObservationId>, RefHash> observations_by_perceiver_;
    std::unordered_map<AwarenessKey, AwarenessRecord, AwarenessKeyHash> awareness_;

    std::deque<PerceptionChange> changes_;
    std::size_t change_capacity_ = 4096;
    std::uint64_t next_change_sequence_ = 1;
    std::uint64_t journal_epoch_ = 1;

    PerceptionBudget budget_{};
    PerceptionTemporalPolicy temporal_policy_{};
    PerceptionTickBudgetState tick_budget_{};
    mutable PerceptionDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::perception
