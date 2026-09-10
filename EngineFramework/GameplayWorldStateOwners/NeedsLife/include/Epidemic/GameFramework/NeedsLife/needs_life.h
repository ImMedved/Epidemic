#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::needs_life
{
struct NeedTypeId
{
    TypeId value{};
    static constexpr NeedTypeId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const NeedTypeId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NeedTypeId &) const noexcept = default;
};
struct NeedProfileId
{
    GameplayObjectId value{};
    static constexpr NeedProfileId FromString(std::string_view s) noexcept { return {GameplayObjectId::FromString(s)}; }
    static constexpr NeedProfileId FromRaw(std::uint64_t h, std::uint64_t l) noexcept { return {GameplayObjectId::FromRaw(h, l)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const NeedProfileId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NeedProfileId &) const noexcept = default;
};
struct LifeRoutineId
{
    GameplayObjectId value{};
    static constexpr LifeRoutineId FromString(std::string_view s) noexcept { return {GameplayObjectId::FromString(s)}; }
    static constexpr LifeRoutineId FromRaw(std::uint64_t h, std::uint64_t l) noexcept { return {GameplayObjectId::FromRaw(h, l)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const LifeRoutineId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const LifeRoutineId &) const noexcept = default;
};
struct LifePressureId
{
    GameplayObjectId value{};
    static constexpr LifePressureId FromString(std::string_view s) noexcept { return {GameplayObjectId::FromString(s)}; }
    static constexpr LifePressureId FromRaw(std::uint64_t h, std::uint64_t l) noexcept { return {GameplayObjectId::FromRaw(h, l)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const LifePressureId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const LifePressureId &) const noexcept = default;
};
struct NeedDecayRuleId
{
    TypeId value{};
    static constexpr NeedDecayRuleId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const NeedDecayRuleId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NeedDecayRuleId &) const noexcept = default;
};
struct LifeSimulationProfileId
{
    TypeId value{};
    static constexpr LifeSimulationProfileId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const LifeSimulationProfileId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const LifeSimulationProfileId &) const noexcept = default;
};
struct IdHash
{
    template <class T> [[nodiscard]] std::size_t operator()(const T &id) const noexcept
    {
        return std::hash<decltype(id.value)>{}(id.value);
    }
};
struct NeedStateKey
{
    GameplayObjectRef subject{};
    NeedTypeId need{};
    [[nodiscard]] constexpr bool operator==(const NeedStateKey &) const noexcept = default;
};
struct NeedStateKeyHash
{
    [[nodiscard]] std::size_t operator()(const NeedStateKey &key) const noexcept
    {
        auto a = std::hash<GameplayObjectRef>{}(key.subject);
        auto b = std::hash<TypeId>{}(key.need.value);
        return a ^ (b + 0x9E3779B97F4A7C15ull + (a << 6u) + (a >> 2u));
    }
};

enum class NeedThreshold { Satisfied, Low, Medium, High, Critical };
enum class NeedMaterializationPolicy { AbstractCapable, MaterializedOnly, DisabledWhenAbstract };
enum class LifePressureState { Active, Resolved, Expired };
enum class NeedsLifeChangeKind
{
    NeedProfileCreated,
    NeedProfileChanged,
    NeedChanged,
    NeedThresholdCrossed,
    NeedSatisfied,
    NeedPressureAdded,
    LifePressureCreated,
    LifePressureChanged,
    LifePressureResolved,
    LifePressureExpired,
    RoutineChanged,
    LifeSimulationStepCompleted
};

struct NeedDecayRule
{
    NeedDecayRuleId id{};
    // Multiplies NeedDefinition::decay_per_tick. 1'000'000 means unchanged.
    std::int64_t rate_multiplier_micro = 1'000'000;
    Revision revision{};
};
struct LifeSimulationProfile
{
    LifeSimulationProfileId id{};
    std::int64_t abstract_rate_multiplier_micro = 1'000'000;
    std::int64_t materialized_rate_multiplier_micro = 1'000'000;
    Revision revision{};
};
struct NeedDefinition
{
    NeedTypeId id{};
    GameplayTagSet tags{};
    std::int64_t default_value = 1'000'000;
    std::int64_t min_value = 0;
    std::int64_t max_value = 1'000'000;
    std::int64_t decay_per_tick = 0;
    NeedDecayRuleId decay_rule{};
    // Thresholds are normalized positions in [0, 1'000'000] over [min_value,max_value].
    std::int64_t satisfied_threshold_micro = 750'000;
    std::int64_t low_threshold_micro = 500'000;
    std::int64_t medium_threshold_micro = 250'000;
    std::int64_t high_threshold_micro = 1;
    std::vector<std::byte> payload;
    Revision revision{};
};
struct NeedProfile
{
    NeedProfileId id{};
    GameplayObjectRef subject{};
    std::vector<NeedTypeId> active_needs;
    LifeSimulationProfileId simulation_profile{};
    NeedMaterializationPolicy materialization_policy = NeedMaterializationPolicy::AbstractCapable;
    GameplayTimePoint last_simulated_at{};
    Revision revision{};
};
struct NeedState
{
    GameplayObjectRef subject{};
    NeedTypeId type{};
    std::int64_t value = 0;
    NeedThreshold threshold = NeedThreshold::Satisfied;
    GameplayTimePoint last_updated_at{};
    Revision revision{};
};
struct LifePressure
{
    LifePressureId id{};
    GameplayObjectRef subject{};
    TypeId type{};
    TypeId source{};
    std::int64_t urgency = 0;
    LifePressureState state = LifePressureState::Active;
    std::optional<GameplayTimePoint> expires_at;
    std::vector<std::byte> payload;
    Revision revision{};
};
struct RoutineEntry
{
    GameplayTimePoint start{};
    GameplayDuration duration{};
    TypeId routine_type{};
    GameplayObjectRef target_area{};
    GameplayObjectRef target_object{};
    std::vector<std::byte> payload;
};
struct LifeRoutine
{
    LifeRoutineId id{};
    GameplayObjectRef subject{};
    GameplayTagSet tags{};
    std::vector<RoutineEntry> entries;
    Revision revision{};
};
struct SatisfyNeedRequest
{
    GameplayObjectRef subject{};
    NeedTypeId need{};
    std::int64_t amount = 0;
    TypeId source{};
    GameplayContext context{};
};
struct AddNeedPressureRequest
{
    GameplayObjectRef subject{};
    NeedTypeId need{};
    std::int64_t amount = 0;
    TypeId source{};
    GameplayContext context{};
};
struct LifeSimulationRequest
{
    GameplayObjectRef subject{};
    GameplayTimePoint from{};
    GameplayTimePoint to{};
    GameplayContext context{};
    bool materialized = false;
};
struct LifeRoutineOccurrence
{
    LifeRoutineId routine{};
    std::uint32_t entry_index = 0;
    GameplayTimePoint start{};
    RoutineEntry entry{};
    [[nodiscard]] constexpr bool operator==(const LifeRoutineOccurrence &other) const noexcept
    {
        return routine == other.routine && entry_index == other.entry_index && start == other.start;
    }
};
struct NeedsLifeChange
{
    std::uint64_t sequence = 0;
    NeedsLifeChangeKind kind = NeedsLifeChangeKind::NeedChanged;
    GameplayObjectRef subject{};
    NeedTypeId need{};
    LifePressureId pressure{};
    NeedThreshold threshold = NeedThreshold::Satisfied;
    GameplayContext context{};
    Revision revision{};
};
struct NeedsLifeChangeBatch
{
    std::vector<NeedsLifeChange> changes;
    std::uint64_t oldest_available_sequence = 0;
    std::uint64_t latest_sequence = 0;
    bool snapshot_required = false;

