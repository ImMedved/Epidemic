#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::navigation_semantics
{
struct NavigationDomainId
{
    TypeId value{};
    static constexpr NavigationDomainId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NavigationDomainId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NavigationDomainId &) const noexcept = default;
};
struct NavigationRuleId
{
    TypeId value{};
    static constexpr NavigationRuleId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NavigationRuleId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NavigationRuleId &) const noexcept = default;
};
struct NavigationLayerId
{
    GameplayObjectId value{};
    static constexpr NavigationLayerId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    static constexpr NavigationLayerId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NavigationLayerId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NavigationLayerId &) const noexcept = default;
};
struct NavigationLinkId
{
    GameplayObjectId value{};
    static constexpr NavigationLinkId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NavigationLinkId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NavigationLinkId &) const noexcept = default;
};
struct NavigationLayerTypeId
{
    TypeId value{};
    static constexpr NavigationLayerTypeId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NavigationLayerTypeId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NavigationLayerTypeId &) const noexcept = default;
};
struct NavigationLinkTypeId
{
    TypeId value{};
    static constexpr NavigationLinkTypeId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NavigationLinkTypeId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NavigationLinkTypeId &) const noexcept = default;
};
struct TraversalModeSemanticId
{
    TypeId value{};
    static constexpr TraversalModeSemanticId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const TraversalModeSemanticId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const TraversalModeSemanticId &) const noexcept = default;
};
struct NavigationReasonId
{
    TypeId value{};
    static constexpr NavigationReasonId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NavigationReasonId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NavigationReasonId &) const noexcept = default;
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
    [[nodiscard]] std::size_t operator()(GameplayObjectRef ref) const noexcept
    {
        return std::hash<GameplayObjectRef>{}(ref);
    }
};
using Fixed = std::int64_t;

// Composition contract: Deny is terminal. Allow does not override a Deny; it means no additional restriction.
// Avoid/Prefer/AddCost only modify semantic cost/availability and remain order-dependent by declared priority/id.
enum class NavigationDecisionKind
{
    Allow,
    Deny,
    AddCost,
    RequireCapability,
    RequireFact,
    Avoid,
    Prefer
};
enum class NavigationAvailability
{
    PhysicallyImpossible,
    TraversalUnsupported,
    Forbidden,
    Unsafe,
    TemporarilyBlocked,
    Available
};
enum class NavigationLayerLifetime
{
    Transient,
    Session,
    Persistent,
    Timed
};
enum class LinkState
{
    Open,
    Closed,
    Locked,
    Blocked,
    Destroyed,
    Disabled,
    Conditional
};
enum class NavigationChangeKind
{
    ProfileChanged,
    LayerAdded,
    LayerRemoved,
    LayerChanged,
    LinkChanged,
    RuleRegistered
};

struct NavigationDomainDefinition
{
    NavigationDomainId id{};
    std::string canonical_name;
    GameplayTagSet tags;
};
struct NavigationSemanticProfile
{
    GameplayObjectRef subject{};
    NavigationDomainId domain{};
    GameplayTagSet navigation_tags;
    Revision revision{};
};
struct NavigationSemanticLayer
{
    NavigationLayerId id{};
    GameplayObjectRef area{};
    GameplayObjectRef source{};
    NavigationLayerTypeId type{};
    std::int32_t priority = 0;
    NavigationLayerLifetime lifetime = NavigationLayerLifetime::Persistent;
    NavigationDecisionKind decision = NavigationDecisionKind::AddCost;
    Fixed additive_cost_micro = 0;
    Fixed multiplier_micro = 1'000'000;
    GameplayTagSet tags;
    TypeId requirement{};
    Fixed required_parameter_micro = 0;
    std::optional<GameplayTimePoint> expires_at{};
    Revision revision{};
};
struct NavigationSemanticLink
{
    NavigationLinkId id{};
    GameplayObjectRef from_area{};
    GameplayObjectRef to_area{};
    NavigationLinkTypeId type{};
    GameplayTagSet tags;
    LinkState state = LinkState::Open;
    TypeId required_fact{};
    Revision revision{};
};
struct NavigationRuleDefinition
{
    NavigationRuleId id{};
    NavigationDomainId domain{};
    std::int32_t priority = 0;
    NavigationDecisionKind decision = NavigationDecisionKind::Allow;
    NavigationReasonId reason{};
    Fixed additive_cost_micro = 0;
    Fixed multiplier_micro = 1'000'000;
    GameplayTagSet required_subject_tags;
    GameplayTagSet required_from_layer_tags;
    GameplayTagSet required_layer_tags;
    TraversalModeSemanticId required_traversal_mode{};
    TypeId requirement{};
    Fixed required_parameter_micro = 0;
};
struct NavigationSemanticCost
{
    Fixed base_multiplier_micro = 1'000'000;
    Fixed additive_cost_micro = 0;
    GameplayTagSet reason_tags;
};
struct NavigationReason
{
    NavigationReasonId id{};
    NavigationRuleId rule{};
    NavigationLayerId layer{};
    NavigationLinkId link{};
    NavigationDecisionKind decision = NavigationDecisionKind::Allow;
};
class INavigationCapabilityProvider
{
  public:
    virtual ~INavigationCapabilityProvider() = default;
    [[nodiscard]] virtual bool HasCapability(GameplayObjectRef subject, TypeId capability, Fixed min_parameter_micro) const noexcept = 0;
};
class INavigationFactProvider
{
  public:
    virtual ~INavigationFactProvider() = default;
    [[nodiscard]] virtual bool HasFact(GameplayObjectRef subject, TypeId fact, const GameplayContext &context) const noexcept = 0;
};

