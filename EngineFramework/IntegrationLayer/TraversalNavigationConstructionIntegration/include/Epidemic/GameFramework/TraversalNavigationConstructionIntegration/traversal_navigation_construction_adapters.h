#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Construction/construction.h"
#include "Epidemic/GameFramework/NavigationSemantics/navigation_semantics.h"
#include "Epidemic/GameFramework/Traversal/traversal.h"

#include <cstddef>
#include <span>
#include <vector>

namespace epidemic::gameplay::traversal_navigation_construction
{
class TraversalNavigationCapabilityProvider final : public navigation_semantics::INavigationCapabilityProvider
{
  public:
    explicit TraversalNavigationCapabilityProvider(const traversal::TraversalService &traversal) : traversal_(traversal) {}
    [[nodiscard]] bool HasCapability(GameplayObjectRef subject, TypeId capability,
                                     navigation_semantics::Fixed min_parameter_micro) const noexcept override;

  private:
    const traversal::TraversalService &traversal_;
};

class TraversalNavigationAdapter
{
  public:
    [[nodiscard]] navigation_semantics::NavigationPermissionResult CanUseLink(
        const traversal::TraversalService &traversal,
        const navigation_semantics::NavigationSemanticsService &navigation,
        GameplayObjectRef subject,
        navigation_semantics::NavigationLinkId link) const;
};

struct ConstructionNavigationLayerPayload
{
    navigation_semantics::NavigationLayerTypeId layer_type{};
    navigation_semantics::NavigationDecisionKind decision = navigation_semantics::NavigationDecisionKind::AddCost;
    navigation_semantics::NavigationLayerLifetime lifetime = navigation_semantics::NavigationLayerLifetime::Persistent;
    std::int32_t priority = 0;
    navigation_semantics::Fixed additive_cost_micro = 0;
    navigation_semantics::Fixed multiplier_micro = 1'000'000;
    TypeId requirement{};
    navigation_semantics::Fixed required_parameter_micro = 0;
};

[[nodiscard]] std::vector<std::byte> EncodeNavigationLayerPayload(const ConstructionNavigationLayerPayload &payload);

class ConstructionNavigationAdapter
{
  public:
    // Legacy immediate adapter. Prefer ProcessPendingOutputs for crash/replay safety.
    [[nodiscard]] foundation::Result<void> ApplyPlacementOutputs(
        const construction::PlacementCommitResult &result,
        navigation_semantics::NavigationSemanticsService &navigation,
        GameplayContext context = {}) const;

    // Processes only navigation outputs. Other outbox entries remain pending for their owning integration adapters.
    // A stable layer id derived from PlacementOutputId makes replay after a crash idempotent.
    [[nodiscard]] foundation::Result<std::size_t> ProcessPendingOutputs(
        construction::ConstructionService &construction,
        navigation_semantics::NavigationSemanticsService &navigation,
        GameplayContext context = {}) const;

  private:
    [[nodiscard]] foundation::Result<navigation_semantics::NavigationSemanticLayer> DecodeLayer(
        const construction::PlacementOutputOperation &output,
        navigation_semantics::NavigationLayerId stable_id = {}) const;
};

class ConstructionTraversalAdapter
{
  public:
    [[nodiscard]] foundation::Result<void> CancelTraversalSessionsBlockedByDestroyedLink(
        traversal::TraversalService &traversal,
        navigation_semantics::NavigationLinkId link,
        std::span<const traversal::TraversalSessionId> affected_sessions,
        GameplayContext context = {}) const;
};
} // namespace epidemic::gameplay::traversal_navigation_construction
