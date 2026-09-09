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
[[nodiscard]] constexpr std::uint64_t TraversalSessionIdScope() noexcept
{
    return GameplayObjectId::FromString("framework.traversal.session").High();
}

[[nodiscard]] constexpr std::uint64_t TraversalRouteIdScope() noexcept
{
    return GameplayObjectId::FromString("framework.traversal.route").High();
}

[[nodiscard]] constexpr std::uint64_t TraversalCapabilityGrantIdScope() noexcept
{
    return GameplayObjectId::FromString("framework.traversal.capability_grant").High();
}

foundation::Error Error(std::string code, std::string message)
{
    return foundation::Error::Create(std::move(code), std::move(message));
}

template <class TId>
void AdvanceGeneratorPastAcceptedId(MonotonicIdGenerator<GameplayObjectId>& generator, TId id) noexcept
{
    if (!id.IsValid())
        return;
    auto snapshot = generator.GetSnapshot();
    if (id.value.High() != snapshot.scope || snapshot.next == 0 || id.value.Low() < snapshot.next)
        return;
    snapshot.next = id.value.Low() == std::numeric_limits<std::uint64_t>::max() ? 0 : id.value.Low() + 1;
    generator.Restore(snapshot);
}

template <class TId>
void TrackMaxLow(TId id, std::uint64_t expected_scope, std::uint64_t& max_low) noexcept
{
    if (id.IsValid() && id.value.High() == expected_scope && id.value.Low() > max_low)
        max_low = id.value.Low();
}
}

