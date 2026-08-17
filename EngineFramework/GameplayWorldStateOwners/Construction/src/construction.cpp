#include "Epidemic/GameFramework/Construction/construction.h"

#include <iterator>
#include <utility>

namespace epidemic::gameplay::construction
{
namespace
{
foundation::Error Error(std::string_view c, std::string_view m)
{
    return foundation::Error::Create(c, m);
}
} // namespace
ConstructionService::ConstructionService()
    : plan_ids_(TypeId::FromString("framework.construction.plan").Raw()),
      site_ids_(TypeId::FromString("framework.construction.site").Raw()),
      placed_ids_(TypeId::FromString("framework.construction.placed").Raw()),
      execution_ids_(TypeId::FromString("framework.construction.execution").Raw())
{
}
foundation::Result<void> ConstructionService::RegisterPlacementDefinition(PlacementDefinition d)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "construction registry frozen"));
    if (!d.id.IsValid() || d.canonical_name.empty() || placement_definitions_.contains(d.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.invalid_placement_definition", "invalid placement definition"));
    placement_definitions_.emplace(d.id, std::move(d));
    return foundation::Result<void>::Success();
}
foundation::Result<void> ConstructionService::RegisterRecipe(ConstructionRecipe r)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "construction registry frozen"));
    if (!r.id.IsValid() || r.canonical_name.empty() || recipes_.contains(r.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.invalid_recipe", "invalid construction recipe"));
    recipes_.emplace(r.id, std::move(r));
    return foundation::Result<void>::Success();
}
foundation::Result<void> ConstructionService::RegisterSocket(PlacementSocket s)
{
    if (!s.id.IsValid() || !s.owner.IsValid() || sockets_.contains(s.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.invalid_socket", "invalid placement socket"));
    Bump();
    s.revision = revision_;
    sockets_.emplace(s.id, s);
    return foundation::Result<void>::Success();
}
const ConstructionRecipe *ConstructionService::FindRecipe(ConstructionRecipeId id) const noexcept
{
    auto it = recipes_.find(id);
    return it == recipes_.end() ? nullptr : &it->second;
}
const PlacementDefinition *ConstructionService::FindPlacementDefinition(PlacementRuleId id) const noexcept
{
    auto it = placement_definitions_.find(id);
    return it == placement_definitions_.end() ? nullptr : &it->second;
}
const PlacementSocket *ConstructionService::FindSocket(PlacementSocketId id) const noexcept
{
    auto it = sockets_.find(id);
    return it == sockets_.end() ? nullptr : &it->second;
}
foundation::Result<PlacementValidationResult> ConstructionService::ValidatePlacement(const PlacementRequest &r) const
{
    ++const_cast<ConstructionService *>(this)->validations_;
    PlacementValidationResult out;
    out.dependencies_revision = revision_;
    auto recipe = recipes_.find(r.recipe);
    auto def = placement_definitions_.find(r.placement_rule);
    if (!r.actor.IsValid() || recipe == recipes_.end() || def == placement_definitions_.end())
    {
        out.availability = PlacementAvailability::InvalidTarget;
        out.reasons.push_back({PlacementReasonId::FromString("construction.invalid_request")});
        ++const_cast<ConstructionService *>(this)->rejections_;
        return foundation::Result<PlacementValidationResult>::Success(out);
    }
    if (def->second.target_kind == PlacementTargetKind::Socket)
    {
        if (!r.target.socket)
        {
            out.availability = PlacementAvailability::InvalidTarget;
            out.reasons.push_back({PlacementReasonId::FromString("construction.socket_required")});
            ++const_cast<ConstructionService *>(this)->rejections_;
            return foundation::Result<PlacementValidationResult>::Success(out);
        }
        auto s = sockets_.find(*r.target.socket);
        if (s == sockets_.end())
        {
            out.availability = PlacementAvailability::InvalidTarget;
            out.reasons.push_back({PlacementReasonId::FromString("construction.socket_missing")});
            ++const_cast<ConstructionService *>(this)->rejections_;
            return foundation::Result<PlacementValidationResult>::Success(out);
        }
        if (s->second.state != SocketState::Free)
        {
            out.availability = PlacementAvailability::Blocked;
            out.reasons.push_back({PlacementReasonId::FromString("construction.socket_blocked")});
            ++const_cast<ConstructionService *>(this)->rejections_;
            return foundation::Result<PlacementValidationResult>::Success(out);
        }
    }
    if (def->second.target_kind == PlacementTargetKind::Area && !r.target.area)
    {
        out.availability = PlacementAvailability::InvalidTarget;
        out.reasons.push_back({PlacementReasonId::FromString("construction.area_required")});
        ++const_cast<ConstructionService *>(this)->rejections_;
        return foundation::Result<PlacementValidationResult>::Success(out);
    }
    if (def->second.target_kind == PlacementTargetKind::Free && !r.target.position)
    {
        out.availability = PlacementAvailability::InvalidTarget;
        out.reasons.push_back({PlacementReasonId::FromString("construction.position_required")});
        ++const_cast<ConstructionService *>(this)->rejections_;
        return foundation::Result<PlacementValidationResult>::Success(out);
    }
    out.availability = PlacementAvailability::Available;
    out.footprint.volume.area = r.target.area.value_or(GameplayObjectRef{});
    if (r.target.position)
    {
        out.footprint.volume.min = *r.target.position;
        out.footprint.volume.max = *r.target.position;
        out.footprint.volume.max.x_mm += 1000;
        out.footprint.volume.max.y_mm += 1000;
        out.footprint.volume.max.z_mm += 1000;
    }
    return foundation::Result<PlacementValidationResult>::Success(out);
}
foundation::Result<PlacementPlan> ConstructionService::PreparePlacementPlan(const PlacementRequest &r)
{
    auto validation = ValidatePlacement(r);
    if (!validation)
        return foundation::Result<PlacementPlan>::Failure(validation.GetError());
    if (validation.Value().availability != PlacementAvailability::Available)
        return foundation::Result<PlacementPlan>::Failure(
            Error("gameplay.construction.placement_rejected", "placement is not available"));
    auto recipe = recipes_.find(r.recipe);
    if (recipe == recipes_.end())
        return foundation::Result<PlacementPlan>::Failure(
            Error("gameplay.construction.recipe_missing", "recipe missing"));
    PlacementPlan p;
    p.id = PlacementPlanId{plan_ids_.Next()};
    p.actor = r.actor;
    p.recipe = r.recipe;
    p.target = r.target;
    p.footprint = validation.Value().footprint;
    p.reserved_costs = recipe->second.costs;
    p.dependency_revision = validation.Value().dependencies_revision;
    p.context = r.context;
    if (r.target.socket)
    {
        p.dependency_socket = r.target.socket;
        auto *socket = FindSocket(*r.target.socket);
        if (socket)
            p.dependency_socket_revision = socket->revision;
    }
    plans_.emplace(p.id, p);
    Record({0, ConstructionChangeKind::PlacementValidated, p.id, {}, {}, {}, r.actor, revision_, r.context});
    return foundation::Result<PlacementPlan>::Success(p);
}
foundation::Result<PlacementCommitResult> ConstructionService::CommitPlacement(PlacementPlanId id,
                                                                               ConstructionCostPolicy cost_policy)
{
    (void)cost_policy;
    auto it = plans_.find(id);
    if (it == plans_.end())
        return foundation::Result<PlacementCommitResult>::Failure(
            Error("gameplay.construction.plan_missing", "placement plan missing"));
    auto &plan = it->second;
    if (plan.state == PlacementPlanState::Committed)
        return foundation::Result<PlacementCommitResult>::Failure(
            Error("gameplay.construction.already_committed", "placement plan already committed"));
    if (plan.state != PlacementPlanState::Prepared)
        return foundation::Result<PlacementCommitResult>::Failure(
            Error("gameplay.construction.plan_not_prepared", "placement plan is not prepared"));
    auto recipe = recipes_.find(plan.recipe);
    if (!plan.id.IsValid() || !plan.actor.IsValid() || recipe == recipes_.end())
        return foundation::Result<PlacementCommitResult>::Failure(
            Error("gameplay.construction.invalid_plan", "invalid placement plan"));
    PlacementSocket *socket = nullptr;
    if (plan.dependency_socket)
    {
        socket = FindMutableSocket(*plan.dependency_socket);
        if (!socket)
            return foundation::Result<PlacementCommitResult>::Failure(
                Error("gameplay.construction.stale_plan", "placement socket missing"));
        if (socket->revision != plan.dependency_socket_revision)
            return foundation::Result<PlacementCommitResult>::Failure(
                Error("gameplay.construction.stale_plan", "placement dependencies changed"));
        if (socket->state == SocketState::Disabled || socket->state == SocketState::Occupied)
            return foundation::Result<PlacementCommitResult>::Failure(
                Error("gameplay.construction.socket_unavailable", "socket unavailable"));
    }
    PlacementCommitResult r;
    r.execution = PlacementExecutionId{execution_ids_.Next()};
    r.placed_object = PlacedObjectId{placed_ids_.Next()};
    PlacementOutputOperation entity;
    entity.type = PlacementOutputTypeId::FromString("construction.output.create_entity");
    entity.archetype = recipe->second.result_entity_archetype;
    entity.placed_record = r.placed_object;
    r.outputs.push_back(entity);
    if (plan.target.area)
    {
        PlacementOutputOperation nav;
        nav.type = PlacementOutputTypeId::FromString("construction.output.navigation_layer");
        nav.subject = *plan.target.area;
        nav.placed_record = r.placed_object;
        r.outputs.push_back(nav);
    }
    Bump();
    r.revision = revision_;
    placed_objects_.push_back(r.placed_object);
    plan.state = PlacementPlanState::Committed;
    if (socket)
    {
        socket->state = SocketState::Occupied;
        socket->revision = revision_;
        Record({0,
                ConstructionChangeKind::SocketOccupied,
                plan.id,
                r.execution,
                {},
                *plan.dependency_socket,
                socket->owner,
                revision_,
                plan.context});
    }
    ++committed_;
    Record({0,
            ConstructionChangeKind::PlacementCommitted,
            plan.id,
            r.execution,
            {},
            plan.target.socket.value_or(PlacementSocketId{}),
            plan.actor,
            revision_,
            plan.context});
    Record({0,
            ConstructionChangeKind::PlacedObjectCreated,
            plan.id,
            r.execution,
            {},
            {},
            plan.actor,
            revision_,
            plan.context});
    return foundation::Result<PlacementCommitResult>::Success(r);
}
foundation::Result<PlacementCommitResult> ConstructionService::CommitPlacement(const PlacementPlan &plan,
                                                                               ConstructionCostPolicy cost_policy)
{
    return CommitPlacement(plan.id, cost_policy);
}
foundation::Result<ConstructionSiteId> ConstructionService::StartConstructionSite(const PlacementPlan &plan,
                                                                                  GameplayTimePoint started_at,
                                                                                  GameplayContext context)
{
    if (!plan.id.IsValid() || !recipes_.contains(plan.recipe))
        return foundation::Result<ConstructionSiteId>::Failure(
            Error("gameplay.construction.invalid_plan", "invalid construction site plan"));
    ConstructionSite s;
    s.id = ConstructionSiteId{site_ids_.Next()};
    s.actor = plan.actor;
    s.recipe = plan.recipe;
    s.target = plan.target;
    s.state = ConstructionSiteState::UnderConstruction;
    s.started_at = started_at;
    Bump();
    s.revision = revision_;
    sites_.emplace(s.id, s);
    Record({0, ConstructionChangeKind::SiteStarted, plan.id, {}, s.id, {}, plan.actor, revision_, context});
    return foundation::Result<ConstructionSiteId>::Success(s.id);
}
foundation::Result<void> ConstructionService::CompleteConstructionSite(ConstructionSiteId id, GameplayContext context)
{
    auto *s = FindMutableSite(id);
    if (!s)
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.site_missing", "construction site missing"));
    Bump();
    s->state = ConstructionSiteState::Completed;
    s->progress_micro = 1'000'000;
    s->revision = revision_;
    ++completed_sites_;
    Record({0, ConstructionChangeKind::SiteCompleted, {}, {}, id, {}, s->actor, revision_, context});
    return foundation::Result<void>::Success();
}
foundation::Result<void> ConstructionService::CancelConstructionSite(ConstructionSiteId id, GameplayContext context)
{
    auto *s = FindMutableSite(id);
    if (!s)
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.site_missing", "construction site missing"));
    Bump();
    s->state = ConstructionSiteState::Cancelled;
    s->revision = revision_;
    Record({0, ConstructionChangeKind::SiteCancelled, {}, {}, id, {}, s->actor, revision_, context});
    return foundation::Result<void>::Success();
}
foundation::Result<void> ConstructionService::ReserveSocket(PlacementSocketId id, GameplayContext context)
{
    auto *s = FindMutableSocket(id);
    if (!s || s->state != SocketState::Free)
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.socket_unavailable", "socket unavailable"));
    Bump();
    s->state = SocketState::Reserved;
    s->revision = revision_;
    ++socket_reservations_;
    Record({0, ConstructionChangeKind::SocketReserved, {}, {}, {}, id, s->owner, revision_, context});
    return foundation::Result<void>::Success();
}
foundation::Result<void> ConstructionService::ReleaseSocket(PlacementSocketId id, GameplayContext context)
{
    auto *s = FindMutableSocket(id);
    if (!s || s->state == SocketState::Occupied)
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.socket_not_releasable", "socket not releasable"));
    Bump();
    s->state = SocketState::Free;
    s->revision = revision_;
    Record({0, ConstructionChangeKind::SocketReleased, {}, {}, {}, id, s->owner, revision_, context});
    return foundation::Result<void>::Success();
}
foundation::Result<void> ConstructionService::OccupySocket(PlacementSocketId id, GameplayContext context)
{
    auto *s = FindMutableSocket(id);
    if (!s || s->state == SocketState::Disabled || s->state == SocketState::Occupied)
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.socket_unavailable", "socket unavailable"));
    Bump();
    s->state = SocketState::Occupied;
    s->revision = revision_;
    Record({0, ConstructionChangeKind::SocketOccupied, {}, {}, {}, id, s->owner, revision_, context});
    return foundation::Result<void>::Success();
}
const PlacementPlan *ConstructionService::FindPlan(PlacementPlanId id) const noexcept
{
    auto it = plans_.find(id);
    return it == plans_.end() ? nullptr : &it->second;
}
const ConstructionSite *ConstructionService::FindSite(ConstructionSiteId id) const noexcept
{
    auto it = sites_.find(id);
    return it == sites_.end() ? nullptr : &it->second;
}
PlacementSocket *ConstructionService::FindMutableSocket(PlacementSocketId id) noexcept
{
    auto it = sockets_.find(id);
    return it == sockets_.end() ? nullptr : &it->second;
}
ConstructionSite *ConstructionService::FindMutableSite(ConstructionSiteId id) noexcept
{
    auto it = sites_.find(id);
    return it == sites_.end() ? nullptr : &it->second;
}
std::vector<ConstructionSite> ConstructionService::FindConstructionSites(GameplayObjectRef actor) const
{
    std::vector<ConstructionSite> out;
    for (const auto &[id, s] : sites_)
    {
        (void)id;
        if (!actor.IsValid() || s.actor == actor)
            out.push_back(s);
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return out;
}
std::vector<PlacementSocket> ConstructionService::FindSocketsForObject(GameplayObjectRef owner) const
{
    std::vector<PlacementSocket> out;
    for (const auto &[id, s] : sockets_)
    {
        (void)id;
        if (s.owner == owner)
            out.push_back(s);
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return out;
}
ConstructionSnapshot ConstructionService::CaptureSnapshot() const
{
    ConstructionSnapshot s;
    for (const auto &[id, site] : sites_)
    {
        (void)id;
        if (site.state == ConstructionSiteState::UnderConstruction || site.state == ConstructionSiteState::Paused ||
            site.state == ConstructionSiteState::Planned)
            s.active_sites.push_back(site);
    }
    for (const auto &[id, socket] : sockets_)
    {
        (void)id;
        s.sockets.push_back(socket);
    }
    s.placed_objects = placed_objects_;
    std::sort(s.active_sites.begin(), s.active_sites.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.sockets.begin(), s.sockets.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.placed_objects.begin(), s.placed_objects.end());
    s.plan_ids = plan_ids_.GetSnapshot();
    s.site_ids = site_ids_.GetSnapshot();
    s.placed_ids = placed_ids_.GetSnapshot();
    s.revision = revision_;
    return s;
}
foundation::Result<void> ConstructionService::RestoreSnapshot(ConstructionSnapshot s)
{
    sites_.clear();
    sockets_.clear();
    placed_objects_ = s.placed_objects;
    for (const auto &site : s.active_sites)
    {
        if (!site.id.IsValid() || !recipes_.contains(site.recipe))
            return foundation::Result<void>::Failure(
                Error("gameplay.construction.restore_invalid", "invalid site snapshot"));
        sites_.emplace(site.id, site);
    }
    for (const auto &socket : s.sockets)
    {
        if (!socket.id.IsValid() || !socket.owner.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.construction.restore_invalid", "invalid socket snapshot"));
        sockets_.emplace(socket.id, socket);
    }
    plan_ids_.Restore(s.plan_ids);
    site_ids_.Restore(s.site_ids);
    placed_ids_.Restore(s.placed_ids);
    revision_ = s.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    return foundation::Result<void>::Success();
}
std::vector<ConstructionChange> ConstructionService::ChangesSince(std::uint64_t seq) const
{
    std::vector<ConstructionChange> out;
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(out),
                 [seq](const auto &c) { return c.sequence > seq; });
    return out;
}
ConstructionDiagnostics ConstructionService::GetDiagnostics() const noexcept
{
    ConstructionDiagnostics d;
    d.recipes = recipes_.size();
    d.placement_definitions = placement_definitions_.size();
    d.validations = validations_;
    d.rejections = rejections_;
    d.committed = committed_;
    d.socket_reservations = socket_reservations_;
    d.completed_sites = completed_sites_;
    for (const auto &[id, s] : sites_)
    {
        (void)id;
        if (s.state == ConstructionSiteState::UnderConstruction || s.state == ConstructionSiteState::Paused ||
            s.state == ConstructionSiteState::Planned)
            ++d.active_sites;
    }
    return d;
}
void ConstructionService::Record(ConstructionChange c)
{
    c.sequence = next_change_sequence_++;
    changes_.push_back(c);
}
} // namespace epidemic::gameplay::construction
