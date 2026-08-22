#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::construction
{
struct PlacementRuleId
{
    TypeId value{};
    static constexpr PlacementRuleId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const PlacementRuleId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PlacementRuleId &) const noexcept = default;
};
struct PlacementPlanId
{
    GameplayObjectId value{};
    static constexpr PlacementPlanId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    static constexpr PlacementPlanId FromString(std::string_view s) noexcept { return {GameplayObjectId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const PlacementPlanId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PlacementPlanId &) const noexcept = default;
};
struct PlacementExecutionId
{
    GameplayObjectId value{};
    static constexpr PlacementExecutionId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    static constexpr PlacementExecutionId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const PlacementExecutionId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PlacementExecutionId &) const noexcept = default;
};
struct ConstructionRecipeId
{
    TypeId value{};
    static constexpr ConstructionRecipeId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const ConstructionRecipeId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const ConstructionRecipeId &) const noexcept = default;
};
struct ConstructionSiteId
{
    GameplayObjectId value{};
    static constexpr ConstructionSiteId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const ConstructionSiteId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const ConstructionSiteId &) const noexcept = default;
};
struct PlacementSocketId
{
    GameplayObjectId value{};
    static constexpr PlacementSocketId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const PlacementSocketId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PlacementSocketId &) const noexcept = default;
};
struct PlacedObjectId
{
    GameplayObjectId value{};
    static constexpr PlacedObjectId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const PlacedObjectId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PlacedObjectId &) const noexcept = default;
};
struct PlacementOutputId
{
    GameplayObjectId value{};
    static constexpr PlacementOutputId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const PlacementOutputId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PlacementOutputId &) const noexcept = default;
};
struct ConstructionCostTypeId
{
    TypeId value{};
    static constexpr ConstructionCostTypeId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const ConstructionCostTypeId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const ConstructionCostTypeId &) const noexcept = default;
};
struct PlacementOutputTypeId
{
    TypeId value{};
    static constexpr PlacementOutputTypeId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const PlacementOutputTypeId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PlacementOutputTypeId &) const noexcept = default;
};
struct PlacementReasonId
{
    TypeId value{};
    static constexpr PlacementReasonId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const PlacementReasonId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PlacementReasonId &) const noexcept = default;
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
    [[nodiscard]] std::size_t operator()(GameplayObjectRef ref) const noexcept { return std::hash<GameplayObjectRef>{}(ref); }
};
using Fixed = std::int64_t;

struct WorldPosition
{
    Fixed x_mm = 0, y_mm = 0, z_mm = 0;
    [[nodiscard]] constexpr bool operator==(const WorldPosition &) const noexcept = default;
};
struct OrientationFixed
{
    Fixed yaw_micro = 0, pitch_micro = 0, roll_micro = 0;
    [[nodiscard]] constexpr bool operator==(const OrientationFixed &) const noexcept = default;
};
struct WorldVolumeRef
{
    GameplayObjectRef area{};
    WorldPosition min{};
    WorldPosition max{};
    [[nodiscard]] constexpr bool IsValid() const noexcept { return area.IsValid() || min != max; }
};

enum class PlacementTargetKind
{
    Free,
    Surface,
    Socket,
    Attach,
    Area
};
enum class PlacementAvailability
{
    Available,
    Unavailable,
    Blocked,
    RequiresResource,
    RequiresPermission,
    RequiresCapability,
    InvalidTarget,
    NotMaterialized,
    Unsupported
};
enum class PlacementCommitPolicy
{
    Instant,
    CreateSite,
    PreviewOnly
};
enum class ConstructionCostPolicy
{
    ValidateOnly,
    ReserveThenCommit,
    ConsumeOnCommit,
    Free
};
enum class ConstructionSiteState
{
    Planned,
    UnderConstruction,
    Paused,
    Completed,
    Cancelled,
    Failed,
    ResourcesCommitted,
    Destroyed
};
enum class PlacementPlanState
{
    Prepared,
    Committed,
    Cancelled,
    Expired
};
enum class SocketState
{
    Free,
    Occupied,
    Disabled,
    Reserved
};
enum class ConstructionChangeKind
{
    PlacementValidated,
    PlacementRejected,
    PlacementCommitted,
    PlacedObjectCreated,
    SiteCreated,
    SiteStarted,
    SiteCompleted,
    SiteCancelled,
    SocketReserved,
    SocketReleased,
    SocketOccupied,
    SitePaused,
    SiteResumed,
    SiteDestroyed,
    OutputQueued,
    OutputAcknowledged
};

