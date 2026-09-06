#include "Epidemic/GameFramework/TraversalNavigationConstructionIntegration/traversal_navigation_construction_adapters.h"

using namespace epidemic;
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::traversal;
using namespace epidemic::gameplay::navigation_semantics;
using namespace epidemic::gameplay::construction;
using namespace epidemic::gameplay::traversal_navigation_construction;

namespace
{
class PlacementProvider final : public IConstructionPlacementProvider
{
  public:
    [[nodiscard]] foundation::Result<PlacementSemanticProjection> Project(const PlacementRequest &request) const override
    {
        PlacementSemanticProjection projection;
        projection.materialized = true;
        projection.area_allowed = true;
        projection.permission_granted = true;
        projection.capability_available = true;
        projection.spacing_clear = true;
        projection.revision = {1};
        if (request.target.area)
            projection.footprint.volume.area = *request.target.area;
        if (request.target.position)
        {
            projection.footprint.volume.min = *request.target.position;
            projection.footprint.volume.max = {request.target.position->x_mm + 100, request.target.position->y_mm + 100,
                                               request.target.position->z_mm + 100};
        }
        return foundation::Result<PlacementSemanticProjection>::Success(projection);
    }

    [[nodiscard]] Revision CurrentRevision() const noexcept override { return {1}; }
};

PlacementOutputOperation NavigationOutput(GameplayObjectRef area, const ConstructionNavigationLayerPayload& payload)
{
    PlacementOutputOperation output;
    output.type = PlacementOutputTypeId::FromString("construction.output.navigation_layer");
    output.subject = area;
    output.payload = EncodeNavigationLayerPayload(payload);
    return output;
}
} // namespace

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
    PlacementProvider provider;
    c.SetPlacementProvider(&provider);
    PlacementDefinition def;
    def.id = PlacementRuleId::FromString("game.free");
    def.canonical_name = "game.free";
    def.target_kind = PlacementTargetKind::Free;
    if (!c.RegisterPlacementDefinition(def))
        return 4;

    const auto source_key = TypeId::FromString("game.bridge.navigation.primary");
    ConstructionRecipe recipe;
    recipe.id = ConstructionRecipeId::FromString("game.bridge");
    recipe.canonical_name = "game.bridge";
    recipe.footprint_size_mm = {100, 100, 100};
    PlacementOutputTemplate output;
    output.type = PlacementOutputTypeId::FromString("construction.output.navigation_layer");
    output.subject_is_target_area = true;
    ConstructionNavigationLayerPayload payload;
    payload.operation = ConstructionNavigationOperation::Add;
    payload.source_key = source_key;
    payload.layer_type = NavigationLayerTypeId::FromString("game.bridge.layer");
    payload.decision = NavigationDecisionKind::AddCost;
    payload.additive_cost_micro = 500;
    output.payload = EncodeNavigationLayerPayload(payload);
    recipe.output_templates.push_back(output);
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

    // H73: the adapter must use the same context-complete evaluator as NavigationSemantics.
    NavigationSemanticLayer timed_block;
    timed_block.id = NavigationLayerId::FromString("test.timed.block");
    timed_block.area = b;
    timed_block.type = NavigationLayerTypeId::FromString("test.timed.block");
    timed_block.decision = NavigationDecisionKind::Deny;
    timed_block.lifetime = NavigationLayerLifetime::Timed;
    timed_block.expires_at = GameplayTimePoint{10};
    if (!n.AddLayer(timed_block))
        return 9;
    TraversalNavigationAdapter tn;
    GameplayContext before_expiry;
    before_expiry.time = GameplayTimePoint{5};
    if (tn.CanUseLink(t, n, actor, link.id, before_expiry).decision != NavigationDecisionKind::Deny)
        return 10;
    GameplayContext after_expiry;
    after_expiry.time = GameplayTimePoint{11};
    if (tn.CanUseLink(t, n, actor, link.id, after_expiry).decision == NavigationDecisionKind::Deny)
        return 11;

    PlacementRequest req;
    req.actor = actor;
    req.recipe = recipe.id;
    req.placement_rule = def.id;
    req.target.position = construction::WorldPosition{0, 0, 0};
    req.target.area = b;
    auto plan = c.PreparePlacementPlan(req);
    if (!plan)
        return 12;
    auto result = c.CommitPlacement(plan.Value());
    if (!result)
        return 13;
    const auto placed = result.Value().placed_object;

    // H74/H75: production path consumes the durable outbox and explicit versioned payload only.
    ConstructionNavigationAdapter cn;
    auto processed = cn.ProcessPendingOutputs(c, n);
    if (!processed || processed.Value() != 1)
        return 14;
    const auto layer_id = NavigationLayerIdFor(placed, source_key);
    const auto* layer = n.FindLayer(layer_id);
    if (layer == nullptr || layer->area != b || layer->additive_cost_micro != 500 || !layer->source.IsValid())
        return 15;
    auto replay = cn.ProcessPendingOutputs(c, n);
    if (!replay || replay.Value() != 0)
        return 16;

    // H76: update keeps the same stable layer identity and duplicate delivery is harmless.
    ConstructionNavigationLayerPayload update_payload = payload;
    update_payload.operation = ConstructionNavigationOperation::Update;
    update_payload.additive_cost_micro = 900;
    if (!cn.QueueNavigationOperation(c, placed, b, update_payload))
        return 17;
    if (!cn.QueueNavigationOperation(c, placed, b, update_payload))
        return 18;
    processed = cn.ProcessPendingOutputs(c, n);
    if (!processed || processed.Value() != 2)
        return 19;
    layer = n.FindLayer(layer_id);
    if (layer == nullptr || layer->additive_cost_micro != 900)
        return 20;

    // Remove is also idempotent: a duplicate remove acknowledges an already absent layer.
    ConstructionNavigationLayerPayload remove_payload = payload;
    remove_payload.operation = ConstructionNavigationOperation::Remove;
    if (!cn.QueueNavigationOperation(c, placed, {}, remove_payload))
        return 21;
    if (!cn.QueueNavigationOperation(c, placed, {}, remove_payload))
        return 22;
    processed = cn.ProcessPendingOutputs(c, n);
    if (!processed || processed.Value() != 2 || n.FindLayer(layer_id) != nullptr)
        return 23;

    // Unsupported/corrupted durable schema is rejected and remains pending rather than being acknowledged.
    auto malformed = NavigationOutput(b, payload);
    malformed.payload.front() = std::byte{0};
    if (!c.EnqueuePlacedObjectOutput(placed, std::move(malformed)))
        return 24;
    processed = cn.ProcessPendingOutputs(c, n);
    if (processed || c.PendingOutputs().size() != 1)
        return 25;

    // M28: the helper is explicit about cancelling the caller-supplied sessions; it no longer pretends to inspect a link.
    auto session = t.StartSession(actor, walk);
    if (!session)
        return 26;
    ConstructionTraversalAdapter ct;
    std::vector<TraversalSessionId> ids{session.Value()};
    if (!ct.CancelTraversalSessions(t, ids))
        return 27;
    if (t.FindSession(session.Value()) != nullptr)
        return 28;
    if (!ct.CancelTraversalSessions(t, ids))
        return 29;
    if (t.GetDiagnostics().active_sessions != 0)
        return 30;
    return 0;
}
