#include "Epidemic/GameFramework/TraversalNavigationConstructionIntegration/traversal_navigation_construction_adapters.h"

namespace epidemic::gameplay::traversal_navigation_construction
{
navigation_semantics::NavigationPermissionResult TraversalNavigationAdapter::CanUseLink(
    const traversal::TraversalService& traversal,
    const navigation_semantics::NavigationSemanticsService& navigation,
    GameplayObjectRef subject,
    navigation_semantics::NavigationLinkId link) const
{
    auto nav_result = navigation.CanUseLink(subject, link);
    if (nav_result.decision == navigation_semantics::NavigationDecisionKind::Deny)
    {
        return nav_result;
    }
    const auto* semantic_link = navigation.FindLink(link);
    if (semantic_link == nullptr)
    {
        nav_result.decision = navigation_semantics::NavigationDecisionKind::Deny;
        return nav_result;
    }
    const auto* state = traversal.FindState(subject);
    if (state == nullptr || traversal.CanUseMode(subject, state->current_mode).kind == traversal::TraversalResultKind::Rejected)
    {
        nav_result.decision = navigation_semantics::NavigationDecisionKind::Deny;
        nav_result.reasons.push_back({navigation_semantics::NavigationReasonId::FromString("navigation.traversal_mode_rejected"), {}, {}, link, navigation_semantics::NavigationDecisionKind::Deny});
    }
    return nav_result;
}

foundation::Result<void> ConstructionNavigationAdapter::ApplyPlacementOutputs(
    const construction::PlacementCommitResult& result,
    navigation_semantics::NavigationSemanticsService& navigation,
    GameplayContext context) const
{
    for (const auto& output : result.outputs)
    {
        if (output.type == construction::PlacementOutputTypeId::FromString("construction.output.navigation_layer"))
        {
            navigation_semantics::NavigationSemanticLayer layer;
            layer.area = output.subject;
            layer.type = navigation_semantics::NavigationLayerTypeId::FromString("construction.placed_object.footprint");
            layer.decision = navigation_semantics::NavigationDecisionKind::AddCost;
            layer.additive_cost_micro = 250'000;
            layer.priority = 10;
            auto added = navigation.AddLayer(layer, context);
            if (!added)
            {
                return foundation::Result<void>::Failure(added.GetError());
            }
        }
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> ConstructionTraversalAdapter::CancelTraversalSessionsBlockedByDestroyedLink(
    traversal::TraversalService& traversal,
    navigation_semantics::NavigationLinkId link,
    std::span<const traversal::TraversalSessionId> affected_sessions,
    GameplayContext context) const
{
    (void)link;
    const auto reason = traversal::TraversalReasonId::FromString("traversal.route_invalidated");
    for (const auto session : affected_sessions)
    {
        auto cancelled = traversal.CancelSession(session, reason, context);
        if (!cancelled)
        {
            return cancelled;
        }
    }
    return foundation::Result<void>::Success();
}
} // namespace epidemic::gameplay::traversal_navigation_construction
