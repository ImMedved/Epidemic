#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::needs_life
{
struct NeedTypeId{TypeId value{}; static constexpr NeedTypeId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const NeedTypeId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const NeedTypeId&) const noexcept=default;};
struct NeedProfileId{GameplayObjectId value{}; static constexpr NeedProfileId FromString(std::string_view s) noexcept{return {GameplayObjectId::FromString(s)};} static constexpr NeedProfileId FromRaw(std::uint64_t h,std::uint64_t l) noexcept{return {GameplayObjectId::FromRaw(h,l)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const NeedProfileId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const NeedProfileId&) const noexcept=default;};
struct LifeRoutineId{GameplayObjectId value{}; static constexpr LifeRoutineId FromString(std::string_view s) noexcept{return {GameplayObjectId::FromString(s)};} static constexpr LifeRoutineId FromRaw(std::uint64_t h,std::uint64_t l) noexcept{return {GameplayObjectId::FromRaw(h,l)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const LifeRoutineId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const LifeRoutineId&) const noexcept=default;};
struct LifePressureId{GameplayObjectId value{}; static constexpr LifePressureId FromString(std::string_view s) noexcept{return {GameplayObjectId::FromString(s)};} static constexpr LifePressureId FromRaw(std::uint64_t h,std::uint64_t l) noexcept{return {GameplayObjectId::FromRaw(h,l)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const LifePressureId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const LifePressureId&) const noexcept=default;};
struct NeedDecayRuleId{TypeId value{}; static constexpr NeedDecayRuleId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const NeedDecayRuleId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const NeedDecayRuleId&) const noexcept=default;};
struct LifeSimulationProfileId{TypeId value{}; static constexpr LifeSimulationProfileId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const LifeSimulationProfileId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const LifeSimulationProfileId&) const noexcept=default;};
struct IdHash{template<class T> [[nodiscard]] std::size_t operator()(const T& id) const noexcept{return std::hash<decltype(id.value)>{}(id.value);}};

enum class NeedThreshold{Satisfied,Low,Medium,High,Critical};
enum class NeedMaterializationPolicy{AbstractCapable,MaterializedOnly,DisabledWhenAbstract};
enum class LifePressureState{Active,Resolved,Expired};
enum class NeedsLifeChangeKind{NeedProfileCreated,NeedChanged,NeedThresholdCrossed,NeedSatisfied,LifePressureCreated,LifePressureChanged,LifePressureResolved,RoutineChanged,LifeSimulationStepCompleted};

struct NeedDefinition{NeedTypeId id{};GameplayTagSet tags{};std::int64_t default_value=1'000'000;std::int64_t min_value=0;std::int64_t max_value=1'000'000;std::int64_t decay_per_tick=0;NeedDecayRuleId decay_rule{};std::vector<std::byte> payload;Revision revision{};};
struct NeedProfile{NeedProfileId id{};GameplayObjectRef subject{};std::vector<NeedTypeId> active_needs;LifeSimulationProfileId simulation_profile{};NeedMaterializationPolicy materialization_policy=NeedMaterializationPolicy::AbstractCapable;Revision revision{};};
struct NeedState{GameplayObjectRef subject{};NeedTypeId type{};std::int64_t value=0;NeedThreshold threshold=NeedThreshold::Satisfied;GameplayTimePoint last_updated_at{};Revision revision{};};
struct LifePressure{LifePressureId id{};GameplayObjectRef subject{};TypeId type{};std::int64_t urgency=0;LifePressureState state=LifePressureState::Active;std::vector<std::byte> payload;Revision revision{};};
struct RoutineEntry{GameplayTimePoint start{};GameplayDuration duration{};TypeId routine_type{};GameplayObjectRef target_area{};GameplayObjectRef target_object{};std::vector<std::byte> payload;};
struct LifeRoutine{LifeRoutineId id{};GameplayObjectRef subject{};GameplayTagSet tags{};std::vector<RoutineEntry> entries;Revision revision{};};
struct SatisfyNeedRequest{GameplayObjectRef subject{};NeedTypeId need{};std::int64_t amount=0;TypeId source{};GameplayContext context{};};
struct LifeSimulationRequest{GameplayObjectRef subject{};GameplayTimePoint from{};GameplayTimePoint to{};TypeId policy{};GameplayContext context{};};
struct NeedsLifeChange{std::uint64_t sequence=0;NeedsLifeChangeKind kind=NeedsLifeChangeKind::NeedChanged;GameplayObjectRef subject{};NeedTypeId need{};LifePressureId pressure{};NeedThreshold threshold=NeedThreshold::Satisfied;GameplayContext context{};Revision revision{};};
struct NeedsLifeSnapshot{std::vector<NeedDefinition> definitions;std::vector<NeedProfile> profiles;std::vector<NeedState> states;std::vector<LifePressure> pressures;std::vector<LifeRoutine> routines;MonotonicIdGenerator<GameplayObjectId>::Snapshot profile_ids{};MonotonicIdGenerator<GameplayObjectId>::Snapshot pressure_ids{};MonotonicIdGenerator<GameplayObjectId>::Snapshot routine_ids{};Revision revision{};};
struct NeedsLifeDiagnostics{std::uint64_t definitions=0,profiles=0,states=0,critical_needs=0,life_pressures=0,routines=0,threshold_events=0,abstract_simulation_steps=0;};

class NeedsLifeService
{
public:
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept{return GameplayDomainId::FromString("framework.needs_life");}
    [[nodiscard]] foundation::Result<void> RegisterNeedDefinition(NeedDefinition definition);
    [[nodiscard]] foundation::Result<NeedProfileId> CreateNeedProfile(NeedProfile profile,GameplayContext context={});
    [[nodiscard]] NeedState EvaluateNeed(GameplayObjectRef subject,NeedTypeId need,GameplayTimePoint now) const;
    [[nodiscard]] foundation::Result<void> CommitNeedEvaluation(GameplayObjectRef subject,NeedTypeId need,GameplayTimePoint now,GameplayContext context={});
    [[nodiscard]] foundation::Result<void> SatisfyNeed(SatisfyNeedRequest request);
    [[nodiscard]] foundation::Result<LifePressureId> CreateLifePressure(LifePressure pressure,GameplayContext context={});
    [[nodiscard]] foundation::Result<void> ResolveLifePressure(LifePressureId id,GameplayContext context={});
    [[nodiscard]] foundation::Result<LifeRoutineId> SetRoutine(LifeRoutine routine,GameplayContext context={});
    [[nodiscard]] foundation::Result<void> SimulateLifeInterval(LifeSimulationRequest request);
    [[nodiscard]] const NeedProfile* GetNeedProfile(GameplayObjectRef subject) const noexcept;
    [[nodiscard]] const NeedState* GetCommittedNeedState(GameplayObjectRef subject,NeedTypeId need) const noexcept;
    [[nodiscard]] std::int64_t GetNeedUrgency(GameplayObjectRef subject,NeedTypeId need,GameplayTimePoint now) const;
    [[nodiscard]] std::vector<NeedState> FindCriticalNeeds(GameplayTimePoint now) const;
    [[nodiscard]] std::vector<LifePressure> FindLifePressures(GameplayObjectRef subject) const;
    [[nodiscard]] const LifeRoutine* GetRoutine(GameplayObjectRef subject) const noexcept;
    [[nodiscard]] std::vector<NeedsLifeChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] NeedsLifeSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(NeedsLifeSnapshot snapshot);
    [[nodiscard]] NeedsLifeDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept{return revision_;}
private:
    void Bump() noexcept{revision_.value++;}
    void Record(NeedsLifeChange change);
    [[nodiscard]] NeedThreshold ThresholdFor(const NeedDefinition& def,std::int64_t value) const noexcept;
    [[nodiscard]] std::int64_t Clamp(const NeedDefinition& def,std::int64_t value) const noexcept{return std::max(def.min_value,std::min(def.max_value,value));}
    [[nodiscard]] std::uint64_t StateKey(GameplayObjectRef subject,NeedTypeId need) const noexcept;
    Revision revision_{};
    MonotonicIdGenerator<GameplayObjectId> profile_ids_{0x2900};
    MonotonicIdGenerator<GameplayObjectId> pressure_ids_{0x2901};
    MonotonicIdGenerator<GameplayObjectId> routine_ids_{0x2902};
    std::unordered_map<NeedTypeId,NeedDefinition,IdHash> definitions_;
    std::unordered_map<NeedProfileId,NeedProfile,IdHash> profiles_;
    std::unordered_map<std::uint64_t,NeedState> states_;
    std::unordered_map<LifePressureId,LifePressure,IdHash> pressures_;
    std::unordered_map<LifeRoutineId,LifeRoutine,IdHash> routines_;
    std::vector<NeedsLifeChange> changes_;
    std::uint64_t next_change_sequence_=1;
    mutable NeedsLifeDiagnostics diagnostics_{};
};
}
