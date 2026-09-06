#include "Epidemic/GameFramework/Traversal/traversal.h"

#include <limits>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::traversal;

namespace
{
GameplayObjectRef Ref(const char* name)
{
    return {GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString(name)};
}

TraversalService BuildService(TraversalModeId walk, TraversalModeId swim, TraversalModeId ride,
                              TraversalCapabilityId swim_cap, TraversalCapabilityId ride_cap,
                              TraversalProfileId profile)
{
    TraversalService s;
    TraversalModeDefinition w;
    w.id = walk;
    w.canonical_name = "game.walk";
    if (!s.RegisterMode(w))
        return s;

    TraversalModeDefinition sw;
    sw.id = swim;
    sw.canonical_name = "game.swim";
    sw.required_capability = swim_cap;
    sw.required_capability_parameter_micro = 2'000'000;
    if (!s.RegisterMode(sw))
        return s;

    TraversalModeDefinition r;
    r.id = ride;
    r.canonical_name = "game.ride";
    r.required_capability = ride_cap;
    if (!s.RegisterMode(r))
        return s;

    TraversalProfile p;
    p.id = profile;
    p.canonical_name = "game.human";
    p.default_mode = walk;
    p.allowed_modes = {walk, swim, ride};
    p.capabilities = {TraversalCapabilityValue{swim_cap, 1'000'000}, TraversalCapabilityValue{ride_cap, 1'000'000}};
    if (!s.RegisterProfile(p))
        return s;
    s.Freeze();
    return s;
}
}

