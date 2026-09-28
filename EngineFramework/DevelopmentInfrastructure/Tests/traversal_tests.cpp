#include "../../GameplayWorldStateOwners/Traversal/src/traversal_test_seam.h"
#include "Epidemic/GameFramework/Traversal/traversal.h"

#include <algorithm>
#include <limits>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::traversal;

namespace
{
template <class T, class Pred>
bool SameVector(const std::vector<T>& a, const std::vector<T>& b, Pred pred)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), pred);
}

bool SameState(const TraversalState& a, const TraversalState& b)
{
    return a.subject == b.subject && a.current_mode == b.current_mode && a.profile == b.profile &&
           a.active_session == b.active_session && a.revision == b.revision;
}

bool SameSession(const TraversalSession& a, const TraversalSession& b)
{
    return a.id == b.id && a.subject == b.subject && a.mode == b.mode && a.route == b.route &&
           a.state == b.state && a.started_at == b.started_at && a.revision == b.revision;
}

bool SameRoute(const TraversalRoute& a, const TraversalRoute& b)
{
    return a.id == b.id && a.subject == b.subject && a.from_area == b.from_area && a.to_area == b.to_area &&
           a.mode == b.mode && a.lifetime == b.lifetime && a.source == b.source && a.payload == b.payload &&
           a.revision == b.revision;
}

bool SameGrant(const TraversalCapabilityGrant& a, const TraversalCapabilityGrant& b)
{
    return a.id == b.id && a.subject == b.subject && a.capability == b.capability && a.source == b.source &&
           a.parameter_micro == b.parameter_micro && a.expires_at == b.expires_at && a.persistent == b.persistent &&
           a.revision == b.revision;
}

bool SameBinding(const TraversalCarrierBinding& a, const TraversalCarrierBinding& b)
{
    return a.passenger == b.passenger && a.carrier == b.carrier && a.carrier_mode == b.carrier_mode &&
           a.previous_mode == b.previous_mode && a.role == b.role && a.revision == b.revision;
}

bool SameChange(const TraversalChange& a, const TraversalChange& b)
{
    return a.sequence == b.sequence && a.kind == b.kind && a.subject == b.subject && a.mode == b.mode &&
           a.session == b.session && a.carrier == b.carrier && a.reason == b.reason && a.context == b.context &&
           a.revision == b.revision && a.capability == b.capability && a.capability_grant == b.capability_grant;
}

