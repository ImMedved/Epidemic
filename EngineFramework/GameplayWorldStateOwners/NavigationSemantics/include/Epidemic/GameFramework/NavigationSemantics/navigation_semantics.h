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
    NavigationLayerTypeId type{};
    std::int32_t priority = 0;
    NavigationLayerLifetime lifetime = NavigationLayerLifetime::Persistent;
    NavigationDecisionKind decision = NavigationDecisionKind::AddCost;
    Fixed additive_cost_micro = 0;
    Fixed multiplier_micro = 1'000'000;
    GameplayTagSet tags;
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
    GameplayTagSet required_layer_tags;
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
struct NavigationSnapshot
{
    std::vector<NavigationSemanticProfile> profiles;
    std::vector<NavigationSemanticLayer> layers;
    std::vector<NavigationSemanticLink> links;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot layer_ids{};
    Revision revision{};
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
    [[nodiscard]] foundation::Result<void> SetProfile(NavigationSemanticProfile profile, GameplayContext context = {});
    [[nodiscard]] foundation::Result<NavigationLayerId> AddLayer(NavigationSemanticLayer layer,
                                                                 GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RemoveLayer(NavigationLayerId id, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> AddOrUpdateLink(NavigationSemanticLink link, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> SetLinkState(NavigationLinkId id, LinkState state,
                                                        GameplayContext context = {});
    [[nodiscard]] NavigationPermissionResult CanEnterArea(GameplayObjectRef subject, GameplayObjectRef area) const;
    [[nodiscard]] NavigationPermissionResult CanUseLink(GameplayObjectRef subject, NavigationLinkId link) const;
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
    [[nodiscard]] std::uint64_t LatestChangeSequence() const noexcept
    {
        return next_change_sequence_ - 1;
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
    [[nodiscard]] NavigationPermissionResult EvaluateAreaForProfile(const NavigationSemanticProfile *profile,
                                                                    GameplayObjectRef area) const;
    std::unordered_map<NavigationDomainId, NavigationDomainDefinition, IdHash> domains_;
    std::vector<NavigationRuleDefinition> rules_;
    std::unordered_map<GameplayObjectRef, NavigationSemanticProfile, RefHash> profiles_;
    std::unordered_map<NavigationLayerId, NavigationSemanticLayer, IdHash> layers_;
    std::unordered_map<NavigationLinkId, NavigationSemanticLink, IdHash> links_;
    MonotonicIdGenerator<GameplayObjectId> layer_ids_;
    Revision revision_{};
    bool frozen_ = false;
    std::vector<NavigationChange> changes_;
    std::uint64_t next_change_sequence_ = 1;
    mutable std::uint64_t permission_queries_ = 0, denied_queries_ = 0, cost_modified_queries_ = 0;
    std::uint64_t dynamic_updates_ = 0;
};
} // namespace epidemic::gameplay::navigation_semantics