    ChangeCursor oldest_available_cursor{};
    ChangeCursor latest_cursor{};
};
struct NeedsLifeSnapshot
{
    std::vector<NeedDecayRule> decay_rules;
    std::vector<LifeSimulationProfile> simulation_profiles;
    std::vector<NeedDefinition> definitions;
    std::vector<NeedProfile> profiles;
    std::vector<NeedState> states;
    std::vector<LifePressure> pressures;
    std::vector<LifeRoutine> routines;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot profile_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot pressure_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot routine_ids{};
    Revision revision{};

    std::uint64_t change_epoch = 1;
};
struct NeedsLifeDiagnostics
{
    std::uint64_t definitions = 0, profiles = 0, states = 0, critical_needs = 0, life_pressures = 0, routines = 0,
                  threshold_events = 0, abstract_simulation_steps = 0, materialized_simulation_steps = 0,
                  expired_pressures = 0;
};

class NeedsLifeService
{
  public:
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept
    {
        return GameplayDomainId::FromString("framework.needs_life");
    }
    [[nodiscard]] foundation::Result<void> RegisterDecayRule(NeedDecayRule rule);
    [[nodiscard]] foundation::Result<void> RegisterSimulationProfile(LifeSimulationProfile profile);
    [[nodiscard]] foundation::Result<void> RegisterNeedDefinition(NeedDefinition definition);
    [[nodiscard]] foundation::Result<void> FreezeDefinitions();
    [[nodiscard]] bool DefinitionsFrozen() const noexcept { return definitions_frozen_; }