int main()
{
    const auto walk = TraversalModeId::FromString("game.walk");
    const auto swim = TraversalModeId::FromString("game.swim");
    const auto ride = TraversalModeId::FromString("game.ride");
    const auto swim_cap = TraversalCapabilityId::FromString("game.can_swim");
    const auto ride_cap = TraversalCapabilityId::FromString("game.can_ride");
    const auto profile = TraversalProfileId::FromString("game.human");
    const auto actor = Ref("actor");
    const auto horse = Ref("horse");
    const auto route_source = Ref("route_source");

    TraversalService s = BuildService(walk, swim, ride, swim_cap, ride_cap, profile);
    if (!s.IsFrozen())
        return 1;
    if (!s.AssignProfile(actor, profile))
        return 2;

    if (s.CanUseMode(actor, swim).kind != TraversalResultKind::Rejected)
        return 3;
    TraversalCapabilityGrant strong_swim;
    strong_swim.subject = actor;
    strong_swim.capability = swim_cap;
    strong_swim.parameter_micro = 3'000'000;
    strong_swim.source = Ref("spell" );
    strong_swim.persistent = false;
    auto grant = s.GrantCapability(strong_swim);
    if (!grant || s.CanUseMode(actor, swim).kind != TraversalResultKind::Accepted)
        return 4;
    if (s.EffectiveCapabilityParameter(actor, swim_cap) != 3'000'000)
        return 5;
    if (!s.RevokeCapability(grant.Value()) || s.CanUseMode(actor, swim).kind != TraversalResultKind::Rejected)
        return 6;

    strong_swim.persistent = true;
    grant = s.GrantCapability(strong_swim);
    if (!grant)
        return 7;
    auto changed = s.ChangeMode({actor, swim, {}});
    if (!changed || changed.Value().kind != TraversalResultKind::Accepted)
        return 8;

    const auto role = TraversalCarrierRoleId::FromString("game.rider");
    if (!s.BoardCarrier(actor, horse, ride, role))
        return 9;
    if (s.FindCarrierPassengers(horse).size() != 1)
        return 10;
    if (!s.Disembark(actor))
        return 11;
    if (!s.FindState(actor) || s.FindState(actor)->current_mode != swim)
        return 12;

    TraversalRoute persistent_route;
    persistent_route.subject = actor;
    persistent_route.mode = swim;
    persistent_route.source = route_source;
    persistent_route.lifetime = TraversalRouteLifetime::Persistent;
    auto route = s.RegisterRoute(persistent_route);
    if (!route || !s.FindRoute(route.Value()))
        return 13;

    TraversalRoute transient_route = persistent_route;
    transient_route.id = {};
    transient_route.lifetime = TraversalRouteLifetime::Transient;
    auto transient = s.RegisterRoute(transient_route);
    if (!transient || !s.FindRoute(transient.Value()))
        return 14;

    auto session = s.StartSession(actor, swim, route.Value(), {}, {});
    if (!session)
        return 15;
    if (s.RemoveRoute(route.Value()))
        return 16;
    if (!s.SuspendSession(session.Value()) || !s.ResumeSession(session.Value()))
        return 17;
    if (!s.CompleteSession(session.Value()))
        return 18;
    if (s.FindSession(session.Value()) || (s.FindState(actor) && s.FindState(actor)->active_session))
        return 19;
    if (s.GetDiagnostics().active_sessions != 0)
        return 20;
    if (!s.RemoveRoute(route.Value()) || s.FindRoute(route.Value()))
        return 21;
    if (s.RemoveRoutesBySource(route_source) != 1 || s.FindRoute(transient.Value()))
        return 22;

    TraversalRoute snapshot_route = persistent_route;
    snapshot_route.id = {};
    snapshot_route.lifetime = TraversalRouteLifetime::Persistent;
    route = s.RegisterRoute(snapshot_route);
    if (!route)
        return 23;
    TraversalRoute snapshot_transient = persistent_route;
    snapshot_transient.id = {};
    snapshot_transient.lifetime = TraversalRouteLifetime::Session;
    transient = s.RegisterRoute(snapshot_transient);
    if (!transient)
        return 24;

    auto snap = s.CaptureSnapshot();
    bool saw_persistent = false;
    bool saw_transient = false;
    for (const auto& r : snap.routes)
    {
        if (r.id == route.Value())
            saw_persistent = true;
        if (r.id == transient.Value())
            saw_transient = true;
    }
    if (!saw_persistent || saw_transient)
        return 25;

    TraversalService restored = BuildService(walk, swim, ride, swim_cap, ride_cap, profile);
    if (!restored.RestoreSnapshot(snap))
        return 26;
    if (!restored.FindRoute(route.Value()) || restored.FindRoute(transient.Value()))
        return 27;

    auto fail_session = restored.StartSession(actor, swim, {}, {}, {});
    if (!fail_session)
        return 28;
    const auto fail_reason = TraversalReasonId::FromString("runtime.path_failed");
    if (!restored.FailSession(fail_session.Value(), fail_reason))
        return 29;
    if (restored.FindSession(fail_session.Value()) || restored.GetDiagnostics().active_sessions != 0)
        return 30;

    auto corrupt = restored.CaptureSnapshot();
    corrupt.route_ids.next = 1;
    TraversalService should_keep_state = BuildService(walk, swim, ride, swim_cap, ride_cap, profile);
    if (!should_keep_state.AssignProfile(Ref("other"), profile))
        return 31;
    if (should_keep_state.RestoreSnapshot(corrupt))
        return 32;
    if (!should_keep_state.FindState(Ref("other")))
        return 33;

    const auto route_scope = GameplayObjectId::FromString("framework.traversal.route").High();
    TraversalRoute max_route = persistent_route;
    max_route.id = {GameplayObjectId::FromRaw(route_scope, std::numeric_limits<std::uint64_t>::max())};
    if (!s.RegisterRoute(max_route))
        return 34;
    TraversalRoute exhausted_route = persistent_route;
    exhausted_route.id = {};
    if (s.RegisterRoute(exhausted_route))
        return 35;

    if (!restored.RemoveState(actor))
        return 36;
    if (restored.FindState(actor))
        return 37;

    return 0;
}
