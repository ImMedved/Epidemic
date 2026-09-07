#include "Epidemic/GameFramework/Construction/construction.h"

#include <limits>
#include <utility>

namespace epidemic::gameplay::construction
{
namespace
{
foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}

[[nodiscard]] Fixed SaturatingAdd(Fixed a, Fixed b) noexcept
{
    if (b > 0 && a > std::numeric_limits<Fixed>::max() - b)
        return std::numeric_limits<Fixed>::max();
    if (b < 0 && a < std::numeric_limits<Fixed>::min() - b)
        return std::numeric_limits<Fixed>::min();
    return a + b;
}

[[nodiscard]] bool HasAllExact(const GameplayTagSet &candidate, const GameplayTagSet &required) noexcept
{
    for (const auto tag : required.Values())
        if (!candidate.HasExact(tag))
            return false;
    return true;
}

[[nodiscard]] bool HasAnyExact(const GameplayTagSet &candidate, const GameplayTagSet &required) noexcept
{
    for (const auto tag : required.Values())
        if (candidate.HasExact(tag))
            return true;
    return false;
}

[[nodiscard]] bool FootprintDimensionsValid(const WorldPosition &size) noexcept
{
    return size.x_mm >= 0 && size.y_mm >= 0 && size.z_mm >= 0;
}

[[nodiscard]] bool IsLiveSite(ConstructionSiteState state) noexcept
{
    return state == ConstructionSiteState::Planned || state == ConstructionSiteState::ResourcesCommitted ||
           state == ConstructionSiteState::UnderConstruction || state == ConstructionSiteState::Paused;
}
} // namespace

ConstructionService::ConstructionService()
    : plan_ids_(TypeId::FromString("framework.construction.plan").Raw()),
      site_ids_(TypeId::FromString("framework.construction.site").Raw()),
      placed_ids_(TypeId::FromString("framework.construction.placed").Raw()),
      execution_ids_(TypeId::FromString("framework.construction.execution").Raw()),
      output_ids_(TypeId::FromString("framework.construction.output").Raw()),
      socket_reservation_ids_(TypeId::FromString("framework.construction.socket_reservation").Raw())
{
}

void ConstructionService::SetPlacementProvider(const IConstructionPlacementProvider *provider) noexcept
{
    if (placement_provider_ == provider)
        return;
    placement_provider_ = provider;
    ++placement_provider_epoch_;
}

void ConstructionService::SetCostProvider(IConstructionCostProvider *provider) noexcept
{
    if (cost_provider_ == provider)
        return;
    cost_provider_ = provider;
    ++cost_provider_epoch_;
}

foundation::Result<void> ConstructionService::RegisterPlacementDefinition(PlacementDefinition definition)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "construction registry frozen"));
    if (!definition.id.IsValid() || definition.canonical_name.empty() || definition.yaw_step_micro < 0 ||
        placement_definitions_.contains(definition.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.invalid_placement_definition", "invalid placement definition"));
    placement_definitions_.emplace(definition.id, std::move(definition));
    return foundation::Result<void>::Success();
}

foundation::Result<void> ConstructionService::RegisterRecipe(ConstructionRecipe recipe)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "construction registry frozen"));
    if (!recipe.id.IsValid() || recipe.canonical_name.empty() || recipes_.contains(recipe.id) ||
        !FootprintDimensionsValid(recipe.footprint_size_mm))
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.invalid_recipe", "invalid construction recipe"));
    for (const auto &cost : recipe.costs)
        if (!cost.type.IsValid() || cost.amount_micro < 0)
            return foundation::Result<void>::Failure(
                Error("gameplay.construction.invalid_cost", "invalid construction cost"));
    for (const auto &output : recipe.output_templates)
        if (!output.type.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.construction.invalid_output", "invalid construction output template"));
    recipes_.emplace(recipe.id, std::move(recipe));
    return foundation::Result<void>::Success();
}

