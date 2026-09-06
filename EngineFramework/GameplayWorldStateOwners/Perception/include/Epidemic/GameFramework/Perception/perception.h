#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
    static constexpr SenseTypeId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const SenseTypeId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const SenseTypeId &) const noexcept = default;
};
struct PerceiverProfileId
{
    TypeId value{};
    static constexpr PerceiverProfileId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const PerceiverProfileId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PerceiverProfileId &) const noexcept = default;
};
struct PerceptionStimulusId
{
    GameplayObjectId value{};
    static constexpr PerceptionStimulusId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr PerceptionStimulusId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
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
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const PerceptionObservationId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PerceptionObservationId &) const noexcept = default;
};
struct PerceptionChannelId
{
    TypeId value{};
    static constexpr PerceptionChannelId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const PerceptionChannelId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PerceptionChannelId &) const noexcept = default;
};
struct PerceptionRuleId
{
    TypeId value{};
    static constexpr PerceptionRuleId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const PerceptionRuleId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PerceptionRuleId &) const noexcept = default;
};
struct AwarenessStateId
{
    TypeId value{};
    static constexpr AwarenessStateId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
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
    [[nodiscard]] std::size_t operator()(GameplayObjectRef r) const noexcept
    {
        return std::hash<GameplayObjectRef>{}(r);
    }
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

enum class PerceptionMaterializationPolicy
{
    AbstractCapable,
    RequiresMaterialized,
    RequiresRuntimeProjection
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
    BudgetExceeded
};

struct SenseDefinition
{
    SenseTypeId id{};
    std::string canonical_name;
    GameplayTagSet tags;
    Fixed base_range_mm = 0;
    Fixed field_of_view_micro = 1'000'000;
    Fixed acuity_micro = 1'000'000;
    GameplayDuration reaction_delay{};
    std::vector<std::byte> payload;
};
struct PerceiverProfile
{
    PerceiverProfileId id{};
    std::string canonical_name;
    GameplayObjectRef subject{};
    GameplayTagSet tags;
    std::vector<SenseTypeId> senses;
    PerceptionMaterializationPolicy materialization_policy = PerceptionMaterializationPolicy::AbstractCapable;
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
    WorldPosition perceived_position{};
    GameplayTimePoint observed_at{};
    GameplayTagSet observed_tags;
    GameplayContext context{};
    Revision revision{};
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
struct PerceptionBudget
{
    std::uint32_t max_stimuli_per_tick = 1024;
    std::uint32_t max_perceivers_per_stimulus = 256;
    std::uint32_t max_visibility_tests_per_tick = 4096;
};
struct PerceptionTemporalPolicy
{
    GameplayDuration observation_retention{1};
    GameplayDuration awareness_decay_interval{1};
    Fixed awareness_decay_micro_per_interval = 100'000;
};
struct PerceiverSpatialSample
{
    GameplayObjectRef subject{};
    WorldPosition position{};
};
struct PerceptionProcessingContext
{
    GameplayTickId tick{};
    GameplayTimePoint now{};
    GameplayContext gameplay{};
    std::vector<PerceiverSpatialSample> perceiver_positions;
    PerceptionProcessingContext() = default;
    explicit PerceptionProcessingContext(GameplayTickId t, GameplayTimePoint n, GameplayContext g = {},
                                         std::vector<PerceiverSpatialSample> positions = {})
        : tick(t), now(n), gameplay(g), perceiver_positions(std::move(positions))
    {
    }
    [[nodiscard]] std::optional<WorldPosition> FindPerceiverPosition(GameplayObjectRef subject) const noexcept
    {
        for (const auto &sample : perceiver_positions)
        {
            if (sample.subject == subject)
                return sample.position;
        }
        return std::nullopt;
    }
};
struct PerceptionTickBudgetState
{
    GameplayTickId tick{};
    std::uint32_t processed_stimuli = 0;
    std::uint32_t visibility_tests = 0;
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
struct PerceptionSnapshot
{
    std::vector<PerceiverProfile> profiles;
    std::vector<PerceptionStimulus> stimuli;
    std::vector<PerceptionObservation> observations;
    std::vector<AwarenessRecord> awareness;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot stimulus_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot observation_ids{};
    Revision revision{};
};
struct PerceptionDiagnostics
{
    std::uint64_t profiles = 0, stimuli = 0, observations = 0, awareness_records = 0, processed_stimuli = 0,
                  dropped_stimuli = 0, visibility_tests = 0, audibility_tests = 0, budget_exhaustions = 0;
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
        auto a = std::hash<GameplayObjectRef>{}(k.perceiver);
        auto b = std::hash<GameplayObjectRef>{}(k.target);
        return a ^ (b + 0x9E3779B97F4A7C15ull + (a << 6u) + (a >> 2u));
    }
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
    [[nodiscard]] foundation::Result<void> RegisterProfile(PerceiverProfile profile);
    void Freeze() noexcept
    {
        frozen_ = true;
    }
    [[nodiscard]] bool IsFrozen() const noexcept
    {
        return frozen_;
    }
    [[nodiscard]] const SenseDefinition *FindSense(SenseTypeId id) const noexcept;
    [[nodiscard]] const PerceiverProfile *FindProfile(PerceiverProfileId id) const noexcept;
    [[nodiscard]] const PerceiverProfile *FindProfileBySubject(GameplayObjectRef subject) const noexcept;