struct NavigationPermissionQuery
{
    GameplayObjectRef subject{};
    GameplayObjectRef from_area{};
    GameplayObjectRef to_area{};
    TraversalModeSemanticId traversal_mode{};
    GameplayContext context{};
};
struct NavigationPermissionResult
{
    NavigationDecisionKind decision = NavigationDecisionKind::Allow;
    NavigationAvailability availability = NavigationAvailability::Available;
    NavigationSemanticCost cost{};
    std::vector<NavigationReason> reasons;
    Revision revision{};
};
struct NavigationChange
{
    std::uint64_t sequence = 0;
    NavigationChangeKind kind = NavigationChangeKind::LayerAdded;
    GameplayObjectRef subject{};
    NavigationLayerId layer{};
    NavigationLinkId link{};
    Revision revision{};
    GameplayContext context{};
};
struct NavigationChangeBatch
{
    bool snapshot_required = false;
    std::uint64_t oldest_available_sequence = 0;
    std::vector<NavigationChange> changes;
};
struct NavigationSnapshot
{
    std::vector<NavigationSemanticProfile> profiles;
    std::vector<NavigationSemanticLayer> layers;
    std::vector<NavigationSemanticLink> links;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot layer_ids{};
    Revision revision{};
    std::vector<NavigationChange> journal;
    std::uint64_t next_change_sequence = 1;
};
struct NavigationDiagnostics
{
    std::uint64_t domains = 0, rules = 0, layers = 0, links = 0, permission_queries = 0, denied_queries = 0,
                  cost_modified_queries = 0, dynamic_updates = 0;
};