    // Exactly one active profile is permitted per semantic subject.
    [[nodiscard]] foundation::Result<NeedProfileId> CreateNeedProfile(NeedProfile profile, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RemoveNeedProfile(GameplayObjectRef subject, GameplayContext context = {});
    [[nodiscard]] NeedState EvaluateNeed(GameplayObjectRef subject, NeedTypeId need, GameplayTimePoint now) const;
    [[nodiscard]] foundation::Result<void> CommitNeedEvaluation(GameplayObjectRef subject, NeedTypeId need,
                                                                GameplayTimePoint now, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> SatisfyNeed(SatisfyNeedRequest request);
    [[nodiscard]] foundation::Result<void> AddNeedPressure(AddNeedPressureRequest request);

    [[nodiscard]] foundation::Result<LifePressureId> CreateLifePressure(LifePressure pressure,
                                                                        GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> ResolveLifePressure(LifePressureId id, GameplayContext context = {});
    [[nodiscard]] foundation::Result<std::size_t> SweepExpiredPressures(GameplayTimePoint now,
                                                                        GameplayContext context = {});

    // SetRoutine is an upsert by subject. A subject owns at most one active routine record.
    [[nodiscard]] foundation::Result<LifeRoutineId> SetRoutine(LifeRoutine routine, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RemoveRoutine(GameplayObjectRef subject, GameplayContext context = {});
    [[nodiscard]] std::vector<LifeRoutineOccurrence> FindRoutineEntriesInInterval(GameplayObjectRef subject,
                                                                                  GameplayTimePoint from,
                                                                                  GameplayTimePoint to) const;
    [[nodiscard]] std::size_t PruneTerminalPressures(GameplayObjectRef subject = {}) noexcept;

    // Mutating simulation is strict over (from,to]. from must equal profile.last_simulated_at.
    [[nodiscard]] foundation::Result<void> SimulateLifeInterval(LifeSimulationRequest request);

    [[nodiscard]] const NeedProfile *GetNeedProfile(GameplayObjectRef subject) const noexcept;
    [[nodiscard]] const NeedState *GetCommittedNeedState(GameplayObjectRef subject, NeedTypeId need) const noexcept;
    [[nodiscard]] std::int64_t GetNeedUrgency(GameplayObjectRef subject, NeedTypeId need, GameplayTimePoint now) const;
    [[nodiscard]] std::vector<NeedState> FindCriticalNeeds(GameplayTimePoint now) const;
    [[nodiscard]] std::vector<NeedState> FindCriticalNeeds(GameplayObjectRef subject, GameplayTimePoint now) const;
    [[nodiscard]] std::vector<LifePressure> FindLifePressures(GameplayObjectRef subject) const;
    [[nodiscard]] const LifeRoutine *GetRoutine(GameplayObjectRef subject) const noexcept;

    private:

        [[nodiscard]] NeedsLifeChangeBatch ReadChangesSinceSequence(std::uint64_t sequence) const;

    public:
    [[nodiscard]] NeedsLifeChangeBatch ReadChangesSince(ChangeCursor cursor) const
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
    private:
        [[nodiscard]] std::vector<NeedsLifeChange> ChangesSinceSequence(std::uint64_t sequence) const;
    public:
    void PruneChangesThrough(std::uint64_t sequence) noexcept;
    void SetChangeJournalCapacity(std::size_t capacity) noexcept;

    [[nodiscard]] NeedsLifeSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(NeedsLifeSnapshot snapshot);
    [[nodiscard]] NeedsLifeDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept { return revision_; }

  private:
    [[nodiscard]] foundation::Result<void> ValidateDefinition(const NeedDefinition &definition) const;
    [[nodiscard]] foundation::Result<void> ValidateProfile(const NeedProfile &profile) const;
    [[nodiscard]] NeedState EvaluateNeedWithMultiplier(GameplayObjectRef subject, NeedTypeId need, GameplayTimePoint now,
                                                       std::int64_t simulation_multiplier_micro) const;
    [[nodiscard]] std::int64_t EffectiveDecayRate(const NeedDefinition &definition,
                                                  std::int64_t simulation_multiplier_micro) const noexcept;
    [[nodiscard]] static std::int64_t SaturatingAdd64(std::int64_t a, std::int64_t b) noexcept;
    [[nodiscard]] static std::int64_t SaturatingMultiply64(std::int64_t a, std::int64_t b) noexcept;
    [[nodiscard]] static std::int64_t ScaleMicro(std::int64_t value, std::int64_t multiplier_micro) noexcept;
    [[nodiscard]] bool AdvanceRevision() noexcept;
    void Record(NeedsLifeChange change) noexcept;
    void TrimJournal() noexcept;
    void RebuildIndexes();
    [[nodiscard]] NeedThreshold ThresholdFor(const NeedDefinition &def, std::int64_t value) const noexcept;
    [[nodiscard]] std::int64_t Clamp(const NeedDefinition &def, std::int64_t value) const noexcept
    {
        return std::max(def.min_value, std::min(def.max_value, value));
    }
    [[nodiscard]] static NeedStateKey StateKey(GameplayObjectRef subject, NeedTypeId need) noexcept;

    Revision revision_{};
    bool definitions_frozen_ = false;
    MonotonicIdGenerator<GameplayObjectId> profile_ids_{0x2900};
    MonotonicIdGenerator<GameplayObjectId> pressure_ids_{0x2901};
    MonotonicIdGenerator<GameplayObjectId> routine_ids_{0x2902};
    std::unordered_map<NeedDecayRuleId, NeedDecayRule, IdHash> decay_rules_;
    std::unordered_map<LifeSimulationProfileId, LifeSimulationProfile, IdHash> simulation_profiles_;
    std::unordered_map<NeedTypeId, NeedDefinition, IdHash> definitions_;
    std::unordered_map<NeedProfileId, NeedProfile, IdHash> profiles_;
    std::unordered_map<GameplayObjectRef, NeedProfileId> profile_by_subject_;
    std::unordered_map<NeedStateKey, NeedState, NeedStateKeyHash> states_;
    std::unordered_map<LifePressureId, LifePressure, IdHash> pressures_;
    std::unordered_multimap<GameplayObjectRef, LifePressureId> pressures_by_subject_;
    std::unordered_map<LifeRoutineId, LifeRoutine, IdHash> routines_;
    std::unordered_map<GameplayObjectRef, LifeRoutineId> routine_by_subject_;
    std::deque<NeedsLifeChange> changes_;
    std::size_t change_journal_capacity_ = 4096;
    std::uint64_t next_change_sequence_ = 1;
    std::uint64_t journal_epoch_ = 1;
    mutable NeedsLifeDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::needs_life
