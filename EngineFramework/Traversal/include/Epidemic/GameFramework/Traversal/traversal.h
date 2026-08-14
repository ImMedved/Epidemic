#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::traversal
{
struct TraversalModeId { TypeId value{}; static constexpr TraversalModeId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const TraversalModeId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const TraversalModeId&) const noexcept = default; };
struct TraversalProfileId { TypeId value{}; static constexpr TraversalProfileId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const TraversalProfileId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const TraversalProfileId&) const noexcept = default; };
struct TraversalSessionId { GameplayObjectId value{}; static constexpr TraversalSessionId FromRaw(std::uint64_t h, std::uint64_t l) noexcept { return {GameplayObjectId::FromRaw(h,l)}; } static constexpr TraversalSessionId FromString(std::string_view s) noexcept { return {GameplayObjectId::FromString(s)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const TraversalSessionId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const TraversalSessionId&) const noexcept = default; };
struct TraversalRouteId { GameplayObjectId value{}; static constexpr TraversalRouteId FromString(std::string_view s) noexcept { return {GameplayObjectId::FromString(s)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const TraversalRouteId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const TraversalRouteId&) const noexcept = default; };
struct TraversalCapabilityId { TypeId value{}; static constexpr TraversalCapabilityId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const TraversalCapabilityId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const TraversalCapabilityId&) const noexcept = default; };
struct TraversalCarrierRoleId { TypeId value{}; static constexpr TraversalCarrierRoleId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const TraversalCarrierRoleId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const TraversalCarrierRoleId&) const noexcept = default; };
struct TraversalReasonId { TypeId value{}; static constexpr TraversalReasonId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const TraversalReasonId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const TraversalReasonId&) const noexcept = default; };

struct IdHash
{
    template <class T> [[nodiscard]] std::size_t operator()(const T& id) const noexcept { return std::hash<decltype(id.value)>{}(id.value); }
};
struct RefHash { [[nodiscard]] std::size_t operator()(GameplayObjectRef ref) const noexcept { return std::hash<GameplayObjectRef>{}(ref); } };

using Fixed = std::int64_t;
struct WorldPosition { Fixed x_mm=0,y_mm=0,z_mm=0; [[nodiscard]] constexpr bool operator==(const WorldPosition&) const noexcept = default; };

enum class TraversalMaterializationPolicy { AbstractCapable, RequiresMaterialized, RequiresRuntimeProjection };
enum class TraversalSessionState { Preparing, Active, Suspended, Completed, Cancelled, Failed };
enum class TraversalChangeKind { ProfileAssigned, ModeChanged, RequestRejected, SessionStarted, SessionCompleted, SessionCancelled, CarrierBoarded, CarrierDisembarked };

enum class TraversalResultKind { Accepted, Rejected, NoOp };

struct TraversalModeDefinition
{
    TraversalModeId id{};
    std::string canonical_name;
    GameplayTagSet tags;
    TraversalCapabilityId required_capability{};
    TraversalMaterializationPolicy materialization_policy = TraversalMaterializationPolicy::AbstractCapable;
    Fixed base_speed_micro = 1'000'000;
    Fixed acceleration_modifier_micro = 1'000'000;
};

struct TraversalProfile
{
    TraversalProfileId id{};
    std::string canonical_name;
    GameplayTagSet tags;
    TraversalModeId default_mode{};
    std::vector<TraversalModeId> allowed_modes;
    std::vector<TraversalCapabilityId> capabilities;
    Revision revision{};
};

struct TraversalState
{
    GameplayObjectRef subject{};
    TraversalModeId current_mode{};
    TraversalProfileId profile{};
    std::optional<TraversalSessionId> active_session{};
    Revision revision{};
};

struct TraversalRoute
{
    TraversalRouteId id{};
    GameplayObjectRef subject{};
    GameplayObjectRef from_area{};
    GameplayObjectRef to_area{};
    TraversalModeId mode{};
    std::vector<std::byte> payload;
    Revision revision{};
};

struct TraversalSession
{
    TraversalSessionId id{};
    GameplayObjectRef subject{};
    TraversalModeId mode{};
    TraversalRouteId route{};
    TraversalSessionState state = TraversalSessionState::Preparing;
    GameplayTimePoint started_at{};
    Revision revision{};
};

struct TraversalCarrierBinding
{
    GameplayObjectRef passenger{};
    GameplayObjectRef carrier{};
    TraversalModeId carrier_mode{};
    TraversalCarrierRoleId role{};
    Revision revision{};
};

