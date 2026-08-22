#include "Epidemic/GameFramework/Traversal/traversal.h"

#include "Epidemic/Foundation/error.h"

#include <iterator>
#include <limits>
#include <unordered_set>
#include <utility>

namespace epidemic::gameplay::traversal
{
namespace
{
foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}
}

TraversalService::TraversalService()
    : session_ids_(TypeId::FromString("framework.traversal.session").Raw()),
      route_ids_(TypeId::FromString("framework.traversal.route").Raw()),
      capability_grant_ids_(TypeId::FromString("framework.traversal.capability_grant").Raw())
{
}

foundation::Result<void> TraversalService::RegisterMode(TraversalModeDefinition definition)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "traversal registry frozen"));
    if (definition.canonical_name.empty())
        return foundation::Result<void>::Failure(Error("gameplay.traversal.invalid_mode", "mode name is required"));
    const auto expected = TraversalModeId::FromString(definition.canonical_name);
    if (!definition.id.IsValid())
        definition.id = expected;
    if (definition.id != expected || modes_.contains(definition.id) || definition.base_speed_micro < 0 ||
        definition.acceleration_modifier_micro < 0 || definition.required_capability_parameter_micro < 0)
        return foundation::Result<void>::Failure(
            Error("gameplay.traversal.invalid_mode", "invalid or duplicate traversal mode"));
    modes_.emplace(definition.id, std::move(definition));
    return foundation::Result<void>::Success();
}

foundation::Result<void> TraversalService::RegisterProfile(TraversalProfile profile)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "traversal registry frozen"));
    if (profile.canonical_name.empty() || !profile.default_mode.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.traversal.invalid_profile", "invalid traversal profile"));
    const auto expected = TraversalProfileId::FromString(profile.canonical_name);
    if (!profile.id.IsValid())
        profile.id = expected;
    if (profile.id != expected || profiles_.contains(profile.id) || !modes_.contains(profile.default_mode))
        return foundation::Result<void>::Failure(
            Error("gameplay.traversal.invalid_profile", "invalid or duplicate traversal profile"));
    if (profile.allowed_modes.empty())
        profile.allowed_modes.push_back(profile.default_mode);
    for (const auto mode : profile.allowed_modes)
        if (!modes_.contains(mode))
            return foundation::Result<void>::Failure(Error("gameplay.traversal.unknown_mode", "allowed mode is not registered"));
    std::sort(profile.allowed_modes.begin(), profile.allowed_modes.end());
    profile.allowed_modes.erase(std::unique(profile.allowed_modes.begin(), profile.allowed_modes.end()), profile.allowed_modes.end());
    if (!std::binary_search(profile.allowed_modes.begin(), profile.allowed_modes.end(), profile.default_mode))
        return foundation::Result<void>::Failure(
            Error("gameplay.traversal.invalid_profile", "default mode must be included in allowed modes"));
    std::sort(profile.capabilities.begin(), profile.capabilities.end());
    profile.capabilities.erase(std::unique(profile.capabilities.begin(), profile.capabilities.end()), profile.capabilities.end());
    const auto& default_mode = modes_.at(profile.default_mode);
    if (default_mode.required_capability.IsValid() &&
        !std::binary_search(profile.capabilities.begin(), profile.capabilities.end(), default_mode.required_capability))
        return foundation::Result<void>::Failure(
            Error("gameplay.traversal.invalid_profile", "default mode must be supported by an intrinsic capability"));
    profiles_.emplace(profile.id, std::move(profile));
    return foundation::Result<void>::Success();
}

const TraversalModeDefinition* TraversalService::FindMode(TraversalModeId id) const noexcept
{
    const auto it = modes_.find(id);
    return it == modes_.end() ? nullptr : &it->second;
}

const TraversalProfile* TraversalService::FindProfile(TraversalProfileId id) const noexcept
{
    const auto it = profiles_.find(id);
    return it == profiles_.end() ? nullptr : &it->second;
}