struct PlacementDefinition
{
    PlacementRuleId id{};
    std::string canonical_name;
    GameplayTagSet tags;
    PlacementTargetKind target_kind = PlacementTargetKind::Free;
    PlacementCommitPolicy commit_policy = PlacementCommitPolicy::Instant;
    bool allow_pitch = true;
    bool allow_roll = true;
    Fixed yaw_step_micro = 0;
    bool requires_permission = false;
    bool requires_capability = false;
};
struct ConstructionCost
{
    ConstructionCostTypeId type{};
    Fixed amount_micro = 0;
    std::vector<std::byte> payload;
};
struct PlacementOutputTemplate
{
    PlacementOutputTypeId type{};
    TypeId archetype{};
    bool subject_is_target_area = false;
    std::vector<std::byte> payload;
};
struct ConstructionRecipe
{
    ConstructionRecipeId id{};
    std::string canonical_name;
    TypeId result_entity_archetype{};
    GameplayTagSet required_surface_tags;
    GameplayTagSet blocked_area_tags;
    GameplayTagSet placement_tags;
    WorldPosition footprint_size_mm{};
    std::vector<ConstructionCost> costs;
    std::vector<PlacementOutputTemplate> output_templates;
    std::vector<std::byte> placement_payload;
};
struct PlacementTarget
{
    std::optional<WorldPosition> position{};
    std::optional<GameplayObjectRef> area{};
    std::optional<GameplayObjectRef> attach_to{};
    std::optional<PlacementSocketId> socket{};
    OrientationFixed orientation{};
};
struct PlacementFootprint
{
    WorldVolumeRef volume{};
    GameplayTagSet footprint_tags;
    std::vector<std::byte> payload;
};
struct PlacementReason
{
    PlacementReasonId id{};
};
struct PlacementSemanticProjection
{
    bool materialized = true;
    bool area_allowed = true;
    bool permission_granted = true;
    bool capability_available = true;
    bool spacing_clear = true;
    GameplayTagSet surface_tags;
    GameplayTagSet area_tags;
    PlacementFootprint footprint{};
    Revision revision{};
};
struct PlacementValidationResult
{
    PlacementAvailability availability = PlacementAvailability::Unavailable;
    std::vector<PlacementReason> reasons;
    PlacementFootprint footprint{};
    Revision dependencies_revision{};
};
struct PlacementPlan
{
    PlacementPlanId id{};
    GameplayObjectRef actor{};
    ConstructionRecipeId recipe{};
    PlacementTarget target{};
    PlacementFootprint footprint{};
    std::vector<ConstructionCost> reserved_costs;
    Revision dependency_revision{};
    GameplayContext context{};
    PlacementPlanState state = PlacementPlanState::Prepared;
    std::optional<PlacementSocketId> dependency_socket{};
    Revision dependency_socket_revision{};
    PlacementRuleId placement_rule{};
    std::uint64_t placement_provider_epoch = 0;
    std::uint64_t cost_provider_epoch = 0;
};
struct PlacementOutputOperation
{
    PlacementOutputTypeId type{};
    GameplayObjectRef subject{};
    TypeId archetype{};
    PlacedObjectId placed_record{};
    std::vector<std::byte> payload;
};
struct PlacementOutputEnvelope
{
    PlacementOutputId id{};
    PlacementExecutionId execution{};
    std::uint32_t ordinal = 0;
    PlacementOutputOperation operation{};
};
struct PlacementCommitResult
{
    PlacementExecutionId execution{};
    PlacedObjectId placed_object{};
    ConstructionSiteId site{};
    std::vector<PlacementOutputOperation> outputs;
    Revision revision{};
};
struct PlacementSocket
{
    PlacementSocketId id{};
    GameplayObjectRef owner{};
    GameplayTagSet accepted_tags;
    SocketState state = SocketState::Free;
    Revision revision{};
};
struct ConstructionSite
{
    ConstructionSiteId id{};
    GameplayObjectRef actor{};
    ConstructionRecipeId recipe{};
    PlacementTarget target{};
    ConstructionSiteState state = ConstructionSiteState::Planned;
    GameplayTimePoint started_at{};
    Fixed progress_micro = 0;
    Revision revision{};
    PlacementPlanId plan{};
    PlacementExecutionId completion_execution{};
    PlacedObjectId placed_object{};
};
struct PlacementRequest
{
    GameplayObjectRef actor{};
    ConstructionRecipeId recipe{};
    PlacementRuleId placement_rule{};
    PlacementTarget target{};
    GameplayContext context{};
};
struct ConstructionCostReservation
{
    GameplayObjectId token{};
};