struct ChangeTraversalModeRequest { GameplayObjectRef subject{}; TraversalModeId target_mode{}; GameplayContext context{}; };
struct TraversalRequest
{
    GameplayObjectRef subject{};
    TraversalModeId mode{};
    std::optional<WorldPosition> target_position{};
    std::optional<GameplayObjectRef> target_area{};
    std::optional<GameplayObjectRef> target_object{};
    GameplayContext context{};
};
struct TraversalResult { TraversalResultKind kind=TraversalResultKind::Rejected; TraversalReasonId reason{}; Revision revision{}; };
struct TraversalChange { std::uint64_t sequence=0; TraversalChangeKind kind=TraversalChangeKind::ProfileAssigned; GameplayObjectRef subject{}; TraversalModeId mode{}; TraversalSessionId session{}; GameplayObjectRef carrier{}; TraversalReasonId reason{}; GameplayContext context{}; Revision revision{}; };
struct TraversalSnapshot { std::vector<TraversalState> states; std::vector<TraversalSession> sessions; std::vector<TraversalCarrierBinding> carrier_bindings; MonotonicIdGenerator<GameplayObjectId>::Snapshot session_ids{}; Revision revision{}; };
struct TraversalDiagnostics { std::uint64_t profiles=0,modes=0,states=0,active_sessions=0,carrier_bindings=0,mode_changes=0,rejected_mode_changes=0,boarding_ops=0; };

class TraversalService
{
public:
    TraversalService();
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept { return GameplayDomainId::FromString("framework.traversal"); }
    [[nodiscard]] foundation::Result<void> RegisterMode(TraversalModeDefinition definition);
    [[nodiscard]] foundation::Result<void> RegisterProfile(TraversalProfile profile);
    void Freeze() noexcept { frozen_=true; }
    [[nodiscard]] bool IsFrozen() const noexcept { return frozen_; }
    [[nodiscard]] const TraversalModeDefinition* FindMode(TraversalModeId id) const noexcept;
    [[nodiscard]] const TraversalProfile* FindProfile(TraversalProfileId id) const noexcept;

    [[nodiscard]] foundation::Result<void> AssignProfile(GameplayObjectRef subject, TraversalProfileId profile, GameplayContext context={});
    [[nodiscard]] foundation::Result<TraversalResult> ChangeMode(ChangeTraversalModeRequest request);
    [[nodiscard]] TraversalResult CanUseMode(GameplayObjectRef subject, TraversalModeId mode) const;

    [[nodiscard]] foundation::Result<TraversalRouteId> RegisterRoute(TraversalRoute route);
    [[nodiscard]] foundation::Result<TraversalSessionId> StartSession(GameplayObjectRef subject, TraversalModeId mode, TraversalRouteId route={}, GameplayTimePoint started_at={}, GameplayContext context={});
    [[nodiscard]] foundation::Result<void> CompleteSession(TraversalSessionId id, GameplayContext context={});
    [[nodiscard]] foundation::Result<void> CancelSession(TraversalSessionId id, TraversalReasonId reason={}, GameplayContext context={});

    [[nodiscard]] foundation::Result<void> BoardCarrier(GameplayObjectRef passenger, GameplayObjectRef carrier, TraversalModeId mode, TraversalCarrierRoleId role, GameplayContext context={});
    [[nodiscard]] foundation::Result<void> Disembark(GameplayObjectRef passenger, GameplayContext context={});

    [[nodiscard]] const TraversalState* FindState(GameplayObjectRef subject) const noexcept;
    [[nodiscard]] const TraversalSession* FindSession(TraversalSessionId id) const noexcept;
    [[nodiscard]] std::vector<TraversalState> FindSubjectsUsingMode(TraversalModeId mode) const;
    [[nodiscard]] std::vector<TraversalCarrierBinding> FindCarrierPassengers(GameplayObjectRef carrier) const;
    [[nodiscard]] std::vector<TraversalChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] std::uint64_t LatestChangeSequence() const noexcept { return next_change_sequence_-1; }
    [[nodiscard]] TraversalSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(TraversalSnapshot snapshot);
    [[nodiscard]] TraversalDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept { return revision_; }

private:
    [[nodiscard]] TraversalState* FindMutableState(GameplayObjectRef subject) noexcept;
    [[nodiscard]] TraversalSession* FindMutableSession(TraversalSessionId id) noexcept;
    [[nodiscard]] bool ProfileAllows(const TraversalProfile& profile, TraversalModeId mode) const noexcept;
    [[nodiscard]] bool ProfileHasCapability(const TraversalProfile& profile, TraversalCapabilityId capability) const noexcept;
    void Bump() noexcept { ++revision_.value; }
    void Record(TraversalChange change);

    std::unordered_map<TraversalModeId, TraversalModeDefinition, IdHash> modes_;
    std::unordered_map<TraversalProfileId, TraversalProfile, IdHash> profiles_;
    std::unordered_map<GameplayObjectRef, TraversalState, RefHash> states_;
    std::unordered_map<TraversalSessionId, TraversalSession, IdHash> sessions_;
    std::unordered_map<TraversalRouteId, TraversalRoute, IdHash> routes_;
    std::unordered_map<GameplayObjectRef, TraversalCarrierBinding, RefHash> carrier_by_passenger_;
    MonotonicIdGenerator<GameplayObjectId> session_ids_;
    Revision revision_{};
    bool frozen_=false;
    std::vector<TraversalChange> changes_;
    std::uint64_t next_change_sequence_=1;
    std::uint64_t mode_changes_=0,rejected_mode_changes_=0,boarding_ops_=0;
};
} // namespace epidemic::gameplay::traversal