foundation::Result<TraversalCapabilityGrantId> TraversalService::GrantCapability(TraversalCapabilityGrant grant,
                                                                                 GameplayContext context)
{
    if (!grant.subject.IsValid() || !grant.capability.IsValid() || grant.parameter_micro < 0 || !states_.contains(grant.subject))
        return foundation::Result<TraversalCapabilityGrantId>::Failure(
            Error("gameplay.traversal.capability_invalid", "capability grant is invalid"));
    if (!grant.id.IsValid())
        grant.id = {capability_grant_ids_.Next()};
    if (capability_grants_.contains(grant.id))
        return foundation::Result<TraversalCapabilityGrantId>::Failure(
            Error("gameplay.already_registered", "capability grant id already exists"));
    Bump();
    grant.revision = revision_;
    const auto id = grant.id;
    const auto subject = grant.subject;
    const auto capability = grant.capability;
    capability_grants_.emplace(id, std::move(grant));
    TraversalChange change{0, TraversalChangeKind::CapabilityGranted, subject, {}, {}, {}, {}, context, revision_};
    change.capability = capability;
    change.capability_grant = id;
    Record(std::move(change));
    return foundation::Result<TraversalCapabilityGrantId>::Success(id);
}

foundation::Result<void> TraversalService::RevokeCapability(TraversalCapabilityGrantId id, GameplayContext context)
{
    const auto it = capability_grants_.find(id);
    if (it == capability_grants_.end())
        return foundation::Result<void>::Success();
    const auto grant = it->second;
    capability_grants_.erase(it);
    Bump();
    TraversalChange change{0, TraversalChangeKind::CapabilityRevoked, grant.subject, {}, {}, {}, {}, context, revision_};
    change.capability = grant.capability;
    change.capability_grant = id;
    Record(std::move(change));
    return foundation::Result<void>::Success();
}

std::uint64_t TraversalService::RevokeCapabilitiesBySource(GameplayObjectRef subject, GameplayObjectRef source,
                                                           GameplayContext context)
{
    std::vector<TraversalCapabilityGrantId> ids;
    for (const auto& [id, grant] : capability_grants_)
        if (grant.subject == subject && grant.source == source)
            ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    for (const auto id : ids)
        (void)RevokeCapability(id, context);
    return ids.size();
}

std::uint64_t TraversalService::ExpireCapabilities(GameplayTimePoint now, GameplayContext context)
{
    std::vector<TraversalCapabilityGrantId> ids;
    for (const auto& [id, grant] : capability_grants_)
        if (grant.expires_at && grant.expires_at->ticks <= now.ticks)
            ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    for (const auto id : ids)
        (void)RevokeCapability(id, context);
    return ids.size();
}

bool TraversalService::HasCapability(GameplayObjectRef subject, TraversalCapabilityId capability,
                                     Fixed min_parameter_micro) const noexcept
{
    const auto* state = FindState(subject);
    if (!state || !capability.IsValid())
        return false;
    const auto profile = profiles_.find(state->profile);
    if (profile != profiles_.end() && ProfileHasCapability(profile->second, capability))
        return true;
    for (const auto& [id, grant] : capability_grants_)
    {
        (void)id;
        if (grant.subject == subject && grant.capability == capability && grant.parameter_micro >= min_parameter_micro)
            return true;
    }
    return false;
}