TraversalService::TraversalService()
    : session_ids_(TraversalSessionIdScope()), route_ids_(TraversalRouteIdScope()),
      capability_grant_ids_(TraversalCapabilityGrantIdScope())
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

    for (const auto& capability : profile.capabilities)
        if (!capability.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.traversal.invalid_profile", "profile capability is invalid"));
    std::sort(profile.capabilities.begin(), profile.capabilities.end(), [](const auto& a, const auto& b) {
        if (a.id != b.id)
            return a.id < b.id;
        return a.parameter_micro < b.parameter_micro;
    });
    std::vector<TraversalCapabilityValue> canonical_capabilities;
    canonical_capabilities.reserve(profile.capabilities.size());
    for (const auto& capability : profile.capabilities)
    {
        if (!canonical_capabilities.empty() && canonical_capabilities.back().id == capability.id)
        {
            canonical_capabilities.back().parameter_micro =
                std::max(canonical_capabilities.back().parameter_micro, capability.parameter_micro);
            continue;
        }
        canonical_capabilities.push_back(capability);
    }
    profile.capabilities = std::move(canonical_capabilities);

    const auto& default_mode = modes_.at(profile.default_mode);
    if (default_mode.required_capability.IsValid() &&
        ProfileCapabilityParameter(profile, default_mode.required_capability) < default_mode.required_capability_parameter_micro)
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

    auto staged_ids = capability_grant_ids_;
    if (!grant.id.IsValid())
        grant.id = {staged_ids.Next()};
    else
        AdvanceGeneratorPastAcceptedId(staged_ids, grant.id);
    if (!grant.id.IsValid())
        return foundation::Result<TraversalCapabilityGrantId>::Failure(
            Error("gameplay.traversal.capability_id_exhausted", "capability grant id is invalid or exhausted"));
    if (capability_grants_.contains(grant.id))
        return foundation::Result<TraversalCapabilityGrantId>::Failure(
            Error("gameplay.already_registered", "capability grant id already exists"));

    Bump();
    capability_grant_ids_ = staged_ids;
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

Fixed TraversalService::EffectiveCapabilityParameter(GameplayObjectRef subject, TraversalCapabilityId capability) const noexcept
{
    if (!capability.IsValid())
        return -1;
    Fixed best = -1;
    const auto* state = FindState(subject);
    if (!state)
        return best;
    const auto profile = profiles_.find(state->profile);
    if (profile != profiles_.end())
        best = std::max(best, ProfileCapabilityParameter(profile->second, capability));
    for (const auto& [id, grant] : capability_grants_)
    {
        (void)id;
        if (grant.subject == subject && grant.capability == capability)
            best = std::max(best, grant.parameter_micro);
    }
    return best;
}

bool TraversalService::HasCapability(GameplayObjectRef subject, TraversalCapabilityId capability,
                                     Fixed min_parameter_micro) const noexcept
{
    if (min_parameter_micro < 0)
        return false;
    return EffectiveCapabilityParameter(subject, capability) >= min_parameter_micro;
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

foundation::Result<void> TraversalService::RemoveState(GameplayObjectRef subject, GameplayContext context)
{
    const auto state_it = states_.find(subject);
    if (state_it == states_.end())
        return foundation::Result<void>::Success();
    if (state_it->second.active_session)
        return foundation::Result<void>::Failure(
            Error("gameplay.traversal.session_active", "cannot remove traversal state with an active session"));
    if (carrier_by_passenger_.contains(subject))
        return foundation::Result<void>::Failure(
            Error("gameplay.traversal.carrier_binding_active", "cannot remove traversal state while passenger is boarded"));
    for (const auto& [passenger, binding] : carrier_by_passenger_)
    {
        (void)passenger;
        if (binding.carrier == subject)
            return foundation::Result<void>::Failure(
                Error("gameplay.traversal.carrier_binding_active", "cannot remove traversal state while carrying passengers"));
    }

    std::vector<TraversalCapabilityGrantId> grant_ids;
    for (const auto& [id, grant] : capability_grants_)
        if (grant.subject == subject)
            grant_ids.push_back(id);
    std::sort(grant_ids.begin(), grant_ids.end());
    for (const auto id : grant_ids)
        capability_grants_.erase(id);

    std::vector<TraversalRouteId> route_ids;
    for (const auto& [id, route] : routes_)
        if (route.subject == subject)
            route_ids.push_back(id);
    std::sort(route_ids.begin(), route_ids.end());
    for (const auto id : route_ids)
    {
        if (IsRouteInUse(id))
            return foundation::Result<void>::Failure(
                Error("gameplay.traversal.route_in_use", "cannot remove traversal state with an active route"));
        routes_.erase(id);
    }

    states_.erase(state_it);
    Bump();
    Record({0, TraversalChangeKind::StateRemoved, subject, {}, {}, {}, {}, context, revision_});
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
    if (!route.subject.IsValid() || !route.mode.IsValid() || !modes_.contains(route.mode) || !states_.contains(route.subject))
        return foundation::Result<TraversalRouteId>::Failure(
            Error("gameplay.traversal.invalid_route", "invalid traversal route"));

    auto staged_ids = route_ids_;
    if (!route.id.IsValid())
        route.id = {staged_ids.Next()};
    else
        AdvanceGeneratorPastAcceptedId(staged_ids, route.id);
    if (!route.id.IsValid())
        return foundation::Result<TraversalRouteId>::Failure(
            Error("gameplay.traversal.route_id_exhausted", "traversal route id is invalid or exhausted"));
    if (routes_.contains(route.id))
        return foundation::Result<TraversalRouteId>::Failure(
            Error("gameplay.already_registered", "traversal route id already exists"));

    Bump();
    route_ids_ = staged_ids;
    route.revision = revision_;
    const auto id = route.id;
    const auto subject = route.subject;
    const auto mode = route.mode;
    routes_.emplace(id, std::move(route));
    Record({0, TraversalChangeKind::RouteRegistered, subject, mode, {}, {}, {}, {}, revision_});
    return foundation::Result<TraversalRouteId>::Success(id);
}

foundation::Result<void> TraversalService::RemoveRoute(TraversalRouteId route, GameplayContext context)
{
    const auto it = routes_.find(route);
    if (it == routes_.end())
        return foundation::Result<void>::Success();
    if (IsRouteInUse(route))
        return foundation::Result<void>::Failure(
            Error("gameplay.traversal.route_in_use", "cannot remove a route used by a live session"));
    const auto subject = it->second.subject;
    const auto mode = it->second.mode;
    routes_.erase(it);
    Bump();
    Record({0, TraversalChangeKind::RouteRemoved, subject, mode, {}, {}, {}, context, revision_});
    return foundation::Result<void>::Success();
}

std::uint64_t TraversalService::RemoveRoutesBySource(GameplayObjectRef source, GameplayContext context)
{
    if (!source.IsValid())
        return 0;
    std::vector<TraversalRouteId> ids;
    for (const auto& [id, route] : routes_)
        if (route.source == source && !IsRouteInUse(id))
            ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    for (const auto id : ids)
        (void)RemoveRoute(id, context);
    return ids.size();
}

const TraversalRoute* TraversalService::FindRoute(TraversalRouteId id) const noexcept
{
    const auto it = routes_.find(id);
    return it == routes_.end() ? nullptr : &it->second;
}

bool TraversalService::IsLiveSession(TraversalSessionState state) const noexcept
{
    return state == TraversalSessionState::Active || state == TraversalSessionState::Suspended;
}

bool TraversalService::IsRouteInUse(TraversalRouteId route) const noexcept
{
    if (!route.IsValid())
        return false;
    for (const auto& [id, session] : sessions_)
    {
        (void)id;
        if (session.route == route && IsLiveSession(session.state))
            return true;
    }
    return false;
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

    auto staged_ids = session_ids_;
    const TraversalSessionId id{staged_ids.Next()};
    if (!id.IsValid())
        return foundation::Result<TraversalSessionId>::Failure(
            Error("gameplay.traversal.session_id_exhausted", "traversal session id exhausted"));

    Bump();
    session_ids_ = staged_ids;
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

foundation::Result<void> TraversalService::FinalizeSession(TraversalSessionId id, TraversalSessionState terminal_state,
                                                           TraversalChangeKind change_kind, TraversalReasonId reason,
                                                           GameplayContext context)
{
    const auto it = sessions_.find(id);
    if (it == sessions_.end() || !IsLiveSession(it->second.state))
        return foundation::Result<void>::Failure(Error("gameplay.traversal.session_state", "session is missing or already terminal"));

    TraversalSession session = it->second;
    session.state = terminal_state;
    Bump();
    session.revision = revision_;
    if (auto* state = FindMutableState(session.subject); state && state->active_session == id)
    {
        state->active_session.reset();
        state->revision = revision_;
    }
    sessions_.erase(it);
    Record({0, change_kind, session.subject, session.mode, id, {}, reason, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> TraversalService::CompleteSession(TraversalSessionId id, GameplayContext context)
{
    return FinalizeSession(id, TraversalSessionState::Completed, TraversalChangeKind::SessionCompleted, {}, context);
}

foundation::Result<void> TraversalService::CancelSession(TraversalSessionId id, TraversalReasonId reason,
                                                         GameplayContext context)
{
    return FinalizeSession(id, TraversalSessionState::Cancelled, TraversalChangeKind::SessionCancelled, reason, context);
}

foundation::Result<void> TraversalService::FailSession(TraversalSessionId id, TraversalReasonId reason,
                                                       GameplayContext context)
{
    return FinalizeSession(id, TraversalSessionState::Failed, TraversalChangeKind::SessionFailed, reason, context);
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
    const auto previous_mode = state->current_mode;
    const auto can = CanUseMode(passenger, mode);
    if (can.kind == TraversalResultKind::Rejected)
        return foundation::Result<void>::Failure(Error("gameplay.traversal.mode_rejected", "carrier mode is not usable by passenger"));
    Bump();
    carrier_by_passenger_.emplace(passenger, TraversalCarrierBinding{passenger, carrier, mode, previous_mode, role, revision_});
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
    const auto binding = it->second;
    auto* state = FindMutableState(passenger);
    if (!state)
        return foundation::Result<void>::Failure(Error("gameplay.traversal.state_missing", "passenger traversal state missing"));
    const auto profile = profiles_.find(state->profile);
    if (profile == profiles_.end())
        return foundation::Result<void>::Failure(Error("gameplay.traversal.profile_missing", "passenger profile missing"));

    TraversalModeId next_mode = profile->second.default_mode;
    if (binding.previous_mode.IsValid() && CanUseMode(passenger, binding.previous_mode).kind != TraversalResultKind::Rejected)
        next_mode = binding.previous_mode;

    carrier_by_passenger_.erase(it);
    Bump();
    state->current_mode = next_mode;
    state->revision = revision_;
    Record({0, TraversalChangeKind::CarrierDisembarked, passenger, next_mode, {}, binding.carrier, {}, context, revision_});
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
Fixed TraversalService::ProfileCapabilityParameter(const TraversalProfile& profile, TraversalCapabilityId capability) const noexcept
{
    Fixed best = -1;
    for (const auto& value : profile.capabilities)
    {
        if (value.id == capability)
            best = std::max(best, value.parameter_micro);
        if (value.id > capability)
            break;
    }
    return best;
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

std::vector<TraversalChange> TraversalService::ChangesSinceSequence(std::uint64_t sequence) const
{
    return ReadChangesSinceSequence(sequence).changes;
}
TraversalChangeBatch TraversalService::ReadChangesSinceSequence(std::uint64_t sequence) const
{
    TraversalChangeBatch batch;
    const auto latest = LatestChangeCursor().sequence;
    batch.oldest_available_sequence = changes_.empty() ? next_change_sequence_ : changes_.front().sequence;
    if (next_change_sequence_ == 0 || sequence > latest)
    {
        batch.snapshot_required = true;
        return batch;
    }
    if (changes_.empty())
    {
        batch.snapshot_required = sequence < latest;
        return batch;
    }
    if (sequence < batch.oldest_available_sequence && batch.oldest_available_sequence - sequence > 1)
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
        {
            auto stored_session = session;
            if (stored_session.route.IsValid())
            {
                const auto route_it = routes_.find(stored_session.route);
                if (route_it == routes_.end() || route_it->second.lifetime != TraversalRouteLifetime::Persistent)
                    stored_session.route = {};
            }
            snapshot.sessions.push_back(stored_session);
        }
    }
    for (const auto& [id, route] : routes_)
    {
        (void)id;
        if (route.lifetime == TraversalRouteLifetime::Persistent)
            snapshot.routes.push_back(route);
    }
    for (const auto& [id, grant] : capability_grants_)
    {
        (void)id;
        if (grant.persistent)
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
    snapshot.change_epoch = journal_epoch_;
    return snapshot;
}

foundation::Result<void> TraversalService::RestoreSnapshot(TraversalSnapshot snapshot)
{
    const auto next_journal_epoch = CheckedNextChangeEpoch(snapshot.change_epoch > journal_epoch_ ? snapshot.change_epoch : journal_epoch_);
    if (!next_journal_epoch)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.change_journal.epoch_exhausted", "change journal epoch is exhausted"));
    }
    std::unordered_map<GameplayObjectRef, TraversalState, RefHash> restored_states;
    std::unordered_map<TraversalSessionId, TraversalSession, IdHash> restored_sessions;
    std::unordered_map<TraversalRouteId, TraversalRoute, IdHash> restored_routes;
    std::unordered_map<TraversalCapabilityGrantId, TraversalCapabilityGrant, IdHash> restored_grants;
    std::unordered_map<GameplayObjectRef, TraversalCarrierBinding, RefHash> restored_bindings;
    std::deque<TraversalChange> restored_changes;

    if (snapshot.journal.size() > kChangeJournalCapacity)
        return foundation::Result<void>::Failure(Error("gameplay.traversal.restore_invalid", "invalid snapshot journal"));

    std::uint64_t max_session_low = 0;
    std::uint64_t max_route_low = 0;
    std::uint64_t max_grant_low = 0;
    const auto session_scope = session_ids_.Scope().Raw();
    const auto route_scope = route_ids_.Scope().Raw();
    const auto grant_scope = capability_grant_ids_.Scope().Raw();

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
            route.lifetime != TraversalRouteLifetime::Persistent || !restored_states.contains(route.subject) ||
            !restored_routes.emplace(route.id, route).second)
            return foundation::Result<void>::Failure(Error("gameplay.traversal.restore_invalid", "invalid traversal route"));
        TrackMaxLow(route.id, route_scope, max_route_low);
    }
    for (const auto& grant : snapshot.capability_grants)
    {
        if (!grant.id.IsValid() || !grant.subject.IsValid() || !grant.capability.IsValid() || grant.parameter_micro < 0 ||
            !grant.persistent || !restored_states.contains(grant.subject) || !restored_grants.emplace(grant.id, grant).second)
            return foundation::Result<void>::Failure(Error("gameplay.traversal.restore_invalid", "invalid capability grant"));
        TrackMaxLow(grant.id, grant_scope, max_grant_low);
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
        TrackMaxLow(session.id, session_scope, max_session_low);
    }
    for (const auto& [subject, state] : restored_states)
        if (state.active_session && !restored_sessions.contains(*state.active_session))
            return foundation::Result<void>::Failure(Error("gameplay.traversal.restore_invalid", "state references missing session"));
    for (const auto& binding : snapshot.carrier_bindings)
    {
        if (!binding.passenger.IsValid() || !binding.carrier.IsValid() || binding.passenger == binding.carrier ||
            !restored_states.contains(binding.passenger) || !modes_.contains(binding.carrier_mode) ||
            (binding.previous_mode.IsValid() && !modes_.contains(binding.previous_mode)) ||
            restored_states.at(binding.passenger).active_session ||
            !restored_bindings.emplace(binding.passenger, binding).second)
            return foundation::Result<void>::Failure(Error("gameplay.traversal.restore_invalid", "invalid carrier binding"));
    }
    if (snapshot.next_change_sequence == 0 &&
        (snapshot.journal.empty() || snapshot.journal.back().sequence != std::numeric_limits<std::uint64_t>::max()))
        return foundation::Result<void>::Failure(
            Error("gameplay.traversal.restore_invalid", "exhausted traversal journal is missing terminal sequence"));

    std::uint64_t previous = 0;
    for (const auto& change : snapshot.journal)
    {
        const bool reaches_next = snapshot.next_change_sequence != 0 && change.sequence >= snapshot.next_change_sequence;
        if (change.sequence == 0 || (previous != 0 && change.sequence <= previous) || reaches_next)
            return foundation::Result<void>::Failure(Error("gameplay.traversal.restore_invalid", "invalid journal sequence"));
        restored_changes.push_back(change);
        previous = change.sequence;
    }

    const auto session_generator_ok = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        snapshot.session_ids, session_ids_.Scope(), max_session_low);
    const auto route_generator_ok = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        snapshot.route_ids, route_ids_.Scope(), max_route_low);
    const auto grant_generator_ok = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        snapshot.capability_grant_ids, capability_grant_ids_.Scope(), max_grant_low);
    if (!session_generator_ok || !route_generator_ok || !grant_generator_ok)
        return foundation::Result<void>::Failure(Error("gameplay.traversal.restore_invalid", "invalid generator snapshot"));

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
    journal_epoch_ = *next_journal_epoch;
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
    if (next_change_sequence_ == 0)
        return;
    change.sequence = next_change_sequence_;
    if (next_change_sequence_ == std::numeric_limits<std::uint64_t>::max())
        next_change_sequence_ = 0;
    else
        ++next_change_sequence_;
    changes_.push_back(std::move(change));
    while (changes_.size() > kChangeJournalCapacity)
        changes_.pop_front();
}
} // namespace epidemic::gameplay::traversal
