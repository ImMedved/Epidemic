#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Construction/construction.h"
#include "Epidemic/GameFramework/NavigationSemantics/navigation_semantics.h"
#include "Epidemic/GameFramework/Traversal/traversal.h"

#include <span>

namespace epidemic::gameplay::traversal_navigation_construction
{
class TraversalNavigationAdapter
{
public:
    [[nodiscard]] navigation_semantics::NavigationPermissionResult CanUseLink(
        const traversal::TraversalService& traversal,
        const navigation_semantics::NavigationSemanticsService& navigation,
        GameplayObjectRef subject,
        navigation_semantics::NavigationLinkId link) const;
};

class ConstructionNavigationAdapter
{
public:
    [[nodiscard]] foundation::Result<void> ApplyPlacementOutputs(
        const construction::PlacementCommitResult& result,
        navigation_semantics::NavigationSemanticsService& navigation,
        GameplayContext context = {}) const;
};

class ConstructionTraversalAdapter
{
public:
    [[nodiscard]] foundation::Result<void> CancelTraversalSessionsBlockedByDestroyedLink(
        traversal::TraversalService& traversal,
        navigation_semantics::NavigationLinkId link,
        std::span<const traversal::TraversalSessionId> affected_sessions,
        GameplayContext context = {}) const;
};
} // namespace epidemic::gameplay::traversal_navigation_construction