class NavigationSemanticsService
{
  public:
    NavigationSemanticsService();
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept
    {
        return GameplayDomainId::FromString("framework.navigation_semantics");
    }
    [[nodiscard]] foundation::Result<void> RegisterDomain(NavigationDomainDefinition definition);
    [[nodiscard]] foundation::Result<void> RegisterRule(NavigationRuleDefinition rule);
    void Freeze() noexcept
    {
        frozen_ = true;
    }
    [[nodiscard]] bool IsFrozen() const noexcept
    {
        return frozen_;
    }
    void SetCapabilityProvider(const INavigationCapabilityProvider *provider) noexcept { capabilities_ = provider; }
    void SetFactProvider(const INavigationFactProvider *provider) noexcept { facts_ = provider; }
    [[nodiscard]] foundation::Result<void> SetProfile(NavigationSemanticProfile profile, GameplayContext context = {});
    [[nodiscard]] foundation::Result<NavigationLayerId> AddLayer(NavigationSemanticLayer layer,
                                                                 GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> UpdateLayer(NavigationLayerId id, NavigationSemanticLayer replacement,
                                                       Revision expected_revision, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RemoveLayer(NavigationLayerId id, GameplayContext context = {});
    [[nodiscard]] std::uint64_t RemoveLayersBySource(GameplayObjectRef source, GameplayContext context = {});
    [[nodiscard]] std::uint64_t ExpireLayers(GameplayTimePoint now, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> AddOrUpdateLink(NavigationSemanticLink link, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RemoveLink(NavigationLinkId id, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> SetLinkState(NavigationLinkId id, LinkState state,
                                                        GameplayContext context = {});
    [[nodiscard]] NavigationPermissionResult CanEnterArea(GameplayObjectRef subject, GameplayObjectRef area,
                                                            TraversalModeSemanticId traversal_mode,
                                                            const GameplayContext &context) const;
    [[nodiscard]] NavigationPermissionResult CanUseLink(GameplayObjectRef subject, NavigationLinkId link,
                                                        TraversalModeSemanticId traversal_mode,
                                                        const GameplayContext &context) const;
    [[nodiscard]] NavigationPermissionResult EvaluatePath(NavigationPermissionQuery query) const;
    [[nodiscard]] std::vector<NavigationSemanticLayer> FindLayersInArea(GameplayObjectRef area) const;
    [[nodiscard]] std::vector<NavigationSemanticLink> FindLinksBetween(GameplayObjectRef from,
                                                                       GameplayObjectRef to) const;
    [[nodiscard]] const NavigationSemanticProfile *FindProfile(GameplayObjectRef subject) const noexcept;
    [[nodiscard]] const NavigationSemanticLink *FindLink(NavigationLinkId id) const noexcept;
    [[nodiscard]] const NavigationSemanticLayer *FindLayer(NavigationLayerId id) const noexcept;
    [[nodiscard]] NavigationSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(NavigationSnapshot snapshot);
    [[nodiscard]] std::vector<NavigationChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] NavigationChangeBatch ReadChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] std::uint64_t LatestChangeSequence() const noexcept
    {
        return next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max()
                                          : next_change_sequence_ - 1;
    }
    [[nodiscard]] NavigationDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept
    {
        return revision_;
    }

  private:
    void Bump() noexcept
    {
        ++revision_.value;
    }
    void Record(NavigationChange change);
    [[nodiscard]] NavigationPermissionResult EvaluatePermission(const NavigationPermissionQuery &query,
                                                                  std::optional<NavigationLinkId> required_link) const;
    [[nodiscard]] NavigationPermissionResult EvaluateAreaForProfile(const NavigationSemanticProfile *profile,
                                                                    GameplayObjectRef from_area, GameplayObjectRef area,
                                                                    TraversalModeSemanticId traversal_mode,
                                                                    const GameplayContext &context) const;
    void IndexLayer(const NavigationSemanticLayer &layer);
    void UnindexLayer(const NavigationSemanticLayer &layer);
    void IndexLink(const NavigationSemanticLink &link);
    void UnindexLink(const NavigationSemanticLink &link);
    void RebuildIndexes();
    [[nodiscard]] foundation::Result<void> ValidateAndAdvanceLayerId(NavigationLayerId id);
    std::unordered_map<NavigationDomainId, NavigationDomainDefinition, IdHash> domains_;
    std::vector<NavigationRuleDefinition> rules_;
    std::unordered_map<GameplayObjectRef, NavigationSemanticProfile, RefHash> profiles_;
    struct LinkPairKey
    {
        GameplayObjectRef from{};
        GameplayObjectRef to{};
        [[nodiscard]] bool operator==(const LinkPairKey &) const noexcept = default;
    };
    struct LinkPairHash
    {
        [[nodiscard]] std::size_t operator()(const LinkPairKey &key) const noexcept
        {
            const auto a = std::hash<GameplayObjectRef>{}(key.from);
            const auto b = std::hash<GameplayObjectRef>{}(key.to);
            return a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6U) + (a >> 2U));
        }
    };
    std::unordered_map<NavigationLayerId, NavigationSemanticLayer, IdHash> layers_;
    std::unordered_map<NavigationLinkId, NavigationSemanticLink, IdHash> links_;
    std::unordered_map<GameplayObjectRef, std::vector<NavigationLayerId>, RefHash> layer_ids_by_area_;
    std::unordered_map<LinkPairKey, std::vector<NavigationLinkId>, LinkPairHash> link_ids_by_pair_;
    MonotonicIdGenerator<GameplayObjectId> layer_ids_;
    const INavigationCapabilityProvider *capabilities_ = nullptr;
    const INavigationFactProvider *facts_ = nullptr;
    Revision revision_{};
    bool frozen_ = false;
    static constexpr std::size_t kChangeJournalCapacity = 4096;
    std::deque<NavigationChange> changes_;
    std::uint64_t next_change_sequence_ = 1;
    mutable std::uint64_t permission_queries_ = 0, denied_queries_ = 0, cost_modified_queries_ = 0;
    std::uint64_t dynamic_updates_ = 0;
};
} // namespace epidemic::gameplay::navigation_semantics
