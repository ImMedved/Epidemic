#include "Epidemic/GameFramework/TraversalNavigationConstructionIntegration/traversal_navigation_construction_adapters.h"
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::traversal;
using namespace epidemic::gameplay::navigation_semantics;
using namespace epidemic::gameplay::construction;
using namespace epidemic::gameplay::traversal_navigation_construction;
int main()
{
    TraversalService t;
    auto walk = TraversalModeId::FromString("game.walk");
    TraversalModeDefinition w;
    w.id = walk;
    w.canonical_name = "game.walk";
    if (!t.RegisterMode(w))
        return 1;
    TraversalProfile p;
    p.id = TraversalProfileId::FromString("game.human");
    p.canonical_name = "game.human";
    p.default_mode = walk;
    p.allowed_modes = {walk};
    if (!t.RegisterProfile(p))
        return 2;
    t.Freeze();
    NavigationSemanticsService n;
    NavigationDomainDefinition d;
    d.id = NavigationDomainId::FromString("game.humanoid");
    d.canonical_name = "game.humanoid";
    if (!n.RegisterDomain(d))
        return 3;
    n.Freeze();
    ConstructionService c;
    PlacementDefinition def;
    def.id = PlacementRuleId::FromString("game.free");
    def.canonical_name = "game.free";
    def.target_kind = PlacementTargetKind::Free;
    if (!c.RegisterPlacementDefinition(def))
        return 4;
    ConstructionRecipe recipe;
    recipe.id = ConstructionRecipeId::FromString("game.bridge");
    recipe.canonical_name = "game.bridge";
    if (!c.RegisterRecipe(recipe))
        return 5;
    c.Freeze();
    GameplayObjectRef actor{GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("actor")};
    GameplayObjectRef a{GameplayDomainId::FromString("test.area"), GameplayObjectId::FromString("a")};
    GameplayObjectRef b{GameplayDomainId::FromString("test.area"), GameplayObjectId::FromString("b")};
    if (!t.AssignProfile(actor, p.id))
        return 6;
    if (!n.SetProfile({actor, d.id, {}, {}}))
        return 7;
    NavigationSemanticLink link;
    link.id = NavigationLinkId::FromString("bridge.link");
    link.from_area = a;
    link.to_area = b;
    link.type = NavigationLinkTypeId::FromString("game.bridge");
    if (!n.AddOrUpdateLink(link))
        return 8;
    TraversalNavigationAdapter tn;
    if (tn.CanUseLink(t, n, actor, link.id).decision == NavigationDecisionKind::Deny)
        return 9;
    PlacementRequest req;
    req.actor = actor;
    req.recipe = recipe.id;
    req.placement_rule = def.id;
    req.target.position = construction::WorldPosition{0, 0, 0};
    req.target.area = b;
    auto plan = c.PreparePlacementPlan(req);
    if (!plan)
        return 10;
    auto result = c.CommitPlacement(plan.Value());
    if (!result)
        return 11;
    ConstructionNavigationAdapter cn;
    if (!cn.ApplyPlacementOutputs(result.Value(), n))
        return 12;
    if (n.FindLayersInArea(b).empty())
        return 13;
    auto session = t.StartSession(actor, walk);
    if (!session)
        return 14;
    ConstructionTraversalAdapter ct;
    std::vector<TraversalSessionId> ids{session.Value()};
    if (!ct.CancelTraversalSessionsBlockedByDestroyedLink(t, link.id, ids))
        return 15;
    if (t.FindSession(session.Value())->state != TraversalSessionState::Cancelled)
        return 16;
    return 0;
}