foundation::Result<void> TraversalService::AssignProfile(GameplayObjectRef subject, TraversalProfileId profile,
                                                         GameplayContext context)
{
    const auto definition = profiles_.find(profile);
    if (!subject.IsValid() || definition == profiles_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.traversal.invalid_profile_assignment", "invalid profile assignment"));
    auto existing = states_.find(subject);
    if (existing != states_.end() && existing->second.active_session)
        return foundation::Result<void>::Failure(
            Error("gameplay.traversal.session_active", "cannot replace traversal profile during an active session"));
    Bump();
    auto& state = states_[subject];
    state.subject = subject;
    state.profile = profile;
    if (!state.current_mode.IsValid() || !ProfileAllows(definition->second, state.current_mode))
        state.current_mode = definition->second.default_mode;
    state.revision = revision_;
    Record({0, TraversalChangeKind::ProfileAssigned, subject, state.current_mode, {}, {}, {}, context, revision_});
    return foundation::Result<void>::Success();
}

TraversalResult TraversalService::CanUseMode(GameplayObjectRef subject, TraversalModeId mode) const
{
    const auto* state = FindState(subject);
    if (!state || !mode.IsValid())
        return {TraversalResultKind::Rejected, TraversalReasonId::FromString("traversal.no_state"), revision_};
    const auto profile = profiles_.find(state->profile);
    const auto definition = modes_.find(mode);
    if (profile == profiles_.end() || definition == modes_.end())
        return {TraversalResultKind::Rejected, TraversalReasonId::FromString("traversal.unknown_mode"), revision_};
    if (!ProfileAllows(profile->second, mode))
        return {TraversalResultKind::Rejected, TraversalReasonId::FromString("traversal.mode_not_allowed"), revision_};
    if (definition->second.required_capability.IsValid() &&
        !HasCapability(subject, definition->second.required_capability, definition->second.required_capability_parameter_micro))
        return {TraversalResultKind::Rejected, TraversalReasonId::FromString("traversal.capability_missing"), revision_};
    if (definition->second.materialization_policy == TraversalMaterializationPolicy::RequiresMaterialized &&
        (materialization_ == nullptr || !materialization_->IsMaterialized(subject)))
        return {TraversalResultKind::Rejected, TraversalReasonId::FromString("traversal.materialization_required"), revision_};
    if (definition->second.materialization_policy == TraversalMaterializationPolicy::RequiresRuntimeProjection &&
        (materialization_ == nullptr || !materialization_->HasRuntimeProjection(subject)))
        return {TraversalResultKind::Rejected, TraversalReasonId::FromString("traversal.runtime_projection_required"), revision_};
    return {state->current_mode == mode ? TraversalResultKind::NoOp : TraversalResultKind::Accepted, {}, revision_};
}

foundation::Result<TraversalResult> TraversalService::ChangeMode(ChangeTraversalModeRequest request)
{
    const auto result = CanUseMode(request.subject, request.target_mode);
    if (result.kind == TraversalResultKind::Rejected)
    {
        ++rejected_mode_changes_;
        Record({0, TraversalChangeKind::RequestRejected, request.subject, request.target_mode, {}, {}, result.reason,
                request.context, revision_});
        return foundation::Result<TraversalResult>::Success(result);
    }
    if (result.kind == TraversalResultKind::NoOp)
        return foundation::Result<TraversalResult>::Success(result);
    auto* state = FindMutableState(request.subject);
    if (!state)
        return foundation::Result<TraversalResult>::Failure(
            Error("gameplay.traversal.state_missing", "traversal state missing"));
    if (state->active_session)
        return foundation::Result<TraversalResult>::Failure(
            Error("gameplay.traversal.session_active", "cannot change traversal mode outside an active session"));
    Bump();
    state->current_mode = request.target_mode;
    state->revision = revision_;
    ++mode_changes_;
    Record({0, TraversalChangeKind::ModeChanged, request.subject, request.target_mode, {}, {}, {}, request.context, revision_});
    auto committed = result;
    committed.revision = revision_;
    return foundation::Result<TraversalResult>::Success(committed);
}

foundation::Result<TraversalRouteId> TraversalService::RegisterRoute(TraversalRoute route)
{
    if (!route.subject.IsValid() || !route.mode.IsValid() || !modes_.contains(route.mode))
        return foundation::Result<TraversalRouteId>::Failure(
            Error("gameplay.traversal.invalid_route", "invalid traversal route"));
    if (route.id.IsValid() && routes_.contains(route.id))
        return foundation::Result<TraversalRouteId>::Failure(
            Error("gameplay.already_registered", "traversal route id already exists"));
    if (!route.id.IsValid())
        route.id = {route_ids_.Next()};
    Bump();
    route.revision = revision_;
    const auto id = route.id;
    routes_.emplace(id, std::move(route));
    return foundation::Result<TraversalRouteId>::Success(id);
}

bool TraversalService::IsLiveSession(TraversalSessionState state) const noexcept
{
    return state == TraversalSessionState::Preparing || state == TraversalSessionState::Active ||
           state == TraversalSessionState::Suspended;
}

foundation::Result<TraversalSessionId> TraversalService::StartSession(GameplayObjectRef subject, TraversalModeId mode,
                                                                      TraversalRouteId route,
                                                                      GameplayTimePoint started_at,
                                                                      GameplayContext context)
{
    const auto can = CanUseMode(subject, mode);
    if (can.kind == TraversalResultKind::Rejected)
        return foundation::Result<TraversalSessionId>::Failure(
            Error("gameplay.traversal.mode_rejected", "cannot start traversal session"));
    auto* state = FindMutableState(subject);
    if (!state)
        return foundation::Result<TraversalSessionId>::Failure(Error("gameplay.traversal.state_missing", "traversal state missing"));
    if (state->active_session)
    {
        const auto existing = sessions_.find(*state->active_session);
        if (existing != sessions_.end() && IsLiveSession(existing->second.state))
            return foundation::Result<TraversalSessionId>::Failure(
                Error("gameplay.traversal.session_active", "subject already has an active traversal session"));
        state->active_session.reset();
    }
    if (route.IsValid())
    {
        const auto route_it = routes_.find(route);
        if (route_it == routes_.end() || route_it->second.subject != subject || route_it->second.mode != mode)
            return foundation::Result<TraversalSessionId>::Failure(
                Error("gameplay.traversal.route_mismatch", "route does not belong to the requested subject and mode"));
    }
    const TraversalSessionId id{session_ids_.Next()};
    Bump();
    sessions_.emplace(id, TraversalSession{id, subject, mode, route, TraversalSessionState::Active, started_at, revision_});
    state->active_session = id;
    state->current_mode = mode;
    state->revision = revision_;
    Record({0, TraversalChangeKind::SessionStarted, subject, mode, id, {}, {}, context, revision_});
    return foundation::Result<TraversalSessionId>::Success(id);
}

foundation::Result<void> TraversalService::SuspendSession(TraversalSessionId id, GameplayContext context)
{
    auto* session = FindMutableSession(id);
    if (!session || session->state != TraversalSessionState::Active)
        return foundation::Result<void>::Failure(Error("gameplay.traversal.session_state", "only an active session can be suspended"));
    Bump();
    session->state = TraversalSessionState::Suspended;
    session->revision = revision_;
    Record({0, TraversalChangeKind::SessionSuspended, session->subject, session->mode, id, {}, {}, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> TraversalService::ResumeSession(TraversalSessionId id, GameplayContext context)
{
    auto* session = FindMutableSession(id);
    if (!session || session->state != TraversalSessionState::Suspended)
        return foundation::Result<void>::Failure(Error("gameplay.traversal.session_state", "only a suspended session can be resumed"));
    const auto can = CanUseMode(session->subject, session->mode);
    if (can.kind == TraversalResultKind::Rejected)
        return foundation::Result<void>::Failure(Error("gameplay.traversal.mode_rejected", "session mode is no longer available"));
    Bump();
    session->state = TraversalSessionState::Active;
    session->revision = revision_;
    Record({0, TraversalChangeKind::SessionResumed, session->subject, session->mode, id, {}, {}, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> TraversalService::CompleteSession(TraversalSessionId id, GameplayContext context)
{
    auto* session = FindMutableSession(id);
    if (!session || !IsLiveSession(session->state))
        return foundation::Result<void>::Failure(Error("gameplay.traversal.session_state", "session is missing or already terminal"));
    Bump();
    session->state = TraversalSessionState::Completed;
    session->revision = revision_;
    if (auto* state = FindMutableState(session->subject); state && state->active_session == id)
    {
        state->active_session.reset();
        state->revision = revision_;
    }
    Record({0, TraversalChangeKind::SessionCompleted, session->subject, session->mode, id, {}, {}, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> TraversalService::CancelSession(TraversalSessionId id, TraversalReasonId reason,
                                                         GameplayContext context)
{
    auto* session = FindMutableSession(id);
    if (!session || !IsLiveSession(session->state))
        return foundation::Result<void>::Failure(Error("gameplay.traversal.session_state", "session is missing or already terminal"));
    Bump();
    session->state = TraversalSessionState::Cancelled;
    session->revision = revision_;
    if (auto* state = FindMutableState(session->subject); state && state->active_session == id)
    {
        state->active_session.reset();
        state->revision = revision_;
    }
    Record({0, TraversalChangeKind::SessionCancelled, session->subject, session->mode, id, {}, reason, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> TraversalService::BoardCarrier(GameplayObjectRef passenger, GameplayObjectRef carrier,
                                                        TraversalModeId mode, TraversalCarrierRoleId role,
                                                        GameplayContext context)
{
    if (!passenger.IsValid() || !carrier.IsValid() || passenger == carrier || !mode.IsValid() || !role.IsValid() ||
        carrier_by_passenger_.contains(passenger))
        return foundation::Result<void>::Failure(Error("gameplay.traversal.invalid_carrier_binding", "invalid carrier binding"));
    auto* state = FindMutableState(passenger);
    if (!state || state->active_session)
        return foundation::Result<void>::Failure(Error("gameplay.traversal.invalid_carrier_binding", "passenger state is unavailable"));
    const auto can = CanUseMode(passenger, mode);
    if (can.kind == TraversalResultKind::Rejected)
        return foundation::Result<void>::Failure(Error("gameplay.traversal.mode_rejected", "carrier mode is not usable by passenger"));
    Bump();
    carrier_by_passenger_.emplace(passenger, TraversalCarrierBinding{passenger, carrier, mode, role, revision_});
    state->current_mode = mode;
    state->revision = revision_;
    ++boarding_ops_;
    Record({0, TraversalChangeKind::CarrierBoarded, passenger, mode, {}, carrier, {}, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> TraversalService::Disembark(GameplayObjectRef passenger, GameplayContext context)
{
    const auto it = carrier_by_passenger_.find(passenger);
    if (it == carrier_by_passenger_.end())
        return foundation::Result<void>::Failure(Error("gameplay.traversal.binding_missing", "carrier binding missing"));
    const auto carrier_mode = it->second.carrier_mode;
    const auto carrier = it->second.carrier;
    auto* state = FindMutableState(passenger);
    if (!state)
        return foundation::Result<void>::Failure(Error("gameplay.traversal.state_missing", "passenger traversal state missing"));
    const auto profile = profiles_.find(state->profile);
    if (profile == profiles_.end())
        return foundation::Result<void>::Failure(Error("gameplay.traversal.profile_missing", "passenger profile missing"));
    carrier_by_passenger_.erase(it);
    Bump();
    state->current_mode = profile->second.default_mode;
    state->revision = revision_;
    Record({0, TraversalChangeKind::CarrierDisembarked, passenger, carrier_mode, {}, carrier, {}, context, revision_});
    return foundation::Result<void>::Success();
}

const TraversalState* TraversalService::FindState(GameplayObjectRef subject) const noexcept
{
    const auto it = states_.find(subject);
    return it == states_.end() ? nullptr : &it->second;
}
TraversalState* TraversalService::FindMutableState(GameplayObjectRef subject) noexcept
{
    const auto it = states_.find(subject);
    return it == states_.end() ? nullptr : &it->second;
}
const TraversalSession* TraversalService::FindSession(TraversalSessionId id) const noexcept
{
    const auto it = sessions_.find(id);
    return it == sessions_.end() ? nullptr : &it->second;
}
TraversalSession* TraversalService::FindMutableSession(TraversalSessionId id) noexcept
{
    const auto it = sessions_.find(id);
    return it == sessions_.end() ? nullptr : &it->second;
}
bool TraversalService::ProfileAllows(const TraversalProfile& profile, TraversalModeId mode) const noexcept
{
    return std::binary_search(profile.allowed_modes.begin(), profile.allowed_modes.end(), mode);
}
bool TraversalService::ProfileHasCapability(const TraversalProfile& profile, TraversalCapabilityId capability) const noexcept
{
    return std::binary_search(profile.capabilities.begin(), profile.capabilities.end(), capability);
}

std::vector<TraversalState> TraversalService::FindSubjectsUsingMode(TraversalModeId mode) const
{
    std::vector<TraversalState> result;
    for (const auto& [ref, state] : states_)
    {
        (void)ref;
        if (state.current_mode == mode)
            result.push_back(state);
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.subject < b.subject; });
    return result;
}

std::vector<TraversalCarrierBinding> TraversalService::FindCarrierPassengers(GameplayObjectRef carrier) const
{
    std::vector<TraversalCarrierBinding> result;
    for (const auto& [passenger, binding] : carrier_by_passenger_)
    {
        (void)passenger;
        if (binding.carrier == carrier)
            result.push_back(binding);
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.passenger < b.passenger; });
    return result;
}

std::vector<TraversalChange> TraversalService::ChangesSince(std::uint64_t sequence) const
{
    return ReadChangesSince(sequence).changes;
}
TraversalChangeBatch TraversalService::ReadChangesSince(std::uint64_t sequence) const
{
    TraversalChangeBatch batch;
    batch.oldest_available_sequence = changes_.empty() ? next_change_sequence_ : changes_.front().sequence;
    if (!changes_.empty() && sequence + 1 < changes_.front().sequence)
    {
        batch.snapshot_required = true;
        return batch;
    }
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(batch.changes),
                 [sequence](const auto& change) { return change.sequence > sequence; });
    return batch;
}

TraversalSnapshot TraversalService::CaptureSnapshot() const
{
    TraversalSnapshot snapshot;
    for (const auto& [ref, state] : states_)
    {
        (void)ref;
        snapshot.states.push_back(state);
    }
    for (const auto& [id, session] : sessions_)
    {
        (void)id;
        if (IsLiveSession(session.state))
            snapshot.sessions.push_back(session);
    }
    for (const auto& [id, route] : routes_)
    {
        (void)id;
        snapshot.routes.push_back(route);
    }
    for (const auto& [id, grant] : capability_grants_)
    {
        (void)id;
        snapshot.capability_grants.push_back(grant);
    }
    for (const auto& [ref, binding] : carrier_by_passenger_)
    {
        (void)ref;
        snapshot.carrier_bindings.push_back(binding);
    }
    std::sort(snapshot.states.begin(), snapshot.states.end(), [](const auto& a, const auto& b) { return a.subject < b.subject; });
    std::sort(snapshot.sessions.begin(), snapshot.sessions.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(snapshot.routes.begin(), snapshot.routes.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(snapshot.capability_grants.begin(), snapshot.capability_grants.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(snapshot.carrier_bindings.begin(), snapshot.carrier_bindings.end(), [](const auto& a, const auto& b) { return a.passenger < b.passenger; });
    snapshot.session_ids = session_ids_.GetSnapshot();
    snapshot.route_ids = route_ids_.GetSnapshot();
    snapshot.capability_grant_ids = capability_grant_ids_.GetSnapshot();
    snapshot.revision = revision_;
    snapshot.journal.assign(changes_.begin(), changes_.end());
    snapshot.next_change_sequence = next_change_sequence_;
    return snapshot;
}

foundation::Result<void> TraversalService::RestoreSnapshot(TraversalSnapshot snapshot)
{
    std::unordered_map<GameplayObjectRef, TraversalState, RefHash> restored_states;
    std::unordered_map<TraversalSessionId, TraversalSession, IdHash> restored_sessions;
    std::unordered_map<TraversalRouteId, TraversalRoute, IdHash> restored_routes;
    std::unordered_map<TraversalCapabilityGrantId, TraversalCapabilityGrant, IdHash> restored_grants;
    std::unordered_map<GameplayObjectRef, TraversalCarrierBinding, RefHash> restored_bindings;
    std::deque<TraversalChange> restored_changes;

    if (snapshot.journal.size() > kChangeJournalCapacity || snapshot.next_change_sequence == 0)
        return foundation::Result<void>::Failure(Error("gameplay.traversal.restore_invalid", "invalid snapshot journal"));
    for (const auto& state : snapshot.states)
    {
        const auto profile = profiles_.find(state.profile);
        if (!state.subject.IsValid() || profile == profiles_.end() || !modes_.contains(state.current_mode) ||
            !ProfileAllows(profile->second, state.current_mode) || !restored_states.emplace(state.subject, state).second)
            return foundation::Result<void>::Failure(Error("gameplay.traversal.restore_invalid", "invalid traversal state"));
    }
    for (const auto& route : snapshot.routes)
    {
        if (!route.id.IsValid() || !route.subject.IsValid() || !modes_.contains(route.mode) ||
            !restored_states.contains(route.subject) || !restored_routes.emplace(route.id, route).second)
            return foundation::Result<void>::Failure(Error("gameplay.traversal.restore_invalid", "invalid traversal route"));
    }
    for (const auto& grant : snapshot.capability_grants)
    {
        if (!grant.id.IsValid() || !grant.subject.IsValid() || !grant.capability.IsValid() || grant.parameter_micro < 0 ||
            !restored_states.contains(grant.subject) || !restored_grants.emplace(grant.id, grant).second)
            return foundation::Result<void>::Failure(Error("gameplay.traversal.restore_invalid", "invalid capability grant"));
    }
    std::unordered_set<GameplayObjectRef, RefHash> live_subjects;
    for (const auto& session : snapshot.sessions)
    {
        if (!session.id.IsValid() || !restored_states.contains(session.subject) || !modes_.contains(session.mode) ||
            !IsLiveSession(session.state) || (session.route.IsValid() && !restored_routes.contains(session.route)) ||
            !live_subjects.insert(session.subject).second || !restored_sessions.emplace(session.id, session).second)
            return foundation::Result<void>::Failure(Error("gameplay.traversal.restore_invalid", "invalid traversal session"));
        const auto& state = restored_states.at(session.subject);
        if (!state.active_session || *state.active_session != session.id)
            return foundation::Result<void>::Failure(Error("gameplay.traversal.restore_invalid", "session and subject state disagree"));
    }
    for (const auto& [subject, state] : restored_states)
        if (state.active_session && !restored_sessions.contains(*state.active_session))
            return foundation::Result<void>::Failure(Error("gameplay.traversal.restore_invalid", "state references missing session"));
    for (const auto& binding : snapshot.carrier_bindings)
    {
        if (!binding.passenger.IsValid() || !binding.carrier.IsValid() || binding.passenger == binding.carrier ||
            !restored_states.contains(binding.passenger) || !modes_.contains(binding.carrier_mode) ||
            restored_states.at(binding.passenger).active_session ||
            !restored_bindings.emplace(binding.passenger, binding).second)
            return foundation::Result<void>::Failure(Error("gameplay.traversal.restore_invalid", "invalid carrier binding"));
    }
    std::uint64_t previous = 0;
    for (const auto& change : snapshot.journal)
    {
        if (change.sequence == 0 || (previous != 0 && change.sequence <= previous) || change.sequence >= snapshot.next_change_sequence)
            return foundation::Result<void>::Failure(Error("gameplay.traversal.restore_invalid", "invalid journal sequence"));
        restored_changes.push_back(change);
        previous = change.sequence;
    }

    states_.swap(restored_states);
    sessions_.swap(restored_sessions);
    routes_.swap(restored_routes);
    capability_grants_.swap(restored_grants);
    carrier_by_passenger_.swap(restored_bindings);
    changes_.swap(restored_changes);
    session_ids_.Restore(snapshot.session_ids);
    route_ids_.Restore(snapshot.route_ids);
    capability_grant_ids_.Restore(snapshot.capability_grant_ids);
    revision_ = snapshot.revision;
    next_change_sequence_ = snapshot.next_change_sequence;
    return foundation::Result<void>::Success();
}

TraversalDiagnostics TraversalService::GetDiagnostics() const noexcept
{
    TraversalDiagnostics diagnostics;
    diagnostics.profiles = profiles_.size();
    diagnostics.modes = modes_.size();
    diagnostics.states = states_.size();
    diagnostics.carrier_bindings = carrier_by_passenger_.size();
    diagnostics.mode_changes = mode_changes_;
    diagnostics.rejected_mode_changes = rejected_mode_changes_;
    diagnostics.boarding_ops = boarding_ops_;
    for (const auto& [id, session] : sessions_)
    {
        (void)id;
        if (IsLiveSession(session.state))
            ++diagnostics.active_sessions;
    }
    return diagnostics;
}

void TraversalService::Record(TraversalChange change)
{
    change.sequence = next_change_sequence_++;
    changes_.push_back(std::move(change));
    while (changes_.size() > kChangeJournalCapacity)
        changes_.pop_front();
}
} // namespace epidemic::gameplay::traversal
