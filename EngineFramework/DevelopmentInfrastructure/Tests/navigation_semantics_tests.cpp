#include "Epidemic/GameFramework/NavigationSemantics/navigation_semantics.h"

#include <limits>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::navigation_semantics;

namespace
{
class TimedFactProvider final : public INavigationFactProvider
{
  public:
    [[nodiscard]] bool HasFact(GameplayObjectRef, TypeId fact, const GameplayContext &context) const noexcept override
    {
        return fact == TypeId::FromString("game.fact.open") && context.time.ticks >= 50;
    }
};
}

int main()
{
    NavigationSemanticsService n;
    const auto domain = NavigationDomainId::FromString("game.humanoid");
    NavigationDomainDefinition d;
    d.id = domain;
    d.canonical_name = "game.humanoid";
    if (!n.RegisterDomain(d))
        return 1;

    NavigationRuleDefinition r;
    r.id = NavigationRuleId::FromString("game.danger_cost");
    r.domain = domain;
    r.priority = 1;
    r.decision = NavigationDecisionKind::AddCost;
    r.additive_cost_micro = 100;
    if (!n.RegisterRule(r))
        return 2;
    n.Freeze();

    const GameplayObjectRef actor{GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("actor")};
    const GameplayObjectRef source{GameplayDomainId::FromString("test.effect"), GameplayObjectId::FromString("source")};
    const GameplayObjectRef a{GameplayDomainId::FromString("test.area"), GameplayObjectId::FromString("a")};
    const GameplayObjectRef b{GameplayDomainId::FromString("test.area"), GameplayObjectId::FromString("b")};
    const GameplayObjectRef c{GameplayDomainId::FromString("test.area"), GameplayObjectId::FromString("c")};
    if (!n.SetProfile({actor, domain, {}, {}}))
        return 3;

    NavigationSemanticLayer l;
    l.area = b;
    l.source = source;
    l.type = NavigationLayerTypeId::FromString("game.blocked");
    l.decision = NavigationDecisionKind::Deny;
    l.priority = 10;
    const auto lid = n.AddLayer(l);
    if (!lid)
        return 4;

    GameplayContext now;
    now.time = GameplayTimePoint{10};
    if (n.CanEnterArea(actor, b, {}, now).decision != NavigationDecisionKind::Deny)
        return 5;

    const auto *stored_layer = n.FindLayer(lid.Value());
    if (stored_layer == nullptr)
        return 6;
    NavigationSemanticLayer moved = *stored_layer;
    moved.area = c;
    moved.priority = 20;
    moved.source = {};
    moved.lifetime = NavigationLayerLifetime::Transient;
    if (!n.UpdateLayer(lid.Value(), moved, stored_layer->revision))
        return 7;
    const auto *updated = n.FindLayer(lid.Value());
    if (updated == nullptr || updated->area != c || updated->source != source ||
        updated->lifetime != NavigationLayerLifetime::Persistent)
        return 8;
    if (!n.FindLayersInArea(b).empty() || n.FindLayersInArea(c).size() != 1)
        return 9;
    if (n.UpdateLayer(lid.Value(), moved, Revision{1}))
        return 10;
    if (n.RemoveLayersBySource(source) != 1 || n.FindLayer(lid.Value()) != nullptr)
        return 11;

    NavigationSemanticLink link;
    link.id = NavigationLinkId::FromString("a_to_b");
    link.from_area = a;
    link.to_area = b;
    link.type = NavigationLinkTypeId::FromString("game.bridge");
    if (!n.AddOrUpdateLink(link))
        return 12;
    if (n.CanUseLink(actor, link.id, {}, now).decision == NavigationDecisionKind::Deny)
        return 13;
    if (n.FindLinksBetween(a, b).size() != 1)
        return 14;

    TimedFactProvider facts;
    n.SetFactProvider(&facts);
    link.state = LinkState::Conditional;
    link.required_fact = TypeId::FromString("game.fact.open");
    if (!n.AddOrUpdateLink(link))
        return 15;
    GameplayContext before_fact;
    before_fact.time = GameplayTimePoint{49};
    GameplayContext after_fact;
    after_fact.time = GameplayTimePoint{50};
    if (n.CanUseLink(actor, link.id, {}, before_fact).availability != NavigationAvailability::Forbidden)
        return 16;
    if (n.CanUseLink(actor, link.id, {}, after_fact).availability == NavigationAvailability::Forbidden)
        return 17;
    NavigationPermissionQuery path_query{actor, a, b, {}, before_fact};
    if (n.EvaluatePath(path_query).availability != NavigationAvailability::Forbidden)
        return 18;
    path_query.context = after_fact;
    if (n.EvaluatePath(path_query).availability == NavigationAvailability::Forbidden)
        return 19;

    NavigationSemanticLayer timed;
    timed.area = b;
    timed.type = NavigationLayerTypeId::FromString("game.timed_block");
    timed.lifetime = NavigationLayerLifetime::Timed;
    timed.decision = NavigationDecisionKind::Deny;
    timed.expires_at = GameplayTimePoint{100};
    const auto timed_id = n.AddLayer(timed);
    if (!timed_id)
        return 20;
    GameplayContext at_99;
    at_99.time = GameplayTimePoint{99};
    GameplayContext at_100;
    at_100.time = GameplayTimePoint{100};
    if (n.CanEnterArea(actor, b, {}, at_99).decision != NavigationDecisionKind::Deny)
        return 21;
    if (n.CanEnterArea(actor, b, {}, at_100).decision == NavigationDecisionKind::Deny)
        return 22;
    if (n.CanUseLink(actor, link.id, {}, at_100).decision == NavigationDecisionKind::Deny)
        return 23;
    NavigationPermissionQuery timed_path{actor, a, b, {}, at_100};
    if (n.EvaluatePath(timed_path).decision == NavigationDecisionKind::Deny)
        return 24;

    NavigationSemanticLayer session;
    session.area = c;
    session.type = NavigationLayerTypeId::FromString("game.session");
    session.lifetime = NavigationLayerLifetime::Session;
    const auto session_id = n.AddLayer(session);
    if (!session_id)
        return 25;
    const auto snapshot_with_session = n.CaptureSnapshot();
    for (const auto &layer_snapshot : snapshot_with_session.layers)
        if (layer_snapshot.id == session_id.Value())
            return 26;

    const auto generator_before = n.CaptureSnapshot().layer_ids;
    if (generator_before.next == 0 || generator_before.next > std::numeric_limits<std::uint64_t>::max() - 10)
        return 27;
    NavigationSemanticLayer requested;
    requested.id = NavigationLayerId::FromRaw(generator_before.scope, generator_before.next + 10);
    requested.area = c;
    requested.type = NavigationLayerTypeId::FromString("game.requested");
    const auto requested_id = n.AddLayer(requested);
    if (!requested_id)
        return 28;
    NavigationSemanticLayer generated;
    generated.area = c;
    generated.type = NavigationLayerTypeId::FromString("game.generated");
    const auto generated_id = n.AddLayer(generated);
    if (!generated_id || generated_id.Value().value.Low() != requested_id.Value().value.Low() + 1)
        return 29;

    if (!n.RemoveLink(link.id))
        return 30;
    if (!n.FindLinksBetween(a, b).empty() || n.FindLink(link.id) != nullptr)
        return 31;

    NavigationSemanticLink destroyed;
    destroyed.id = NavigationLinkId::FromString("destroyed");
    destroyed.from_area = a;
    destroyed.to_area = b;
    destroyed.type = NavigationLinkTypeId::FromString("game.bridge");
    destroyed.state = LinkState::Destroyed;
    if (!n.AddOrUpdateLink(destroyed))
        return 32;

    const auto snap = n.CaptureSnapshot();
    NavigationSemanticsService restored;
    if (!restored.RegisterDomain(d))
        return 33;
    if (!restored.RegisterRule(r))
        return 34;
    restored.Freeze();
    if (!restored.RestoreSnapshot(snap))
        return 35;
    if (restored.CanUseLink(actor, destroyed.id, {}, now).decision != NavigationDecisionKind::Deny)
        return 36;
    if (restored.FindLayersInArea(c).empty())
        return 37;

    auto invalid_generator = snap;
    invalid_generator.layer_ids.scope ^= 0x55ULL;
    const auto previous_revision = restored.CurrentRevision();
    const auto previous_layers = restored.FindLayersInArea(c).size();
    if (restored.RestoreSnapshot(std::move(invalid_generator)))
        return 38;
    if (restored.CurrentRevision() != previous_revision || restored.FindLayersInArea(c).size() != previous_layers)
        return 39;
    if (!restored.ReadChangesSince(restored.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required)
        return 40;
    NavigationSemanticsService empty_journal;
    if (!empty_journal.ReadChangesSince(empty_journal.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required)
        return 47;

    auto navigation_journal_seed = restored.CaptureSnapshot();
    navigation_journal_seed.journal.clear();
    navigation_journal_seed.next_change_sequence = std::numeric_limits<std::uint64_t>::max();
    if (!restored.RestoreSnapshot(navigation_journal_seed))
        return 41;
    if (!restored.SetLinkState(destroyed.id, LinkState::Open) ||
        !restored.SetLinkState(destroyed.id, LinkState::Destroyed))
        return 42;
    const auto navigation_exhausted = restored.CaptureSnapshot();
    if (navigation_exhausted.next_change_sequence != 0 || navigation_exhausted.journal.size() != 1 ||
        navigation_exhausted.journal.front().sequence != std::numeric_limits<std::uint64_t>::max())
        return 43;
    if (!restored.ReadChangesSince(restored.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required)
        return 44;
    NavigationSemanticsService navigation_exhausted_restore;
    if (!navigation_exhausted_restore.RegisterDomain(d) || !navigation_exhausted_restore.RegisterRule(r))
        return 45;
    navigation_exhausted_restore.Freeze();
    if (!navigation_exhausted_restore.RestoreSnapshot(navigation_exhausted))
        return 46;

    return 0;
}