foundation::Result<void> ConstructionService::RegisterSocket(PlacementSocket socket)
{
    if (!socket.id.IsValid() || !socket.owner.IsValid() || sockets_.contains(socket.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.invalid_socket", "invalid placement socket"));
    Bump();
    socket.revision = revision_;
    sockets_.emplace(socket.id, std::move(socket));
    return foundation::Result<void>::Success();
}

const ConstructionRecipe *ConstructionService::FindRecipe(ConstructionRecipeId id) const noexcept
{
    const auto it = recipes_.find(id);
    return it == recipes_.end() ? nullptr : &it->second;
}

const PlacementDefinition *ConstructionService::FindPlacementDefinition(PlacementRuleId id) const noexcept
{
    const auto it = placement_definitions_.find(id);
    return it == placement_definitions_.end() ? nullptr : &it->second;
}

const PlacementSocket *ConstructionService::FindSocket(PlacementSocketId id) const noexcept
{
    const auto it = sockets_.find(id);
    return it == sockets_.end() ? nullptr : &it->second;
}

foundation::Result<PlacementValidationResult> ConstructionService::ValidatePlacement(const PlacementRequest &request) const
{
    return ValidatePlacementInternal(request, true);
}

foundation::Result<PlacementValidationResult> ConstructionService::ValidatePlacementInternal(
    const PlacementRequest &request, bool count_diagnostics) const
{
    if (count_diagnostics)
        ++validations_;

    PlacementValidationResult result;
    const auto reject = [&](PlacementAvailability availability, std::string_view reason) {
        result.availability = availability;
        result.reasons.push_back({PlacementReasonId::FromString(reason)});
        if (count_diagnostics)
            ++rejections_;
        return foundation::Result<PlacementValidationResult>::Success(result);
    };

    const auto recipe_it = recipes_.find(request.recipe);
    const auto definition_it = placement_definitions_.find(request.placement_rule);
    if (!request.actor.IsValid() || recipe_it == recipes_.end() || definition_it == placement_definitions_.end())
        return reject(PlacementAvailability::InvalidTarget, "construction.invalid_request");

    const auto &recipe = recipe_it->second;
    const auto &definition = definition_it->second;
    if (!definition.allow_pitch && request.target.orientation.pitch_micro != 0)
        return reject(PlacementAvailability::InvalidTarget, "construction.pitch_not_allowed");
    if (!definition.allow_roll && request.target.orientation.roll_micro != 0)
        return reject(PlacementAvailability::InvalidTarget, "construction.roll_not_allowed");
    if (definition.yaw_step_micro > 0 && request.target.orientation.yaw_micro % definition.yaw_step_micro != 0)
        return reject(PlacementAvailability::InvalidTarget, "construction.yaw_not_aligned");

    switch (definition.target_kind)
    {
    case PlacementTargetKind::Free:
        if (!request.target.position)
            return reject(PlacementAvailability::InvalidTarget, "construction.position_required");
        break;
    case PlacementTargetKind::Surface:
        if (!request.target.position || !request.target.attach_to)
            return reject(PlacementAvailability::InvalidTarget, "construction.surface_required");
        break;
    case PlacementTargetKind::Socket:
        if (!request.target.socket)
            return reject(PlacementAvailability::InvalidTarget, "construction.socket_required");
        break;
    case PlacementTargetKind::Attach:
        if (!request.target.attach_to)
            return reject(PlacementAvailability::InvalidTarget, "construction.attach_target_required");
        break;
    case PlacementTargetKind::Area:
        if (!request.target.area)
            return reject(PlacementAvailability::InvalidTarget, "construction.area_required");
        break;
    }

    if (request.target.socket)
    {
        const auto socket_it = sockets_.find(*request.target.socket);
        if (socket_it == sockets_.end())
            return reject(PlacementAvailability::InvalidTarget, "construction.socket_missing");
        if (socket_it->second.state == SocketState::Reserved)
        {
            if (!request.socket_reservation)
                return reject(PlacementAvailability::Blocked, "construction.socket_reserved");
            const auto reservation_it = socket_reservations_by_id_.find(*request.socket_reservation);
            if (reservation_it == socket_reservations_by_id_.end() || reservation_it->second.socket != *request.target.socket ||
                reservation_it->second.owner != request.actor)
                return reject(PlacementAvailability::Blocked, "construction.socket_reservation_mismatch");
        }
        else if (socket_it->second.state != SocketState::Free)
            return reject(PlacementAvailability::Blocked, "construction.socket_blocked");
        if (!socket_it->second.accepted_tags.Values().empty() && !recipe.placement_tags.Values().empty() &&
            !HasAnyExact(socket_it->second.accepted_tags, recipe.placement_tags))
            return reject(PlacementAvailability::InvalidTarget, "construction.socket_tag_mismatch");
    }

    PlacementSemanticProjection projection;
    if (placement_provider_)
    {
        auto projected = placement_provider_->Project(request);
        if (!projected)
            return foundation::Result<PlacementValidationResult>::Failure(projected.GetError());
        projection = std::move(projected).Value();
        result.dependencies_revision = placement_provider_->CurrentRevision();
        if (projection.revision.value > result.dependencies_revision.value)
            result.dependencies_revision = projection.revision;
    }
    else
    {
        result.dependencies_revision = revision_;
        if (!recipe.required_surface_tags.Values().empty() || !recipe.blocked_area_tags.Values().empty() ||
            definition.requires_permission || definition.requires_capability)
            return reject(PlacementAvailability::Unsupported, "construction.semantic_provider_required");
    }

    if (!projection.materialized)
        return reject(PlacementAvailability::NotMaterialized, "construction.target_not_materialized");
    if (!projection.area_allowed)
        return reject(PlacementAvailability::Unavailable, "construction.area_not_allowed");
    if (definition.requires_permission && !projection.permission_granted)
        return reject(PlacementAvailability::RequiresPermission, "construction.permission_required");
    if (definition.requires_capability && !projection.capability_available)
        return reject(PlacementAvailability::RequiresCapability, "construction.capability_required");
    if (!projection.spacing_clear)
        return reject(PlacementAvailability::Blocked, "construction.spacing_blocked");
    if (!HasAllExact(projection.surface_tags, recipe.required_surface_tags))
        return reject(PlacementAvailability::InvalidTarget, "construction.surface_tags_missing");
    if (HasAnyExact(projection.area_tags, recipe.blocked_area_tags))
        return reject(PlacementAvailability::Blocked, "construction.area_tag_blocked");

    result.footprint = projection.footprint;
    if (!result.footprint.volume.IsValid())
    {
        result.footprint.volume.area = request.target.area.value_or(GameplayObjectRef{});
        if (request.target.position)
        {
            const auto &size = recipe.footprint_size_mm;
            if (size.x_mm == 0 && size.y_mm == 0 && size.z_mm == 0)
                return reject(PlacementAvailability::Unsupported, "construction.footprint_missing");
            result.footprint.volume.min = *request.target.position;
            result.footprint.volume.max = {SaturatingAdd(request.target.position->x_mm, size.x_mm),
                                           SaturatingAdd(request.target.position->y_mm, size.y_mm),
                                           SaturatingAdd(request.target.position->z_mm, size.z_mm)};
        }
    }
    result.footprint.payload = recipe.placement_payload;
    result.availability = PlacementAvailability::Available;

    if (recipe.cost_policy == ConstructionCostPolicy::Free)
    {
        return foundation::Result<PlacementValidationResult>::Success(result);
    }
    if (cost_provider_)
    {
        for (const auto &cost : recipe.costs)
        {
            auto affordable = cost_provider_->CanAfford(request.actor, cost, request.context);
            if (!affordable)
                return foundation::Result<PlacementValidationResult>::Failure(affordable.GetError());
            if (!affordable.Value())
                return reject(PlacementAvailability::RequiresResource, "construction.resource_required");
        }
    }
    else if (!recipe.costs.empty())
    {
        return reject(PlacementAvailability::RequiresResource, "construction.cost_provider_required");
    }

    return foundation::Result<PlacementValidationResult>::Success(result);
}

foundation::Result<PlacementPlan> ConstructionService::PreparePlacementPlan(const PlacementRequest &request)
{
    auto validation = ValidatePlacement(request);
    if (!validation)
        return foundation::Result<PlacementPlan>::Failure(validation.GetError());
    if (validation.Value().availability != PlacementAvailability::Available)
    {
        Record({0, ConstructionChangeKind::PlacementRejected, {}, {}, {}, request.target.socket.value_or(PlacementSocketId{}),
                request.actor, revision_, request.context, {}});
        return foundation::Result<PlacementPlan>::Failure(
            Error("gameplay.construction.placement_rejected", "placement is not available"));
    }
    const auto recipe_it = recipes_.find(request.recipe);
    if (recipe_it == recipes_.end())
        return foundation::Result<PlacementPlan>::Failure(Error("gameplay.construction.recipe_missing", "recipe missing"));

    PlacementPlan plan;
    plan.id = PlacementPlanId{plan_ids_.Next()};
    if (!plan.id.IsValid())
        return foundation::Result<PlacementPlan>::Failure(Error("gameplay.construction.id_exhausted", "plan id exhausted"));
    plan.actor = request.actor;
    plan.recipe = request.recipe;
    plan.placement_rule = request.placement_rule;
    plan.target = request.target;
    plan.footprint = validation.Value().footprint;
    plan.reserved_costs = recipe_it->second.costs;
    plan.dependency_revision = validation.Value().dependencies_revision;
    plan.context = request.context;
    plan.placement_provider_epoch = placement_provider_epoch_;
    plan.cost_provider_epoch = cost_provider_epoch_;
    plan.prepared_at = request.context.time;
    plan.expires_at = request.context.time + kDefaultPlanLifetime;
    if (request.target.socket)
    {
        plan.dependency_socket = request.target.socket;
        plan.dependency_socket_reservation = request.socket_reservation;
        const auto *socket = FindSocket(*request.target.socket);
        if (socket)
            plan.dependency_socket_revision = socket->revision;
    }
    plans_.emplace(plan.id, plan);
    Record({0, ConstructionChangeKind::PlacementValidated, plan.id, {}, {}, {}, request.actor, revision_, request.context, {}});
    return foundation::Result<PlacementPlan>::Success(plan);
}

foundation::Result<std::vector<ConstructionCostReservation>> ConstructionService::ReserveCosts(const PlacementPlan &plan)
{
    std::vector<ConstructionCostReservation> reservations;
    const auto recipe_it = recipes_.find(plan.recipe);
    if (recipe_it == recipes_.end())
        return foundation::Result<std::vector<ConstructionCostReservation>>::Failure(
            Error("gameplay.construction.recipe_missing", "recipe missing"));
    if (recipe_it->second.cost_policy == ConstructionCostPolicy::Free || plan.reserved_costs.empty())
        return foundation::Result<std::vector<ConstructionCostReservation>>::Success(std::move(reservations));
    if (!cost_provider_)
        return foundation::Result<std::vector<ConstructionCostReservation>>::Failure(
            Error("gameplay.construction.cost_provider_missing", "construction cost provider missing"));

    reservations.reserve(plan.reserved_costs.size());
    for (const auto &cost : plan.reserved_costs)
    {
        auto reserved = cost_provider_->Reserve(plan.actor, cost, plan.context);
        if (!reserved || !reserved.Value().token.IsValid())
        {
            ReleaseCosts(reservations, plan.context);
            if (!reserved)
                return foundation::Result<std::vector<ConstructionCostReservation>>::Failure(reserved.GetError());
            return foundation::Result<std::vector<ConstructionCostReservation>>::Failure(
                Error("gameplay.construction.invalid_cost_reservation", "cost provider returned invalid reservation"));
        }
        reservations.push_back(std::move(reserved).Value());
    }
    return foundation::Result<std::vector<ConstructionCostReservation>>::Success(std::move(reservations));
}

void ConstructionService::CommitCosts(const std::vector<ConstructionCostReservation> &reservations,
                                      const GameplayContext &context) noexcept
{
    if (!cost_provider_)
        return;
    for (const auto &reservation : reservations)
        cost_provider_->Commit(reservation, context);
}

void ConstructionService::ReleaseCosts(const std::vector<ConstructionCostReservation> &reservations,
                                       const GameplayContext &context) noexcept
{
    if (!cost_provider_)
        return;
    for (auto it = reservations.rbegin(); it != reservations.rend(); ++it)
        cost_provider_->Release(*it, context);
}

std::vector<PlacementOutputOperation> ConstructionService::BuildOutputs(const PlacementPlan &plan,
                                                                        const ConstructionRecipe &recipe,
                                                                        PlacedObjectId placed) const
{
    std::vector<PlacementOutputOperation> outputs;
    if (recipe.result_entity_archetype.IsValid())
    {
        PlacementOutputOperation entity;
        entity.type = PlacementOutputTypeId::FromString("construction.output.create_entity");
        entity.archetype = recipe.result_entity_archetype;
        entity.placed_record = placed;
        entity.payload = recipe.placement_payload;
        outputs.push_back(std::move(entity));
    }
    for (const auto &templ : recipe.output_templates)
    {
        PlacementOutputOperation output;
        output.type = templ.type;
        output.archetype = templ.archetype;
        output.placed_record = placed;
        output.payload = templ.payload;
        if (templ.subject_is_target_area)
            output.subject = plan.target.area.value_or(GameplayObjectRef{});
        outputs.push_back(std::move(output));
    }
    return outputs;
}

foundation::Result<std::vector<PlacementOutputEnvelope>> ConstructionService::StageOutputs(
    PlacementExecutionId execution,
    const std::vector<PlacementOutputOperation> &outputs) const
{
    if (outputs.size() > kOutboxCapacity - outbox_.size())
        return foundation::Result<std::vector<PlacementOutputEnvelope>>::Failure(
            Error("gameplay.construction.outbox_full", "construction outbox full"));

    auto output_ids = output_ids_;
    std::vector<PlacementOutputEnvelope> envelopes;
    envelopes.reserve(outputs.size());
    for (std::size_t i = 0; i < outputs.size(); ++i)
    {
        PlacementOutputEnvelope envelope;
        envelope.id = PlacementOutputId{output_ids.Next()};
        if (!envelope.id.IsValid())
        {
            return foundation::Result<std::vector<PlacementOutputEnvelope>>::Failure(
                Error("gameplay.construction.id_exhausted", "output id exhausted"));
        }
        envelope.execution = execution;
        envelope.ordinal = static_cast<std::uint32_t>(i);
        envelope.operation = outputs[i];
        envelopes.push_back(std::move(envelope));
    }
    return foundation::Result<std::vector<PlacementOutputEnvelope>>::Success(std::move(envelopes));
}

void ConstructionService::QueueOutputs(std::vector<PlacementOutputEnvelope> outputs,
                                       PlacementPlanId plan, ConstructionSiteId site,
                                       GameplayObjectRef actor, GameplayContext context)
{
    for (auto &envelope : outputs)
    {
        output_ids_.Restore({output_ids_.Scope().Raw(), envelope.id.value.Low() + 1});
        const auto output_id = envelope.id;
        const auto execution = envelope.execution;
        outbox_.push_back(std::move(envelope));
        Record({0, ConstructionChangeKind::OutputQueued, plan, execution, site, {}, actor, revision_, context, output_id});
    }
}

foundation::Result<PlacementCommitResult> ConstructionService::CommitPlacement(PlacementPlanId id)
{
    const auto plan_it = plans_.find(id);
    if (plan_it == plans_.end())
        return foundation::Result<PlacementCommitResult>::Failure(Error("gameplay.construction.plan_missing", "placement plan missing"));
    auto &plan = plan_it->second;
    if (plan.state == PlacementPlanState::Committed)
        return foundation::Result<PlacementCommitResult>::Failure(
            Error("gameplay.construction.already_committed", "placement plan already committed"));
    if (plan.state != PlacementPlanState::Prepared)
        return foundation::Result<PlacementCommitResult>::Failure(
            Error("gameplay.construction.plan_not_prepared", "placement plan is not prepared"));
    const auto recipe_it = recipes_.find(plan.recipe);
    const auto definition_it = placement_definitions_.find(plan.placement_rule);
    if (!plan.actor.IsValid() || recipe_it == recipes_.end() || definition_it == placement_definitions_.end())
        return foundation::Result<PlacementCommitResult>::Failure(Error("gameplay.construction.invalid_plan", "invalid placement plan"));
    if (definition_it->second.commit_policy == PlacementCommitPolicy::PreviewOnly)
        return foundation::Result<PlacementCommitResult>::Failure(
            Error("gameplay.construction.preview_only", "preview-only placement cannot be committed"));
    if (plan.placement_provider_epoch != placement_provider_epoch_ || plan.cost_provider_epoch != cost_provider_epoch_)
        return foundation::Result<PlacementCommitResult>::Failure(
            Error("gameplay.construction.stale_plan", "placement providers changed"));

    PlacementRequest request;
    request.actor = plan.actor;
    request.recipe = plan.recipe;
    request.placement_rule = plan.placement_rule;
    request.target = plan.target;
    request.socket_reservation = plan.dependency_socket_reservation;
    request.context = plan.context;
    auto validation = ValidatePlacementInternal(request, false);
    if (!validation)
        return foundation::Result<PlacementCommitResult>::Failure(validation.GetError());
    if (validation.Value().availability != PlacementAvailability::Available ||
        validation.Value().dependencies_revision != plan.dependency_revision)
        return foundation::Result<PlacementCommitResult>::Failure(
            Error("gameplay.construction.stale_plan", "placement dependencies changed"));

    PlacementSocket *socket = nullptr;
    if (plan.dependency_socket)
    {
        socket = FindMutableSocket(*plan.dependency_socket);
        const bool owns_reservation = plan.dependency_socket_reservation &&
            socket_reservation_by_socket_.contains(*plan.dependency_socket) &&
            socket_reservation_by_socket_.at(*plan.dependency_socket) == *plan.dependency_socket_reservation;
        const bool state_ok = socket && (socket->state == SocketState::Free || (socket->state == SocketState::Reserved && owns_reservation));
        if (!state_ok || socket->revision != plan.dependency_socket_revision)
            return foundation::Result<PlacementCommitResult>::Failure(
                Error("gameplay.construction.stale_plan", "placement socket dependency changed"));
    }

    auto reservations = ReserveCosts(plan);
    if (!reservations)
        return foundation::Result<PlacementCommitResult>::Failure(reservations.GetError());

    PlacementCommitResult result;
    result.execution = PlacementExecutionId{execution_ids_.Next()};
    if (!result.execution.IsValid())
    {
        ReleaseCosts(reservations.Value(), plan.context);
        return foundation::Result<PlacementCommitResult>::Failure(Error("gameplay.construction.id_exhausted", "execution id exhausted"));
    }

    std::vector<PlacementOutputEnvelope> staged_outputs;
    if (definition_it->second.commit_policy == PlacementCommitPolicy::Instant)
    {
        result.placed_object = PlacedObjectId{placed_ids_.Next()};
        if (!result.placed_object.IsValid())
        {
            ReleaseCosts(reservations.Value(), plan.context);
            return foundation::Result<PlacementCommitResult>::Failure(Error("gameplay.construction.id_exhausted", "placed object id exhausted"));
        }
        result.outputs = BuildOutputs(plan, recipe_it->second, result.placed_object);
        auto staged = StageOutputs(result.execution, result.outputs);
        if (!staged)
        {
            ReleaseCosts(reservations.Value(), plan.context);
            return foundation::Result<PlacementCommitResult>::Failure(staged.GetError());
        }
        staged_outputs = std::move(staged).Value();
    }
    else
    {
        result.site = ConstructionSiteId{site_ids_.Next()};
        if (!result.site.IsValid())
        {
            ReleaseCosts(reservations.Value(), plan.context);
            return foundation::Result<PlacementCommitResult>::Failure(Error("gameplay.construction.id_exhausted", "site id exhausted"));
        }
    }

    Bump();
    result.revision = revision_;
    plan.state = PlacementPlanState::Committed;

    if (socket)
    {
        socket->state = SocketState::Occupied;
        socket->revision = revision_;
        if (plan.dependency_socket_reservation)
        {
            socket_reservations_by_id_.erase(*plan.dependency_socket_reservation);
            socket_reservation_by_socket_.erase(*plan.dependency_socket);
        }
        Record({0, ConstructionChangeKind::SocketOccupied, plan.id, result.execution, result.site,
                *plan.dependency_socket, socket->owner, revision_, plan.context, {}});
    }

    if (definition_it->second.commit_policy == PlacementCommitPolicy::Instant)
    {
        placed_objects_.push_back({result.placed_object, plan.id, result.execution});
        QueueOutputs(std::move(staged_outputs), plan.id, {}, plan.actor, plan.context);
        Record({0, ConstructionChangeKind::PlacedObjectCreated, plan.id, result.execution, {}, {}, plan.actor,
                revision_, plan.context, {}});
    }
    else
    {
        ConstructionSite site;
        site.id = result.site;
        site.actor = plan.actor;
        site.recipe = plan.recipe;
        site.target = plan.target;
        site.state = ConstructionSiteState::ResourcesCommitted;
        site.revision = revision_;
        site.plan = plan.id;
        sites_.emplace(site.id, site);
        Record({0, ConstructionChangeKind::SiteCreated, plan.id, result.execution, site.id, {}, plan.actor,
                revision_, plan.context, {}});
    }

    CommitCosts(reservations.Value(), plan.context);
    ++committed_;
    Record({0, ConstructionChangeKind::PlacementCommitted, plan.id, result.execution, result.site,
            plan.target.socket.value_or(PlacementSocketId{}), plan.actor, revision_, plan.context, {}});
    return foundation::Result<PlacementCommitResult>::Success(std::move(result));
}

foundation::Result<PlacementCommitResult> ConstructionService::CommitPlacement(const PlacementPlan &plan)
{
    return CommitPlacement(plan.id);
}

foundation::Result<void> ConstructionService::CancelPlacementPlan(PlacementPlanId id, GameplayContext context)
{
    const auto it = plans_.find(id);
    if (it == plans_.end())
        return foundation::Result<void>::Failure(Error("gameplay.construction.plan_missing", "placement plan missing"));
    if (it->second.state == PlacementPlanState::Cancelled)
        return foundation::Result<void>::Success();
    if (it->second.state != PlacementPlanState::Prepared)
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.plan_not_cancellable", "placement plan is not cancellable"));
    Bump();
    it->second.state = PlacementPlanState::Cancelled;
    Record({0, ConstructionChangeKind::PlacementPlanCancelled, id, {}, {}, {}, it->second.actor, revision_, context, {}});
    return foundation::Result<void>::Success();
}

std::size_t ConstructionService::ExpirePlacementPlans(GameplayTimePoint now, GameplayContext context)
{
    std::vector<PlacementPlanId> due;
    for (const auto &[id, plan] : plans_)
        if (plan.state == PlacementPlanState::Prepared && plan.expires_at.ticks != 0 && plan.expires_at <= now)
            due.push_back(id);
    std::sort(due.begin(), due.end());
    for (const auto id : due)
    {
        auto &plan = plans_.at(id);
        Bump();
        plan.state = PlacementPlanState::Expired;
        Record({0, ConstructionChangeKind::PlacementPlanExpired, id, {}, {}, {}, plan.actor, revision_, context, {}});
    }
    return due.size();
}

foundation::Result<ConstructionSiteId> ConstructionService::StartConstructionSite(PlacementPlanId plan_id,
                                                                                  GameplayTimePoint started_at,
                                                                                  GameplayContext context)
{
    const auto plan_it = plans_.find(plan_id);
    if (plan_it == plans_.end() || plan_it->second.state != PlacementPlanState::Committed)
        return foundation::Result<ConstructionSiteId>::Failure(
            Error("gameplay.construction.invalid_plan", "construction site requires a committed placement plan"));
    const auto definition_it = placement_definitions_.find(plan_it->second.placement_rule);
    if (definition_it == placement_definitions_.end() || definition_it->second.commit_policy != PlacementCommitPolicy::CreateSite)
        return foundation::Result<ConstructionSiteId>::Failure(
            Error("gameplay.construction.site_not_required", "placement policy does not create a construction site"));

    ConstructionSite *site = nullptr;
    for (auto &[id, candidate] : sites_)
        if (candidate.plan == plan_id)
        {
            (void)id;
            site = &candidate;
            break;
        }
    if (!site)
        return foundation::Result<ConstructionSiteId>::Failure(Error("gameplay.construction.site_missing", "construction site missing"));
    if (site->state == ConstructionSiteState::UnderConstruction)
        return foundation::Result<ConstructionSiteId>::Success(site->id);
    if (site->state != ConstructionSiteState::ResourcesCommitted && site->state != ConstructionSiteState::Planned)
        return foundation::Result<ConstructionSiteId>::Failure(
            Error("gameplay.construction.site_not_startable", "construction site is not startable"));

    Bump();
    site->state = ConstructionSiteState::UnderConstruction;
    site->started_at = started_at;
    site->revision = revision_;
    Record({0, ConstructionChangeKind::SiteStarted, plan_id, {}, site->id, {}, site->actor, revision_, context, {}});
    return foundation::Result<ConstructionSiteId>::Success(site->id);
}

foundation::Result<ConstructionSiteId> ConstructionService::StartConstructionSite(const PlacementPlan &plan,
                                                                                  GameplayTimePoint started_at,
                                                                                  GameplayContext context)
{
    return StartConstructionSite(plan.id, started_at, context);
}

foundation::Result<void> ConstructionService::PauseConstructionSite(ConstructionSiteId id, GameplayContext context)
{
    auto *site = FindMutableSite(id);
    if (!site || site->state != ConstructionSiteState::UnderConstruction)
        return foundation::Result<void>::Failure(Error("gameplay.construction.site_not_pausable", "construction site is not pausable"));
    Bump();
    site->state = ConstructionSiteState::Paused;
    site->revision = revision_;
    Record({0, ConstructionChangeKind::SitePaused, site->plan, {}, id, {}, site->actor, revision_, context, {}});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ConstructionService::ResumeConstructionSite(ConstructionSiteId id, GameplayContext context)
{
    auto *site = FindMutableSite(id);
    if (!site || site->state != ConstructionSiteState::Paused)
        return foundation::Result<void>::Failure(Error("gameplay.construction.site_not_resumable", "construction site is not resumable"));
    Bump();
    site->state = ConstructionSiteState::UnderConstruction;
    site->revision = revision_;
    Record({0, ConstructionChangeKind::SiteResumed, site->plan, {}, id, {}, site->actor, revision_, context, {}});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ConstructionService::CompleteConstructionSite(ConstructionSiteId id, GameplayContext context)
{
    auto *site = FindMutableSite(id);
    if (!site)
        return foundation::Result<void>::Failure(Error("gameplay.construction.site_missing", "construction site missing"));
    if (site->state == ConstructionSiteState::Completed)
        return foundation::Result<void>::Success();
    if (site->state != ConstructionSiteState::UnderConstruction || site->progress_micro != 1'000'000)
        return foundation::Result<void>::Failure(Error("gameplay.construction.site_not_completable", "construction site is not completable"));
    const auto plan_it = plans_.find(site->plan);
    const auto recipe_it = recipes_.find(site->recipe);
    if (plan_it == plans_.end() || recipe_it == recipes_.end())
        return foundation::Result<void>::Failure(Error("gameplay.construction.site_invalid", "construction site references missing definition"));

    const auto execution = PlacementExecutionId{execution_ids_.Next()};
    const auto placed = PlacedObjectId{placed_ids_.Next()};
    if (!execution.IsValid() || !placed.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.construction.id_exhausted", "construction completion id exhausted"));
    const auto outputs = BuildOutputs(plan_it->second, recipe_it->second, placed);
    auto staged_outputs = StageOutputs(execution, outputs);
    if (!staged_outputs)
        return foundation::Result<void>::Failure(staged_outputs.GetError());

    Bump();
    site->state = ConstructionSiteState::Completed;
    site->completion_execution = execution;
    site->placed_object = placed;
    site->revision = revision_;
    placed_objects_.push_back({placed, site->plan, execution});
    QueueOutputs(std::move(staged_outputs).Value(), site->plan, id, site->actor, context);
    ++completed_sites_;
    Record({0, ConstructionChangeKind::PlacedObjectCreated, site->plan, execution, id, {}, site->actor, revision_, context, {}});
    Record({0, ConstructionChangeKind::SiteCompleted, site->plan, execution, id, {}, site->actor, revision_, context, {}});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ConstructionService::CancelConstructionSite(ConstructionSiteId id, GameplayContext context)
{
    auto *site = FindMutableSite(id);
    if (!site)
        return foundation::Result<void>::Failure(Error("gameplay.construction.site_missing", "construction site missing"));
    if (site->state == ConstructionSiteState::Cancelled)
        return foundation::Result<void>::Success();
    if (!IsLiveSite(site->state))
        return foundation::Result<void>::Failure(Error("gameplay.construction.site_not_cancellable", "construction site is not cancellable"));
    Bump();
    site->state = ConstructionSiteState::Cancelled;
    site->revision = revision_;
    Record({0, ConstructionChangeKind::SiteCancelled, site->plan, {}, id, {}, site->actor, revision_, context, {}});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ConstructionService::DestroyConstructionSite(ConstructionSiteId id, GameplayContext context)
{
    auto *site = FindMutableSite(id);
    if (!site)
        return foundation::Result<void>::Failure(Error("gameplay.construction.site_missing", "construction site missing"));
    if (site->state == ConstructionSiteState::Destroyed)
        return foundation::Result<void>::Success();
    if (site->state == ConstructionSiteState::Cancelled)
        return foundation::Result<void>::Failure(Error("gameplay.construction.site_not_destroyable", "cancelled site cannot be destroyed"));
    Bump();
    site->state = ConstructionSiteState::Destroyed;
    site->revision = revision_;
    Record({0, ConstructionChangeKind::SiteDestroyed, site->plan, site->completion_execution, id, {}, site->actor,
            revision_, context, {}});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ConstructionService::AdvanceConstructionProgress(ConstructionSiteId id,
                                                                                  Fixed delta_micro,
                                                                                  GameplayContext context)
{
    auto *site = FindMutableSite(id);
    if (!site || site->state != ConstructionSiteState::UnderConstruction || delta_micro <= 0)
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.site_not_progressable", "construction site is not progressable"));
    const auto next = SaturatingAdd(site->progress_micro, delta_micro);
    site->progress_micro = next > 1'000'000 ? 1'000'000 : next;
    Bump();
    site->revision = revision_;
    Record({0, ConstructionChangeKind::SiteProgressed, site->plan, {}, id, {}, site->actor, revision_, context, {}});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ConstructionService::FailConstructionSite(ConstructionSiteId id,
                                                                    PlacementReasonId reason,
                                                                    GameplayContext context)
{
    auto *site = FindMutableSite(id);
    if (!site || !IsLiveSite(site->state) || !reason.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.site_not_failable", "construction site is not failable"));
    Bump();
    site->state = ConstructionSiteState::Failed;
    site->terminal_reason = reason;
    site->revision = revision_;
    Record({0, ConstructionChangeKind::SiteFailed, site->plan, {}, id, {}, site->actor, revision_, context, {}});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ConstructionService::PruneTerminalSite(ConstructionSiteId id, GameplayContext context)
{
    const auto it = sites_.find(id);
    if (it == sites_.end())
        return foundation::Result<void>::Failure(Error("gameplay.construction.site_missing", "construction site missing"));
    if (IsLiveSite(it->second.state))
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.site_not_terminal", "construction site is not terminal"));
    const auto has_pending_output = std::any_of(outbox_.begin(), outbox_.end(), [&](const auto &output) {
        return output.execution.IsValid() && output.execution == it->second.completion_execution;
    });
    if (has_pending_output)
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.site_has_pending_outputs", "construction site still has pending outputs"));
    Bump();
    const auto plan = it->second.plan;
    const auto actor = it->second.actor;
    sites_.erase(it);
    Record({0, ConstructionChangeKind::PlacementStateCompacted, plan, {}, id, {}, actor, revision_, context, {}});
    return foundation::Result<void>::Success();
}

foundation::Result<ConstructionSocketReservationId> ConstructionService::ReserveSocket(
    PlacementSocketId id, GameplayObjectRef owner, GameplayContext context)
{
    auto *socket = FindMutableSocket(id);
    if (!socket || socket->state != SocketState::Free || !owner.IsValid())
        return foundation::Result<ConstructionSocketReservationId>::Failure(
            Error("gameplay.construction.socket_unavailable", "socket unavailable"));
    const auto reservation_id = ConstructionSocketReservationId{socket_reservation_ids_.Next()};
    if (!reservation_id.IsValid())
        return foundation::Result<ConstructionSocketReservationId>::Failure(
            Error("gameplay.construction.id_exhausted", "socket reservation id exhausted"));
    Bump();
    socket->state = SocketState::Reserved;
    socket->revision = revision_;
    ConstructionSocketReservation reservation;
    reservation.id = reservation_id;
    reservation.socket = id;
    reservation.owner = owner;
    reservation.socket_revision = revision_;
    reservation.context = context;
    socket_reservations_by_id_.emplace(reservation.id, reservation);
    socket_reservation_by_socket_.emplace(id, reservation.id);
    ++socket_reservations_;
    Record({0, ConstructionChangeKind::SocketReserved, {}, {}, {}, id, owner, revision_, context, {}});
    return foundation::Result<ConstructionSocketReservationId>::Success(reservation.id);
}

foundation::Result<void> ConstructionService::ReleaseSocket(ConstructionSocketReservationId reservation_id,
                                                             GameplayContext context)
{
    const auto reservation_it = socket_reservations_by_id_.find(reservation_id);
    if (reservation_it == socket_reservations_by_id_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.socket_reservation_missing", "socket reservation missing"));
    const auto reservation = reservation_it->second;
    auto *socket = FindMutableSocket(reservation.socket);
    if (!socket || socket->state != SocketState::Reserved ||
        !socket_reservation_by_socket_.contains(reservation.socket) ||
        socket_reservation_by_socket_.at(reservation.socket) != reservation_id)
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.socket_not_releasable", "socket reservation is not releasable"));
    Bump();
    socket->state = SocketState::Free;
    socket->revision = revision_;
    socket_reservation_by_socket_.erase(reservation.socket);
    socket_reservations_by_id_.erase(reservation_id);
    Record({0, ConstructionChangeKind::SocketReleased, {}, {}, {}, reservation.socket, reservation.owner,
            revision_, context, {}});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ConstructionService::OccupySocket(
    PlacementSocketId id, std::optional<ConstructionSocketReservationId> reservation, GameplayContext context)
{
    auto *socket = FindMutableSocket(id);
    if (!socket || socket->state == SocketState::Disabled || socket->state == SocketState::Occupied)
        return foundation::Result<void>::Failure(Error("gameplay.construction.socket_unavailable", "socket unavailable"));
    if (socket->state == SocketState::Reserved)
    {
        if (!reservation || !socket_reservation_by_socket_.contains(id) ||
            socket_reservation_by_socket_.at(id) != *reservation)
            return foundation::Result<void>::Failure(
                Error("gameplay.construction.socket_reservation_mismatch", "socket is reserved by another owner"));
        socket_reservations_by_id_.erase(*reservation);
        socket_reservation_by_socket_.erase(id);
    }
    Bump();
    socket->state = SocketState::Occupied;
    socket->revision = revision_;
    Record({0, ConstructionChangeKind::SocketOccupied, {}, {}, {}, id, socket->owner, revision_, context, {}});
    return foundation::Result<void>::Success();
}

const PlacementPlan *ConstructionService::FindPlan(PlacementPlanId id) const noexcept
{
    const auto it = plans_.find(id);
    return it == plans_.end() ? nullptr : &it->second;
}

const ConstructionSite *ConstructionService::FindSite(ConstructionSiteId id) const noexcept
{
    const auto it = sites_.find(id);
    return it == sites_.end() ? nullptr : &it->second;
}

PlacementSocket *ConstructionService::FindMutableSocket(PlacementSocketId id) noexcept
{
    const auto it = sockets_.find(id);
    return it == sockets_.end() ? nullptr : &it->second;
}

ConstructionSite *ConstructionService::FindMutableSite(ConstructionSiteId id) noexcept
{
    const auto it = sites_.find(id);
    return it == sites_.end() ? nullptr : &it->second;
}

std::vector<ConstructionSite> ConstructionService::FindConstructionSites(GameplayObjectRef actor) const
{
    std::vector<ConstructionSite> result;
    for (const auto &[id, site] : sites_)
    {
        (void)id;
        if (!actor.IsValid() || site.actor == actor)
            result.push_back(site);
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return result;
}

std::vector<PlacementSocket> ConstructionService::FindSocketsForObject(GameplayObjectRef owner) const
{
    std::vector<PlacementSocket> result;
    for (const auto &[id, socket] : sockets_)
    {
        (void)id;
        if (socket.owner == owner)
            result.push_back(socket);
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return result;
}

std::vector<PlacementOutputEnvelope> ConstructionService::PendingOutputs() const
{
    return {outbox_.begin(), outbox_.end()};
}

foundation::Result<PlacementOutputId> ConstructionService::EnqueuePlacedObjectOutput(
    PlacedObjectId placed_object, PlacementOutputOperation operation, GameplayContext context)
{
    const auto placed_it = std::find_if(placed_objects_.begin(), placed_objects_.end(),
                                        [&](const auto& placed) { return placed.id == placed_object; });
    if (placed_it == placed_objects_.end())
        return foundation::Result<PlacementOutputId>::Failure(
            Error("gameplay.construction.placed_object_missing", "placed construction object is missing"));
    if (!operation.type.IsValid())
        return foundation::Result<PlacementOutputId>::Failure(
            Error("gameplay.construction.output_invalid", "placed object output type is invalid"));
    if (operation.placed_record.IsValid() && operation.placed_record != placed_object)
        return foundation::Result<PlacementOutputId>::Failure(
            Error("gameplay.construction.output_placed_object_mismatch",
                  "placed object output references a different construction object"));

    const auto plan_it = plans_.find(placed_it->plan);
    if (plan_it == plans_.end())
        return foundation::Result<PlacementOutputId>::Failure(
            Error("gameplay.construction.plan_missing", "placed construction object references a missing plan"));

    operation.placed_record = placed_object;
    std::vector<PlacementOutputOperation> operations;
    operations.push_back(std::move(operation));
    auto staged = StageOutputs(placed_it->execution, operations);
    if (!staged)
        return foundation::Result<PlacementOutputId>::Failure(staged.GetError());

    auto envelopes = std::move(staged).Value();
    const auto output_id = envelopes.front().id;
    ConstructionSiteId site_id{};
    const auto site_it = std::find_if(sites_.begin(), sites_.end(),
                                      [&](const auto& entry) { return entry.second.placed_object == placed_object; });
    if (site_it != sites_.end())
        site_id = site_it->first;

    Bump();
    QueueOutputs(std::move(envelopes), placed_it->plan, site_id, plan_it->second.actor, context);
    return foundation::Result<PlacementOutputId>::Success(output_id);
}

foundation::Result<void> ConstructionService::AcknowledgeOutput(PlacementOutputId id, GameplayContext context)
{
    const auto it = std::find_if(outbox_.begin(), outbox_.end(), [id](const auto &entry) { return entry.id == id; });
    if (it == outbox_.end())
        return foundation::Result<void>::Failure(Error("gameplay.construction.output_missing", "construction output missing"));
    const auto entry = *it;
    outbox_.erase(it);
    Record({0, ConstructionChangeKind::OutputAcknowledged, {}, entry.execution, {}, {}, {}, revision_, context, id});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ConstructionService::DeadLetterOutput(PlacementOutputId id,
                                                                    PlacementReasonId reason,
                                                                    GameplayContext context)
{
    if (!reason.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.output_reason_invalid", "dead letter reason invalid"));
    const auto it = std::find_if(outbox_.begin(), outbox_.end(), [id](const auto &entry) { return entry.id == id; });
    if (it == outbox_.end())
        return foundation::Result<void>::Failure(Error("gameplay.construction.output_missing", "construction output missing"));
    const auto entry = *it;
    outbox_.erase(it);
    dead_letters_.push_back({entry.id, entry.execution, reason, context});
    if (dead_letters_.size() > kDeadLetterCapacity)
        dead_letters_.pop_front();
    Record({0, ConstructionChangeKind::OutputDeadLettered, {}, entry.execution, {}, {}, {}, revision_, context, id});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ConstructionService::CompactPlacementState(PlacementPlanId plan_id, GameplayContext context)
{
    const auto plan_it = plans_.find(plan_id);
    if (plan_it == plans_.end())
        return foundation::Result<void>::Failure(Error("gameplay.construction.plan_missing", "placement plan missing"));
    if (plan_it->second.state == PlacementPlanState::Prepared)
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.plan_not_terminal", "prepared placement plan cannot be compacted"));
    const bool has_site = std::any_of(sites_.begin(), sites_.end(), [&](const auto &entry) { return entry.second.plan == plan_id; });
    if (has_site)
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.plan_has_site", "placement plan still has a construction site"));
    const bool has_pending = std::any_of(outbox_.begin(), outbox_.end(), [&](const auto &output) {
        return output.operation.placed_record.IsValid() && std::any_of(placed_objects_.begin(), placed_objects_.end(),
            [&](const auto &placed) { return placed.plan == plan_id && placed.id == output.operation.placed_record; });
    });
    if (has_pending)
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.plan_has_pending_outputs", "placement plan still has pending outputs"));
    const auto actor = plan_it->second.actor;
    placed_objects_.erase(std::remove_if(placed_objects_.begin(), placed_objects_.end(),
                                        [&](const auto &placed) { return placed.plan == plan_id; }),
                          placed_objects_.end());
    plans_.erase(plan_it);
    Bump();
    Record({0, ConstructionChangeKind::PlacementStateCompacted, plan_id, {}, {}, {}, actor, revision_, context, {}});
    return foundation::Result<void>::Success();
}

ConstructionSnapshot ConstructionService::CaptureSnapshot() const
{
    ConstructionSnapshot snapshot;
    for (const auto &[id, plan] : plans_)
    {
        (void)id;
        snapshot.plans.push_back(plan);
    }
    for (const auto &[id, site] : sites_)
    {
        (void)id;
        snapshot.sites.push_back(site);
    }
    for (const auto &[id, socket] : sockets_)
    {
        (void)id;
        snapshot.sockets.push_back(socket);
    }
    for (const auto &[id, reservation] : socket_reservations_by_id_)
    {
        (void)id;
        snapshot.socket_reservations.push_back(reservation);
    }
    snapshot.placed_objects = placed_objects_;
    snapshot.pending_outputs.assign(outbox_.begin(), outbox_.end());
    snapshot.dead_letters.assign(dead_letters_.begin(), dead_letters_.end());
    std::sort(snapshot.plans.begin(), snapshot.plans.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.sites.begin(), snapshot.sites.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.socket_reservations.begin(), snapshot.socket_reservations.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.sockets.begin(), snapshot.sockets.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.placed_objects.begin(), snapshot.placed_objects.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.pending_outputs.begin(), snapshot.pending_outputs.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    snapshot.plan_ids = plan_ids_.GetSnapshot();
    snapshot.site_ids = site_ids_.GetSnapshot();
    snapshot.placed_ids = placed_ids_.GetSnapshot();
    snapshot.execution_ids = execution_ids_.GetSnapshot();
    snapshot.output_ids = output_ids_.GetSnapshot();
    snapshot.socket_reservation_ids = socket_reservation_ids_.GetSnapshot();
    snapshot.revision = revision_;
    snapshot.journal.assign(changes_.begin(), changes_.end());
    snapshot.next_change_sequence = next_change_sequence_;
    return snapshot;
}

foundation::Result<void> ConstructionService::RestoreSnapshot(ConstructionSnapshot snapshot)
{
    if (!MonotonicIdGenerator<GameplayObjectId>::IsValidSnapshot(snapshot.plan_ids) ||
        !MonotonicIdGenerator<GameplayObjectId>::IsValidSnapshot(snapshot.site_ids) ||
        !MonotonicIdGenerator<GameplayObjectId>::IsValidSnapshot(snapshot.placed_ids) ||
        !MonotonicIdGenerator<GameplayObjectId>::IsValidSnapshot(snapshot.execution_ids) ||
        !MonotonicIdGenerator<GameplayObjectId>::IsValidSnapshot(snapshot.output_ids) ||
        !MonotonicIdGenerator<GameplayObjectId>::IsValidSnapshot(snapshot.socket_reservation_ids) ||
        snapshot.journal.size() > kChangeJournalCapacity ||
        snapshot.pending_outputs.size() > kOutboxCapacity || snapshot.dead_letters.size() > kDeadLetterCapacity)
        return foundation::Result<void>::Failure(Error("gameplay.construction.restore_invalid", "invalid construction snapshot metadata"));

    std::unordered_map<PlacementPlanId, PlacementPlan, IdHash> new_plans;
    std::unordered_map<ConstructionSiteId, ConstructionSite, IdHash> new_sites;
    std::unordered_map<PlacementSocketId, PlacementSocket, IdHash> new_sockets;
    std::unordered_map<ConstructionSocketReservationId, ConstructionSocketReservation, IdHash> new_reservations;
    std::unordered_map<PlacementSocketId, ConstructionSocketReservationId, IdHash> new_reservation_by_socket;
    std::deque<PlacementOutputEnvelope> new_outbox;
    std::deque<PlacementOutputDeadLetter> new_dead_letters;

    for (const auto &plan : snapshot.plans)
    {
        if (!plan.id.IsValid() || !plan.actor.IsValid() || !recipes_.contains(plan.recipe) ||
            !placement_definitions_.contains(plan.placement_rule) || !new_plans.emplace(plan.id, plan).second)
            return foundation::Result<void>::Failure(Error("gameplay.construction.restore_invalid", "invalid placement plan snapshot"));
    }
    for (const auto &site : snapshot.sites)
    {
        if (!site.id.IsValid() || !site.actor.IsValid() || !recipes_.contains(site.recipe) || !site.plan.IsValid() ||
            !new_plans.contains(site.plan) || site.progress_micro < 0 || site.progress_micro > 1'000'000 ||
            (site.state == ConstructionSiteState::Completed &&
             (!site.completion_execution.IsValid() || !site.placed_object.IsValid() || site.progress_micro != 1'000'000)) ||
            !new_sites.emplace(site.id, site).second)
            return foundation::Result<void>::Failure(Error("gameplay.construction.restore_invalid", "invalid construction site snapshot"));
    }
    for (const auto &socket : snapshot.sockets)
    {
        if (!socket.id.IsValid() || !socket.owner.IsValid() || !new_sockets.emplace(socket.id, socket).second)
            return foundation::Result<void>::Failure(Error("gameplay.construction.restore_invalid", "invalid construction socket snapshot"));
    }
    for (const auto &reservation : snapshot.socket_reservations)
    {
        const auto socket_it = new_sockets.find(reservation.socket);
        if (!reservation.id.IsValid() || !reservation.socket.IsValid() || !reservation.owner.IsValid() ||
            socket_it == new_sockets.end() || socket_it->second.state != SocketState::Reserved ||
            !new_reservations.emplace(reservation.id, reservation).second ||
            !new_reservation_by_socket.emplace(reservation.socket, reservation.id).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.construction.restore_invalid", "invalid construction socket reservation snapshot"));
    }
    for (const auto &placed : snapshot.placed_objects)
    {
        if (!placed.id.IsValid() || !placed.plan.IsValid() || !placed.execution.IsValid() || !new_plans.contains(placed.plan))
            return foundation::Result<void>::Failure(
                Error("gameplay.construction.restore_invalid", "invalid placed object snapshot"));
    }
    for (const auto &output : snapshot.pending_outputs)
    {
        if (!output.id.IsValid() || !output.execution.IsValid() || !output.operation.type.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.construction.restore_invalid", "invalid construction outbox snapshot"));
        if (std::any_of(new_outbox.begin(), new_outbox.end(), [&](const auto &existing) { return existing.id == output.id; }))
            return foundation::Result<void>::Failure(Error("gameplay.construction.restore_invalid", "duplicate construction output id"));
        new_outbox.push_back(output);
    }
    for (const auto &dead : snapshot.dead_letters)
    {
        if (!dead.id.IsValid() || !dead.execution.IsValid() || !dead.reason.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.construction.restore_invalid", "invalid construction dead letter snapshot"));
        new_dead_letters.push_back(dead);
    }

    const auto generator_valid = [](const auto &snapshot_value, std::uint64_t expected_scope, std::uint64_t max_low) {
        if (snapshot_value.scope != expected_scope)
            return false;
        return snapshot_value.next == 0 || snapshot_value.next > max_low;
    };
    std::uint64_t max_plan = 0, max_site = 0, max_placed = 0, max_execution = 0, max_output = 0, max_socket_reservation = 0;
    for (const auto &[id, plan] : new_plans) { (void)plan; max_plan = std::max(max_plan, id.value.Low()); }
    for (const auto &[id, site] : new_sites) { max_site = std::max(max_site, id.value.Low()); max_execution = std::max(max_execution, site.completion_execution.value.Low()); max_placed = std::max(max_placed, site.placed_object.value.Low()); }
    for (const auto &placed : snapshot.placed_objects) { max_placed = std::max(max_placed, placed.id.value.Low()); max_execution = std::max(max_execution, placed.execution.value.Low()); }
    for (const auto &output : snapshot.pending_outputs) { max_output = std::max(max_output, output.id.value.Low()); max_execution = std::max(max_execution, output.execution.value.Low()); }
    for (const auto &dead : snapshot.dead_letters) { max_output = std::max(max_output, dead.id.value.Low()); max_execution = std::max(max_execution, dead.execution.value.Low()); }
    for (const auto &[id, reservation] : new_reservations) { (void)reservation; max_socket_reservation = std::max(max_socket_reservation, id.value.Low()); }
    if (!generator_valid(snapshot.plan_ids, plan_ids_.Scope().Raw(), max_plan) ||
        !generator_valid(snapshot.site_ids, site_ids_.Scope().Raw(), max_site) ||
        !generator_valid(snapshot.placed_ids, placed_ids_.Scope().Raw(), max_placed) ||
        !generator_valid(snapshot.execution_ids, execution_ids_.Scope().Raw(), max_execution) ||
        !generator_valid(snapshot.output_ids, output_ids_.Scope().Raw(), max_output) ||
        !generator_valid(snapshot.socket_reservation_ids, socket_reservation_ids_.Scope().Raw(), max_socket_reservation))
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.restore_invalid", "invalid construction id generator snapshot"));

    if (snapshot.next_change_sequence == 0 &&
        (snapshot.journal.empty() || snapshot.journal.back().sequence != std::numeric_limits<std::uint64_t>::max()))
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.restore_invalid", "exhausted construction journal is missing terminal sequence"));

    std::uint64_t previous_sequence = 0;
    for (const auto &change : snapshot.journal)
    {
        const bool reaches_next = snapshot.next_change_sequence != 0 && change.sequence >= snapshot.next_change_sequence;
        if (change.sequence == 0 || change.sequence <= previous_sequence || reaches_next)
            return foundation::Result<void>::Failure(Error("gameplay.construction.restore_invalid", "invalid construction journal"));
        previous_sequence = change.sequence;
    }

    plans_.swap(new_plans);
    sites_.swap(new_sites);
    sockets_.swap(new_sockets);
    socket_reservations_by_id_.swap(new_reservations);
    socket_reservation_by_socket_.swap(new_reservation_by_socket);
    placed_objects_ = std::move(snapshot.placed_objects);
    outbox_.swap(new_outbox);
    dead_letters_.swap(new_dead_letters);
    plan_ids_.Restore(snapshot.plan_ids);
    site_ids_.Restore(snapshot.site_ids);
    placed_ids_.Restore(snapshot.placed_ids);
    execution_ids_.Restore(snapshot.execution_ids);
    output_ids_.Restore(snapshot.output_ids);
    socket_reservation_ids_.Restore(snapshot.socket_reservation_ids);
    revision_ = snapshot.revision;
    changes_.assign(snapshot.journal.begin(), snapshot.journal.end());
    next_change_sequence_ = snapshot.next_change_sequence;
    return foundation::Result<void>::Success();
}

std::vector<ConstructionChange> ConstructionService::ChangesSince(std::uint64_t sequence) const
{
    return ReadChangesSince(sequence).changes;
}

ConstructionChangeBatch ConstructionService::ReadChangesSince(std::uint64_t sequence) const
{
    ConstructionChangeBatch batch;
    const auto latest = LatestChangeSequence();
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
    for (const auto &change : changes_)
        if (change.sequence > sequence)
            batch.changes.push_back(change);
    return batch;
}

ConstructionDiagnostics ConstructionService::GetDiagnostics() const noexcept
{
    ConstructionDiagnostics diagnostics;
    diagnostics.recipes = recipes_.size();
    diagnostics.placement_definitions = placement_definitions_.size();
    diagnostics.validations = validations_;
    diagnostics.rejections = rejections_;
    diagnostics.committed = committed_;
    diagnostics.socket_reservations = socket_reservations_;
    diagnostics.completed_sites = completed_sites_;
    diagnostics.pending_outputs = outbox_.size();
    diagnostics.dead_lettered_outputs = dead_letters_.size();
    for (const auto &[id, site] : sites_)
    {
        (void)id;
        if (IsLiveSite(site.state))
            ++diagnostics.active_sites;
    }
    return diagnostics;
}

void ConstructionService::Bump() noexcept
{
    if (revision_.value != std::numeric_limits<std::uint64_t>::max())
        ++revision_.value;
}

void ConstructionService::Record(ConstructionChange change)
{
    if (next_change_sequence_ == 0)
        return;
    change.sequence = next_change_sequence_;
    if (next_change_sequence_ == std::numeric_limits<std::uint64_t>::max())
        next_change_sequence_ = 0;
    else
        ++next_change_sequence_;
    changes_.push_back(std::move(change));
    if (changes_.size() > kChangeJournalCapacity)
        changes_.pop_front();
}
} // namespace epidemic::gameplay::construction

