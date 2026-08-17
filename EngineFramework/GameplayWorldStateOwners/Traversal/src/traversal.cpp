#include "Epidemic/GameFramework/Traversal/traversal.h"

#include <iterator>
#include <utility>

namespace epidemic::gameplay::traversal
{
namespace
{
foundation::Error Error(std::string_view c, std::string_view m)
{
    return foundation::Error::Create(c, m);
}
} // namespace
TraversalService::TraversalService() : session_ids_(TypeId::FromString("framework.traversal.session").Raw())
{
}
foundation::Result<void> TraversalService::RegisterMode(TraversalModeDefinition d)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "traversal registry frozen"));
    if (!d.id.IsValid() || d.canonical_name.empty() || modes_.contains(d.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.traversal.invalid_mode", "invalid or duplicate traversal mode"));
    modes_.emplace(d.id, std::move(d));
    return foundation::Result<void>::Success();
}
foundation::Result<void> TraversalService::RegisterProfile(TraversalProfile p)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "traversal registry frozen"));
    if (!p.id.IsValid() || p.canonical_name.empty() || !p.default_mode.IsValid() || profiles_.contains(p.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.traversal.invalid_profile", "invalid or duplicate traversal profile"));
    if (!modes_.contains(p.default_mode))
        return foundation::Result<void>::Failure(
            Error("gameplay.traversal.unknown_mode", "default mode is not registered"));
    if (p.allowed_modes.empty())
        p.allowed_modes.push_back(p.default_mode);
    for (const auto m : p.allowed_modes)
        if (!modes_.contains(m))
            return foundation::Result<void>::Failure(
                Error("gameplay.traversal.unknown_mode", "allowed mode is not registered"));
    std::sort(p.allowed_modes.begin(), p.allowed_modes.end());
    p.allowed_modes.erase(std::unique(p.allowed_modes.begin(), p.allowed_modes.end()), p.allowed_modes.end());
    std::sort(p.capabilities.begin(), p.capabilities.end());
    p.capabilities.erase(std::unique(p.capabilities.begin(), p.capabilities.end()), p.capabilities.end());
    profiles_.emplace(p.id, std::move(p));
    return foundation::Result<void>::Success();
}
const TraversalModeDefinition *TraversalService::FindMode(TraversalModeId id) const noexcept
{
    auto it = modes_.find(id);
    return it == modes_.end() ? nullptr : &it->second;
}
const TraversalProfile *TraversalService::FindProfile(TraversalProfileId id) const noexcept
{
    auto it = profiles_.find(id);
    return it == profiles_.end() ? nullptr : &it->second;
}
foundation::Result<void> TraversalService::AssignProfile(GameplayObjectRef subject, TraversalProfileId profile,
                                                         GameplayContext context)
{
    auto it = profiles_.find(profile);
    if (!subject.IsValid() || it == profiles_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.traversal.invalid_profile_assignment", "invalid profile assignment"));
    Bump();
    auto &s = states_[subject];
    s.subject = subject;
    s.profile = profile;
    if (!s.current_mode.IsValid())
        s.current_mode = it->second.default_mode;
    s.revision = revision_;
    Record({0, TraversalChangeKind::ProfileAssigned, subject, s.current_mode, {}, {}, {}, context, revision_});
    return foundation::Result<void>::Success();
}
TraversalResult TraversalService::CanUseMode(GameplayObjectRef subject, TraversalModeId mode) const
{
    const auto *s = FindState(subject);
    if (!s || !mode.IsValid())
        return {TraversalResultKind::Rejected, TraversalReasonId::FromString("traversal.no_state"), revision_};
    auto p = profiles_.find(s->profile);
    auto m = modes_.find(mode);
    if (p == profiles_.end() || m == modes_.end())
        return {TraversalResultKind::Rejected, TraversalReasonId::FromString("traversal.unknown_mode"), revision_};
    if (!ProfileAllows(p->second, mode))
        return {TraversalResultKind::Rejected, TraversalReasonId::FromString("traversal.mode_not_allowed"), revision_};
    if (m->second.required_capability.IsValid() && !ProfileHasCapability(p->second, m->second.required_capability))
        return {TraversalResultKind::Rejected, TraversalReasonId::FromString("traversal.capability_missing"),
                revision_};
    return {s->current_mode == mode ? TraversalResultKind::NoOp : TraversalResultKind::Accepted, {}, revision_};
}
foundation::Result<TraversalResult> TraversalService::ChangeMode(ChangeTraversalModeRequest r)
{
    auto res = CanUseMode(r.subject, r.target_mode);
    if (res.kind == TraversalResultKind::Rejected)
    {
        ++rejected_mode_changes_;
        Record({0,
                TraversalChangeKind::RequestRejected,
                r.subject,
                r.target_mode,
                {},
                {},
                res.reason,
                r.context,
                revision_});
        return foundation::Result<TraversalResult>::Success(res);
    }
    if (res.kind == TraversalResultKind::NoOp)
        return foundation::Result<TraversalResult>::Success(res);
    auto *st = FindMutableState(r.subject);
    if (!st)
        return foundation::Result<TraversalResult>::Failure(
            Error("gameplay.traversal.state_missing", "traversal state missing"));
    Bump();
    st->current_mode = r.target_mode;
    st->revision = revision_;
    ++mode_changes_;
    Record({0, TraversalChangeKind::ModeChanged, r.subject, r.target_mode, {}, {}, {}, r.context, revision_});
    res.revision = revision_;
    return foundation::Result<TraversalResult>::Success(res);
}
foundation::Result<TraversalRouteId> TraversalService::RegisterRoute(TraversalRoute route)
{
    if (!route.id.IsValid())
        route.id = TraversalRouteId::FromString("route." + std::to_string(routes_.size() + 1));
    if (!route.id.IsValid() || !route.subject.IsValid() || !route.mode.IsValid() || routes_.contains(route.id))
        return foundation::Result<TraversalRouteId>::Failure(
            Error("gameplay.traversal.invalid_route", "invalid traversal route"));
    Bump();
    route.revision = revision_;
    routes_.emplace(route.id, route);
    return foundation::Result<TraversalRouteId>::Success(route.id);
}
foundation::Result<TraversalSessionId> TraversalService::StartSession(GameplayObjectRef subject, TraversalModeId mode,
                                                                      TraversalRouteId route,
                                                                      GameplayTimePoint started_at,
                                                                      GameplayContext context)
{
    auto can = CanUseMode(subject, mode);
    if (can.kind == TraversalResultKind::Rejected)
        return foundation::Result<TraversalSessionId>::Failure(
            Error("gameplay.traversal.mode_rejected", "cannot start traversal session"));
    auto *st = FindMutableState(subject);
    if (!st)
        return foundation::Result<TraversalSessionId>::Failure(
            Error("gameplay.traversal.state_missing", "traversal state missing"));
    auto id = TraversalSessionId{session_ids_.Next()};
    Bump();
    TraversalSession s{id, subject, mode, route, TraversalSessionState::Active, started_at, revision_};
    sessions_.emplace(id, s);
    st->active_session = id;
    st->current_mode = mode;
    st->revision = revision_;
    Record({0, TraversalChangeKind::SessionStarted, subject, mode, id, {}, {}, context, revision_});
    return foundation::Result<TraversalSessionId>::Success(id);
}
foundation::Result<void> TraversalService::CompleteSession(TraversalSessionId id, GameplayContext context)
{
    auto *s = FindMutableSession(id);
    if (!s)
        return foundation::Result<void>::Failure(Error("gameplay.traversal.session_missing", "session missing"));
    Bump();
    s->state = TraversalSessionState::Completed;
    s->revision = revision_;
    if (auto *st = FindMutableState(s->subject))
    {
        st->active_session.reset();
        st->revision = revision_;
    }
    Record({0, TraversalChangeKind::SessionCompleted, s->subject, s->mode, id, {}, {}, context, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> TraversalService::CancelSession(TraversalSessionId id, TraversalReasonId reason,
                                                         GameplayContext context)
{
    auto *s = FindMutableSession(id);
    if (!s)
        return foundation::Result<void>::Failure(Error("gameplay.traversal.session_missing", "session missing"));
    Bump();
    s->state = TraversalSessionState::Cancelled;
    s->revision = revision_;
    if (auto *st = FindMutableState(s->subject))
    {
        st->active_session.reset();
        st->revision = revision_;
    }
    Record({0, TraversalChangeKind::SessionCancelled, s->subject, s->mode, id, {}, reason, context, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> TraversalService::BoardCarrier(GameplayObjectRef passenger, GameplayObjectRef carrier,
                                                        TraversalModeId mode, TraversalCarrierRoleId role,
                                                        GameplayContext context)
{
    if (!passenger.IsValid() || !carrier.IsValid() || !mode.IsValid() || !role.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.traversal.invalid_carrier_binding", "invalid carrier binding"));
    Bump();
    carrier_by_passenger_[passenger] = {passenger, carrier, mode, role, revision_};
    if (auto *st = FindMutableState(passenger))
    {
        st->current_mode = mode;
        st->revision = revision_;
    }
    ++boarding_ops_;
    Record({0, TraversalChangeKind::CarrierBoarded, passenger, mode, {}, carrier, {}, context, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> TraversalService::Disembark(GameplayObjectRef passenger, GameplayContext context)
{
    auto it = carrier_by_passenger_.find(passenger);
    if (it == carrier_by_passenger_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.traversal.binding_missing", "carrier binding missing"));
    auto mode = it->second.carrier_mode;
    auto carrier = it->second.carrier;
    carrier_by_passenger_.erase(it);
    Bump();
    Record({0, TraversalChangeKind::CarrierDisembarked, passenger, mode, {}, carrier, {}, context, revision_});
    return foundation::Result<void>::Success();
}
const TraversalState *TraversalService::FindState(GameplayObjectRef subject) const noexcept
{
    auto it = states_.find(subject);
    return it == states_.end() ? nullptr : &it->second;
}
TraversalState *TraversalService::FindMutableState(GameplayObjectRef subject) noexcept
{
    auto it = states_.find(subject);
    return it == states_.end() ? nullptr : &it->second;
}
const TraversalSession *TraversalService::FindSession(TraversalSessionId id) const noexcept
{
    auto it = sessions_.find(id);
    return it == sessions_.end() ? nullptr : &it->second;
}
TraversalSession *TraversalService::FindMutableSession(TraversalSessionId id) noexcept
{
    auto it = sessions_.find(id);
    return it == sessions_.end() ? nullptr : &it->second;
}
bool TraversalService::ProfileAllows(const TraversalProfile &p, TraversalModeId mode) const noexcept
{
    return std::binary_search(p.allowed_modes.begin(), p.allowed_modes.end(), mode);
}
bool TraversalService::ProfileHasCapability(const TraversalProfile &p, TraversalCapabilityId cap) const noexcept
{
    return std::binary_search(p.capabilities.begin(), p.capabilities.end(), cap);
}
std::vector<TraversalState> TraversalService::FindSubjectsUsingMode(TraversalModeId mode) const
{
    std::vector<TraversalState> out;
    for (const auto &[ref, s] : states_)
    {
        (void)ref;
        if (s.current_mode == mode)
            out.push_back(s);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.subject < b.subject; });
    return out;
}
std::vector<TraversalCarrierBinding> TraversalService::FindCarrierPassengers(GameplayObjectRef carrier) const
{
    std::vector<TraversalCarrierBinding> out;
    for (const auto &[p, b] : carrier_by_passenger_)
    {
        (void)p;
        if (b.carrier == carrier)
            out.push_back(b);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.passenger < b.passenger; });
    return out;
}
std::vector<TraversalChange> TraversalService::ChangesSince(std::uint64_t seq) const
{
    std::vector<TraversalChange> out;
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(out),
                 [seq](const auto &c) { return c.sequence > seq; });
    return out;
}
TraversalSnapshot TraversalService::CaptureSnapshot() const
{
    TraversalSnapshot s;
    for (const auto &[ref, st] : states_)
    {
        (void)ref;
        s.states.push_back(st);
    }
    for (const auto &[id, ss] : sessions_)
    {
        (void)id;
        if (ss.state == TraversalSessionState::Active || ss.state == TraversalSessionState::Suspended)
            s.sessions.push_back(ss);
    }
    for (const auto &[ref, b] : carrier_by_passenger_)
    {
        (void)ref;
        s.carrier_bindings.push_back(b);
    }
    std::sort(s.states.begin(), s.states.end(), [](auto &a, auto &b) { return a.subject < b.subject; });
    std::sort(s.sessions.begin(), s.sessions.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.carrier_bindings.begin(), s.carrier_bindings.end(),
              [](auto &a, auto &b) { return a.passenger < b.passenger; });
    s.session_ids = session_ids_.GetSnapshot();
    s.revision = revision_;
    return s;
}
foundation::Result<void> TraversalService::RestoreSnapshot(TraversalSnapshot s)
{
    states_.clear();
    sessions_.clear();
    carrier_by_passenger_.clear();
    for (const auto &st : s.states)
    {
        if (!st.subject.IsValid() || !profiles_.contains(st.profile) || !modes_.contains(st.current_mode))
            return foundation::Result<void>::Failure(
                Error("gameplay.traversal.restore_invalid", "invalid traversal state"));
        states_.emplace(st.subject, st);
    }
    for (const auto &ss : s.sessions)
    {
        if (!ss.id.IsValid() || !states_.contains(ss.subject) || !modes_.contains(ss.mode))
            return foundation::Result<void>::Failure(
                Error("gameplay.traversal.restore_invalid", "invalid traversal session"));
        sessions_.emplace(ss.id, ss);
    }
    for (const auto &b : s.carrier_bindings)
    {
        if (!b.passenger.IsValid() || !b.carrier.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.traversal.restore_invalid", "invalid carrier binding"));
        carrier_by_passenger_.emplace(b.passenger, b);
    }
    session_ids_.Restore(s.session_ids);
    revision_ = s.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    return foundation::Result<void>::Success();
}
TraversalDiagnostics TraversalService::GetDiagnostics() const noexcept
{
    TraversalDiagnostics d;
    d.profiles = profiles_.size();
    d.modes = modes_.size();
    d.states = states_.size();
    d.carrier_bindings = carrier_by_passenger_.size();
    d.mode_changes = mode_changes_;
    d.rejected_mode_changes = rejected_mode_changes_;
    d.boarding_ops = boarding_ops_;
    for (const auto &[id, s] : sessions_)
    {
        (void)id;
        if (s.state == TraversalSessionState::Active || s.state == TraversalSessionState::Suspended)
            ++d.active_sessions;
    }
    return d;
}
void TraversalService::Record(TraversalChange c)
{
    c.sequence = next_change_sequence_++;
    changes_.push_back(c);
}
} // namespace epidemic::gameplay::traversal