    [[nodiscard]] foundation::Result<PerceptionStimulusId> CreateStimulus(PerceptionStimulus stimulus);
    [[nodiscard]] foundation::Result<void> ExpireStimuli(GameplayTimePoint now);
    [[nodiscard]] foundation::Result<std::vector<PerceptionObservation>> ProcessStimulus(
        PerceptionStimulusId stimulus, const PerceptionProcessingContext &context);
    [[nodiscard]] foundation::Result<std::vector<PerceptionObservation>> ProcessStimulus(PerceptionStimulusId stimulus,
                                                                                         GameplayTimePoint now);
    [[nodiscard]] VisibilityResult EvaluateVisibility(GameplayObjectRef perceiver, GameplayObjectRef target,
                                                      WorldPosition observer_position,
                                                      WorldPosition target_position) const;
    [[nodiscard]] AudibilityResult EvaluateAudibility(GameplayObjectRef perceiver, PerceptionStimulusId stimulus) const;
    [[nodiscard]] AudibilityResult EvaluateAudibility(GameplayObjectRef perceiver, PerceptionStimulusId stimulus,
                                                       WorldPosition observer_position) const;
    void SetOccluded(GameplayObjectRef perceiver, GameplayObjectRef target, bool occluded);
    void SetSenseModifier(GameplayObjectRef perceiver, SenseTypeId sense, Fixed multiplier_micro);
    void SetBudget(PerceptionBudget budget) noexcept
    {
        budget_ = budget;
    }
    void SetTemporalPolicy(PerceptionTemporalPolicy policy) noexcept
    {
        temporal_policy_ = policy;
    }

    [[nodiscard]] const PerceptionStimulus *FindStimulus(PerceptionStimulusId id) const noexcept;
    [[nodiscard]] const AwarenessRecord *GetAwareness(GameplayObjectRef perceiver,
                                                      GameplayObjectRef target) const noexcept;
    [[nodiscard]] std::vector<PerceptionStimulus> FindStimuliInArea(GameplayObjectRef area) const;
    [[nodiscard]] std::vector<PerceptionObservation> FindObservationsByPerceiver(GameplayObjectRef perceiver) const;
    [[nodiscard]] std::vector<PerceptionChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] PerceptionSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(PerceptionSnapshot snapshot);
    [[nodiscard]] PerceptionDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept
    {
        return revision_;
    }

  private:
    void Bump() noexcept
    {
        revision_.value++;
    }
    void Record(PerceptionChange change);
    void EnsureBudgetEpoch(GameplayTickId tick) noexcept;
    void MaintainTemporalState(GameplayTimePoint now, GameplayContext context);
    [[nodiscard]] static Fixed DistanceSquared(WorldPosition a, WorldPosition b) noexcept;
    [[nodiscard]] static Fixed DistanceAttenuatedScore(Fixed strength_micro, Fixed range_mm, WorldPosition observer,
                                                       WorldPosition stimulus) noexcept;
    [[nodiscard]] static Fixed ScaledRange(Fixed base_range_mm, Fixed multiplier_micro) noexcept;
    [[nodiscard]] Fixed ModifierFor(GameplayObjectRef perceiver, SenseTypeId sense) const noexcept;
    [[nodiscard]] bool IsOccluded(GameplayObjectRef perceiver, GameplayObjectRef target) const noexcept;
    [[nodiscard]] PerceptionConfidence ConfidenceFromScore(Fixed score_micro) const noexcept;
    [[nodiscard]] AwarenessRecord &TouchAwareness(GameplayObjectRef perceiver, GameplayObjectRef target);

    bool frozen_ = false;
    Revision revision_{};
    MonotonicIdGenerator<GameplayObjectId> stimulus_ids_{0x2000};
    MonotonicIdGenerator<GameplayObjectId> observation_ids_{0x2001};
    std::unordered_map<SenseTypeId, SenseDefinition, IdHash> senses_;
    std::unordered_map<PerceiverProfileId, PerceiverProfile, IdHash> profiles_;
    std::unordered_map<GameplayObjectRef, PerceiverProfileId, RefHash> profile_by_subject_;
    std::unordered_map<PerceptionStimulusId, PerceptionStimulus, IdHash> stimuli_;
    std::unordered_map<PerceptionObservationId, PerceptionObservation, IdHash> observations_;
    std::unordered_map<AwarenessKey, AwarenessRecord, AwarenessKeyHash> awareness_;
    std::unordered_map<AwarenessKey, bool, AwarenessKeyHash> occlusion_;
    std::unordered_map<AwarenessKey, Fixed, AwarenessKeyHash> modifiers_;
    std::vector<PerceptionChange> changes_;
    std::uint64_t next_change_sequence_ = 1;
    PerceptionBudget budget_{};
    PerceptionTemporalPolicy temporal_policy_{};
    PerceptionTickBudgetState tick_budget_{};
    PerceptionDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::perception