bool SameSnapshot(const TraversalSnapshot& a, const TraversalSnapshot& b)
{
    return SameVector(a.states, b.states, SameState) && SameVector(a.sessions, b.sessions, SameSession) &&
           SameVector(a.routes, b.routes, SameRoute) &&
           SameVector(a.capability_grants, b.capability_grants, SameGrant) &&
           SameVector(a.carrier_bindings, b.carrier_bindings, SameBinding) &&
           a.session_ids.scope == b.session_ids.scope && a.session_ids.next == b.session_ids.next &&
           a.route_ids.scope == b.route_ids.scope && a.route_ids.next == b.route_ids.next &&
           a.capability_grant_ids.scope == b.capability_grant_ids.scope &&
           a.capability_grant_ids.next == b.capability_grant_ids.next && a.revision == b.revision &&
           SameVector(a.journal, b.journal, SameChange) && a.next_change_sequence == b.next_change_sequence &&
           a.change_epoch == b.change_epoch;
}

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

    if (!restored.ReadChangesSince(restored.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required)
        return 38;
    auto traversal_journal_seed = restored.CaptureSnapshot();
    traversal_journal_seed.journal.clear();
    traversal_journal_seed.next_change_sequence = std::numeric_limits<std::uint64_t>::max();
    if (!restored.RestoreSnapshot(traversal_journal_seed))
        return 39;
    if (!restored.AssignProfile(actor, profile) || !restored.AssignProfile(actor, profile))
        return 40;
    const auto traversal_exhausted = restored.CaptureSnapshot();
    if (traversal_exhausted.next_change_sequence != 0 || traversal_exhausted.journal.size() != 1 ||
        traversal_exhausted.journal.front().sequence != std::numeric_limits<std::uint64_t>::max())
        return 41;
    if (!restored.ReadChangesSince(restored.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required)
        return 42;
    TraversalService traversal_exhausted_restore = BuildService(walk, swim, ride, swim_cap, ride_cap, profile);
    if (!traversal_exhausted_restore.RestoreSnapshot(traversal_exhausted))
        return 43;

    if (!restored.RemoveState(actor))
        return 36;
    if (restored.FindState(actor))
        return 37;

    TraversalService empty_journal = BuildService(walk, swim, ride, swim_cap, ride_cap, profile);
    if (!empty_journal.ReadChangesSince(empty_journal.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required)
        return 44;

    // Goal 4 G4-INFRA-001 / G4-TRAV-002: every named failure seam starts from a fresh fixture and preserves the full snapshot.
    const auto seam_actor = Ref("seam-actor");
    const auto seam_carrier = Ref("seam-carrier");
    {
        auto service = BuildService(walk, swim, ride, swim_cap, ride_cap, profile);
        const auto before = service.CaptureSnapshot();
        test_seam::FailNext(test_seam::FaultPoint::AssignProfileBeforePublish);
        if (service.AssignProfile(seam_actor, profile) || !SameSnapshot(before, service.CaptureSnapshot()))
            return 949;
    }
    {
        auto service = BuildService(walk, swim, ride, swim_cap, ride_cap, profile);
        if (!service.AssignProfile(seam_actor, profile))
            return 950;
        TraversalCapabilityGrant seam_grant;
        seam_grant.subject = seam_actor;
        seam_grant.capability = swim_cap;
        seam_grant.parameter_micro = 3'000'000;
        const auto before = service.CaptureSnapshot();
        test_seam::FailNext(test_seam::FaultPoint::GrantCapabilityBeforePublish);
        if (service.GrantCapability(seam_grant) || !SameSnapshot(before, service.CaptureSnapshot()))
            return 951;
    }
    {
        auto service = BuildService(walk, swim, ride, swim_cap, ride_cap, profile);
        if (!service.AssignProfile(seam_actor, profile))
            return 952;
        TraversalRoute seam_route;
        seam_route.subject = seam_actor;
        seam_route.mode = walk;
        seam_route.lifetime = TraversalRouteLifetime::Persistent;
        const auto before = service.CaptureSnapshot();
        test_seam::FailNext(test_seam::FaultPoint::RegisterRouteBeforePublish);
        if (service.RegisterRoute(seam_route) || !SameSnapshot(before, service.CaptureSnapshot()))
            return 953;
    }
    {
        auto service = BuildService(walk, swim, ride, swim_cap, ride_cap, profile);
        if (!service.AssignProfile(seam_actor, profile))
            return 954;
        const auto before = service.CaptureSnapshot();
        test_seam::FailNext(test_seam::FaultPoint::StartSessionBeforePublish);
        if (service.StartSession(seam_actor, walk, {}, {}, {}) || !SameSnapshot(before, service.CaptureSnapshot()))
            return 955;
    }
    {
        auto service = BuildService(walk, swim, ride, swim_cap, ride_cap, profile);
        if (!service.AssignProfile(seam_actor, profile))
            return 956;
        const auto before = service.CaptureSnapshot();
        test_seam::FailNext(test_seam::FaultPoint::BoardCarrierBeforePublish);
        if (service.BoardCarrier(seam_actor, seam_carrier, ride, TraversalCarrierRoleId::FromString("seam.role")) ||
            !SameSnapshot(before, service.CaptureSnapshot()))
            return 975;
    }
    {
        auto seeded = BuildService(walk, swim, ride, swim_cap, ride_cap, profile);
        if (!seeded.AssignProfile(seam_actor, profile))
            return 976;
        TraversalRoute seam_route;
        seam_route.subject = seam_actor;
        seam_route.mode = walk;
        seam_route.lifetime = TraversalRouteLifetime::Persistent;
        if (!seeded.RegisterRoute(seam_route))
            return 977;
        const auto restore_seed = seeded.CaptureSnapshot();
        for (const auto point : {test_seam::FaultPoint::RestoreCandidateBuild, test_seam::FaultPoint::RestoreBeforeCommit})
        {
            auto service = BuildService(walk, swim, ride, swim_cap, ride_cap, profile);
            const auto before = service.CaptureSnapshot();
            auto target = restore_seed;
            test_seam::FailNext(point);
            if (service.RestoreSnapshot(std::move(target)) || !SameSnapshot(before, service.CaptureSnapshot()))
                return 978;
        }
    }

    // Journal allocation has an explicit recovery policy: accepted state remains authoritative and the cursor epoch rotates.
    {
        auto service = BuildService(walk, swim, ride, swim_cap, ride_cap, profile);
        const auto journal_before = service.CaptureSnapshot();
        const auto journal_actor = Ref("journal-actor");
        test_seam::FailNext(test_seam::FaultPoint::JournalAppend);
        if (!service.AssignProfile(journal_actor, profile))
            return 973;
        const auto journal_after = service.CaptureSnapshot();
        if (!service.FindState(journal_actor) || !journal_after.journal.empty() ||
            journal_after.change_epoch == journal_before.change_epoch || journal_after.next_change_sequence != 1)
            return 974;
    }

    // G4-TRAV-001: every named state-changing family rejects max revision before mutation.
    TraversalService guard = BuildService(walk, swim, ride, swim_cap, ride_cap, profile);
    const auto guard_actor = Ref("guard-actor");
    if (!guard.AssignProfile(guard_actor, profile))
        return 957;
    TraversalCapabilityGrant guard_grant;
    guard_grant.subject = guard_actor;
    guard_grant.capability = swim_cap;
    guard_grant.parameter_micro = 3'000'000;
    auto guard_grant_id = guard.GrantCapability(guard_grant);
    if (!guard_grant_id)
        return 958;
    TraversalRoute guard_route;
    guard_route.subject = guard_actor;
    guard_route.mode = walk;
    guard_route.lifetime = TraversalRouteLifetime::Persistent;
    auto guard_route_id = guard.RegisterRoute(guard_route);
    if (!guard_route_id)
        return 959;
    auto max_base = guard.CaptureSnapshot();
    max_base.revision.value = std::numeric_limits<std::uint64_t>::max();

    TraversalService max_guard = BuildService(walk, swim, ride, swim_cap, ride_cap, profile);
    if (!max_guard.RestoreSnapshot(max_base))
        return 960;
    const auto stable = max_guard.CaptureSnapshot();
    TraversalCapabilityGrant extra = guard_grant;
    extra.id = {};
    TraversalRoute extra_route = guard_route;
    extra_route.id = {};
    extra_route.source = Ref("guard-route-extra");
    if (max_guard.GrantCapability(extra) || max_guard.RevokeCapability(guard_grant_id.Value()) ||
        max_guard.ChangeMode({guard_actor, swim, {}}) || max_guard.RegisterRoute(extra_route) ||
        max_guard.RemoveRoute(guard_route_id.Value()) || max_guard.StartSession(guard_actor, walk, {}, {}, {}) ||
        max_guard.BoardCarrier(guard_actor, Ref("guard-carrier"), ride, TraversalCarrierRoleId::FromString("guard.role")))
        return 961;
    const auto stable_after = max_guard.CaptureSnapshot();
    if (stable_after.revision != stable.revision || stable_after.states.size() != stable.states.size() ||
        stable_after.routes.size() != stable.routes.size() ||
        stable_after.capability_grants.size() != stable.capability_grants.size())
        return 962;

    // Active and suspended session transitions also reject at max revision without breaking state cross-links.
    TraversalService active_guard = BuildService(walk, swim, ride, swim_cap, ride_cap, profile);
    if (!active_guard.AssignProfile(guard_actor, profile))
        return 963;
    auto active_session = active_guard.StartSession(guard_actor, walk, {}, {}, {});
    if (!active_session)
        return 964;
    auto active_max = active_guard.CaptureSnapshot();
    active_max.revision.value = std::numeric_limits<std::uint64_t>::max();
    TraversalService active_max_guard = BuildService(walk, swim, ride, swim_cap, ride_cap, profile);
    if (!active_max_guard.RestoreSnapshot(active_max) || active_max_guard.SuspendSession(active_session.Value()) ||
        active_max_guard.CompleteSession(active_session.Value()))
        return 965;
    if (!active_max_guard.FindSession(active_session.Value()) ||
        !active_max_guard.FindState(guard_actor)->active_session)
        return 966;

    TraversalService suspended_guard = BuildService(walk, swim, ride, swim_cap, ride_cap, profile);
    if (!suspended_guard.AssignProfile(guard_actor, profile))
        return 967;
    auto suspended_session = suspended_guard.StartSession(guard_actor, walk, {}, {}, {});
    if (!suspended_session || !suspended_guard.SuspendSession(suspended_session.Value()))
        return 968;
    auto suspended_max = suspended_guard.CaptureSnapshot();
    suspended_max.revision.value = std::numeric_limits<std::uint64_t>::max();
    TraversalService suspended_max_guard = BuildService(walk, swim, ride, swim_cap, ride_cap, profile);
    if (!suspended_max_guard.RestoreSnapshot(suspended_max) || suspended_max_guard.ResumeSession(suspended_session.Value()))
        return 969;

    // Disembark is destructive only after revision preflight.
    TraversalService carrier_guard = BuildService(walk, swim, ride, swim_cap, ride_cap, profile);
    if (!carrier_guard.AssignProfile(guard_actor, profile) ||
        !carrier_guard.BoardCarrier(guard_actor, Ref("guard-carrier"), ride, TraversalCarrierRoleId::FromString("guard.role")))
        return 970;
    auto carrier_max = carrier_guard.CaptureSnapshot();
    carrier_max.revision.value = std::numeric_limits<std::uint64_t>::max();
    TraversalService carrier_max_guard = BuildService(walk, swim, ride, swim_cap, ride_cap, profile);
    if (!carrier_max_guard.RestoreSnapshot(carrier_max) || carrier_max_guard.Disembark(guard_actor))
        return 971;
    if (carrier_max_guard.FindCarrierPassengers(Ref("guard-carrier")).size() != 1)
        return 972;

    return 0;
}