class IConstructionPlacementProvider
{
  public:
    virtual ~IConstructionPlacementProvider() = default;
    [[nodiscard]] virtual foundation::Result<PlacementSemanticProjection> Project(
        const PlacementRequest &request) const = 0;
    [[nodiscard]] virtual Revision CurrentRevision() const noexcept = 0;
};
class IConstructionCostProvider
{
  public:
    virtual ~IConstructionCostProvider() = default;
    [[nodiscard]] virtual foundation::Result<bool> CanAfford(GameplayObjectRef actor, const ConstructionCost &cost,
                                                             const GameplayContext &context) const = 0;
    [[nodiscard]] virtual foundation::Result<ConstructionCostReservation> Reserve(GameplayObjectRef actor,
                                                                                   const ConstructionCost &cost,
                                                                                   const GameplayContext &context) = 0;
    virtual void Commit(const ConstructionCostReservation &reservation, const GameplayContext &context) noexcept = 0;
    virtual void Release(const ConstructionCostReservation &reservation, const GameplayContext &context) noexcept = 0;
};

struct ConstructionChange
{
    std::uint64_t sequence = 0;
    ConstructionChangeKind kind = ConstructionChangeKind::PlacementValidated;
    PlacementPlanId plan{};
    PlacementExecutionId execution{};
    ConstructionSiteId site{};
    PlacementSocketId socket{};
    GameplayObjectRef actor{};
    Revision revision{};
    GameplayContext context{};
    PlacementOutputId output{};
};
struct ConstructionChangeBatch
{
    bool snapshot_required = false;
    std::uint64_t oldest_available_sequence = 0;
    std::vector<ConstructionChange> changes;
};
struct ConstructionSnapshot
{
    std::vector<PlacementPlan> plans;
    std::vector<ConstructionSite> active_sites;
    std::vector<PlacementSocket> sockets;
    std::vector<PlacedObjectId> placed_objects;
    std::vector<PlacementOutputEnvelope> pending_outputs;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot plan_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot site_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot placed_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot execution_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot output_ids{};
    Revision revision{};
    std::vector<ConstructionChange> journal;
    std::uint64_t next_change_sequence = 1;
};
struct ConstructionDiagnostics
{
    std::uint64_t recipes = 0, placement_definitions = 0, validations = 0, rejections = 0, committed = 0,
                  active_sites = 0, socket_reservations = 0, completed_sites = 0, pending_outputs = 0;
};

class ConstructionService
{
  public:
    ConstructionService();
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept
    {
        return GameplayDomainId::FromString("framework.construction");
    }
    [[nodiscard]] foundation::Result<void> RegisterPlacementDefinition(PlacementDefinition definition);
    [[nodiscard]] foundation::Result<void> RegisterRecipe(ConstructionRecipe recipe);
    [[nodiscard]] foundation::Result<void> RegisterSocket(PlacementSocket socket);
    void Freeze() noexcept { frozen_ = true; }
    [[nodiscard]] bool IsFrozen() const noexcept { return frozen_; }
    void SetPlacementProvider(const IConstructionPlacementProvider *provider) noexcept;
    void SetCostProvider(IConstructionCostProvider *provider) noexcept;
    [[nodiscard]] const ConstructionRecipe *FindRecipe(ConstructionRecipeId id) const noexcept;
    [[nodiscard]] const PlacementDefinition *FindPlacementDefinition(PlacementRuleId id) const noexcept;
    [[nodiscard]] const PlacementSocket *FindSocket(PlacementSocketId id) const noexcept;
    [[nodiscard]] foundation::Result<PlacementValidationResult> ValidatePlacement(const PlacementRequest &request) const;
    [[nodiscard]] foundation::Result<PlacementPlan> PreparePlacementPlan(const PlacementRequest &request);
    [[nodiscard]] foundation::Result<PlacementCommitResult> CommitPlacement(
        PlacementPlanId plan, ConstructionCostPolicy cost_policy = ConstructionCostPolicy::ConsumeOnCommit);
    [[nodiscard]] foundation::Result<PlacementCommitResult> CommitPlacement(
        const PlacementPlan &plan, ConstructionCostPolicy cost_policy = ConstructionCostPolicy::ConsumeOnCommit);
    [[nodiscard]] foundation::Result<ConstructionSiteId> StartConstructionSite(PlacementPlanId plan,
                                                                               GameplayTimePoint started_at = {},
                                                                               GameplayContext context = {});
    [[nodiscard]] foundation::Result<ConstructionSiteId> StartConstructionSite(const PlacementPlan &plan,
                                                                               GameplayTimePoint started_at = {},
                                                                               GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> PauseConstructionSite(ConstructionSiteId id, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> ResumeConstructionSite(ConstructionSiteId id, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> CompleteConstructionSite(ConstructionSiteId id,
                                                                    GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> CancelConstructionSite(ConstructionSiteId id, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> DestroyConstructionSite(ConstructionSiteId id, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> ReserveSocket(PlacementSocketId id, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> ReleaseSocket(PlacementSocketId id, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> OccupySocket(PlacementSocketId id, GameplayContext context = {});
    [[nodiscard]] const PlacementPlan *FindPlan(PlacementPlanId id) const noexcept;
    [[nodiscard]] const ConstructionSite *FindSite(ConstructionSiteId id) const noexcept;
    [[nodiscard]] std::vector<ConstructionSite> FindConstructionSites(GameplayObjectRef actor = {}) const;
    [[nodiscard]] std::vector<PlacementSocket> FindSocketsForObject(GameplayObjectRef owner) const;
    [[nodiscard]] std::vector<PlacementOutputEnvelope> PendingOutputs() const;
    [[nodiscard]] foundation::Result<void> AcknowledgeOutput(PlacementOutputId id, GameplayContext context = {});
    [[nodiscard]] ConstructionSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(ConstructionSnapshot snapshot);
    [[nodiscard]] std::vector<ConstructionChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] ConstructionChangeBatch ReadChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] std::uint64_t LatestChangeSequence() const noexcept { return next_change_sequence_ - 1; }
    [[nodiscard]] ConstructionDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept { return revision_; }

  private:
    void Bump() noexcept;
    void Record(ConstructionChange change);
    [[nodiscard]] PlacementSocket *FindMutableSocket(PlacementSocketId id) noexcept;
    [[nodiscard]] ConstructionSite *FindMutableSite(ConstructionSiteId id) noexcept;
    [[nodiscard]] foundation::Result<std::vector<ConstructionCostReservation>> ReserveCosts(
        const PlacementPlan &plan, ConstructionCostPolicy policy);
    void CommitCosts(const std::vector<ConstructionCostReservation> &reservations, const GameplayContext &context) noexcept;
    void ReleaseCosts(const std::vector<ConstructionCostReservation> &reservations, const GameplayContext &context) noexcept;
    [[nodiscard]] std::vector<PlacementOutputOperation> BuildOutputs(const PlacementPlan &plan,
                                                                     const ConstructionRecipe &recipe,
                                                                     PlacedObjectId placed) const;
    [[nodiscard]] foundation::Result<std::vector<PlacementOutputEnvelope>> StageOutputs(
        PlacementExecutionId execution,
        const std::vector<PlacementOutputOperation> &outputs) const;
    void QueueOutputs(std::vector<PlacementOutputEnvelope> outputs,
                      PlacementPlanId plan, ConstructionSiteId site,
                      GameplayObjectRef actor, GameplayContext context);
    [[nodiscard]] foundation::Result<PlacementValidationResult> ValidatePlacementInternal(
        const PlacementRequest &request, bool count_diagnostics) const;

    std::unordered_map<PlacementRuleId, PlacementDefinition, IdHash> placement_definitions_;
    std::unordered_map<ConstructionRecipeId, ConstructionRecipe, IdHash> recipes_;
    std::unordered_map<PlacementSocketId, PlacementSocket, IdHash> sockets_;
    std::unordered_map<PlacementPlanId, PlacementPlan, IdHash> plans_;
    std::unordered_map<ConstructionSiteId, ConstructionSite, IdHash> sites_;
    std::vector<PlacedObjectId> placed_objects_;
    std::deque<PlacementOutputEnvelope> outbox_;
    MonotonicIdGenerator<GameplayObjectId> plan_ids_, site_ids_, placed_ids_, execution_ids_, output_ids_;
    const IConstructionPlacementProvider *placement_provider_ = nullptr;
    IConstructionCostProvider *cost_provider_ = nullptr;
    std::uint64_t placement_provider_epoch_ = 1;
    std::uint64_t cost_provider_epoch_ = 1;
    Revision revision_{};
    bool frozen_ = false;
    static constexpr std::size_t kChangeJournalCapacity = 4096;
    static constexpr std::size_t kOutboxCapacity = 8192;
    std::deque<ConstructionChange> changes_;
    std::uint64_t next_change_sequence_ = 1;
    mutable std::uint64_t validations_ = 0, rejections_ = 0;
    std::uint64_t committed_ = 0, socket_reservations_ = 0, completed_sites_ = 0;
};
} // namespace epidemic::gameplay::construction

