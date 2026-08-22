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
      output_ids_(TypeId::FromString("framework.construction.output").Raw())
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
        if (socket_it->second.state != SocketState::Free)
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
    if (request.target.socket)
    {
        plan.dependency_socket = request.target.socket;
        const auto *socket = FindSocket(*request.target.socket);
        if (socket)
            plan.dependency_socket_revision = socket->revision;
    }
    plans_.emplace(plan.id, plan);
    Record({0, ConstructionChangeKind::PlacementValidated, plan.id, {}, {}, {}, request.actor, revision_, request.context, {}});
    return foundation::Result<PlacementPlan>::Success(plan);
}

foundation::Result<std::vector<ConstructionCostReservation>> ConstructionService::ReserveCosts(
    const PlacementPlan &plan, ConstructionCostPolicy policy)
{
    std::vector<ConstructionCostReservation> reservations;
    if (policy == ConstructionCostPolicy::Free || policy == ConstructionCostPolicy::ValidateOnly || plan.reserved_costs.empty())
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

foundation::Result<PlacementCommitResult> ConstructionService::CommitPlacement(PlacementPlanId id,
                                                                               ConstructionCostPolicy cost_policy)
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
        if (!socket || socket->revision != plan.dependency_socket_revision || socket->state != SocketState::Free)
            return foundation::Result<PlacementCommitResult>::Failure(
                Error("gameplay.construction.stale_plan", "placement socket dependency changed"));
    }

    auto reservations = ReserveCosts(plan, cost_policy);
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
        Record({0, ConstructionChangeKind::SocketOccupied, plan.id, result.execution, result.site,
                *plan.dependency_socket, socket->owner, revision_, plan.context, {}});
    }

    if (definition_it->second.commit_policy == PlacementCommitPolicy::Instant)
    {
        placed_objects_.push_back(result.placed_object);
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

foundation::Result<PlacementCommitResult> ConstructionService::CommitPlacement(const PlacementPlan &plan,
                                                                               ConstructionCostPolicy cost_policy)
{
    return CommitPlacement(plan.id, cost_policy);
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
    if (site->state != ConstructionSiteState::UnderConstruction)
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
    site->progress_micro = 1'000'000;
    site->completion_execution = execution;
    site->placed_object = placed;
    site->revision = revision_;
    placed_objects_.push_back(placed);
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

foundation::Result<void> ConstructionService::ReserveSocket(PlacementSocketId id, GameplayContext context)
{
    auto *socket = FindMutableSocket(id);
    if (!socket || socket->state != SocketState::Free)
        return foundation::Result<void>::Failure(Error("gameplay.construction.socket_unavailable", "socket unavailable"));
    Bump();
    socket->state = SocketState::Reserved;
    socket->revision = revision_;
    ++socket_reservations_;
    Record({0, ConstructionChangeKind::SocketReserved, {}, {}, {}, id, socket->owner, revision_, context, {}});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ConstructionService::ReleaseSocket(PlacementSocketId id, GameplayContext context)
{
    auto *socket = FindMutableSocket(id);
    if (!socket || socket->state == SocketState::Occupied || socket->state == SocketState::Disabled)
        return foundation::Result<void>::Failure(
            Error("gameplay.construction.socket_not_releasable", "socket not releasable"));
    if (socket->state == SocketState::Free)
        return foundation::Result<void>::Success();
    Bump();
    socket->state = SocketState::Free;
    socket->revision = revision_;
    Record({0, ConstructionChangeKind::SocketReleased, {}, {}, {}, id, socket->owner, revision_, context, {}});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ConstructionService::OccupySocket(PlacementSocketId id, GameplayContext context)
{
    auto *socket = FindMutableSocket(id);
    if (!socket || socket->state == SocketState::Disabled || socket->state == SocketState::Occupied)
        return foundation::Result<void>::Failure(Error("gameplay.construction.socket_unavailable", "socket unavailable"));
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
        snapshot.active_sites.push_back(site);
    }
    for (const auto &[id, socket] : sockets_)
    {
        (void)id;
        snapshot.sockets.push_back(socket);
    }
    snapshot.placed_objects = placed_objects_;
    snapshot.pending_outputs.assign(outbox_.begin(), outbox_.end());
    std::sort(snapshot.plans.begin(), snapshot.plans.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.active_sites.begin(), snapshot.active_sites.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.sockets.begin(), snapshot.sockets.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.placed_objects.begin(), snapshot.placed_objects.end());
    std::sort(snapshot.pending_outputs.begin(), snapshot.pending_outputs.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    snapshot.plan_ids = plan_ids_.GetSnapshot();
    snapshot.site_ids = site_ids_.GetSnapshot();
    snapshot.placed_ids = placed_ids_.GetSnapshot();
    snapshot.execution_ids = execution_ids_.GetSnapshot();
    snapshot.output_ids = output_ids_.GetSnapshot();
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
        snapshot.next_change_sequence == 0 || snapshot.journal.size() > kChangeJournalCapacity ||
        snapshot.pending_outputs.size() > kOutboxCapacity)
        return foundation::Result<void>::Failure(Error("gameplay.construction.restore_invalid", "invalid construction snapshot metadata"));

    std::unordered_map<PlacementPlanId, PlacementPlan, IdHash> new_plans;
    std::unordered_map<ConstructionSiteId, ConstructionSite, IdHash> new_sites;
    std::unordered_map<PlacementSocketId, PlacementSocket, IdHash> new_sockets;
    std::deque<PlacementOutputEnvelope> new_outbox;

    for (const auto &plan : snapshot.plans)
    {
        if (!plan.id.IsValid() || !plan.actor.IsValid() || !recipes_.contains(plan.recipe) ||
            !placement_definitions_.contains(plan.placement_rule) || !new_plans.emplace(plan.id, plan).second)
            return foundation::Result<void>::Failure(Error("gameplay.construction.restore_invalid", "invalid placement plan snapshot"));
    }
    for (const auto &site : snapshot.active_sites)
    {
        if (!site.id.IsValid() || !site.actor.IsValid() || !recipes_.contains(site.recipe) || !site.plan.IsValid() ||
            !new_plans.contains(site.plan) || !new_sites.emplace(site.id, site).second)
            return foundation::Result<void>::Failure(Error("gameplay.construction.restore_invalid", "invalid construction site snapshot"));
    }
    for (const auto &socket : snapshot.sockets)
    {
        if (!socket.id.IsValid() || !socket.owner.IsValid() || !new_sockets.emplace(socket.id, socket).second)
            return foundation::Result<void>::Failure(Error("gameplay.construction.restore_invalid", "invalid construction socket snapshot"));
    }
    for (const auto &output : snapshot.pending_outputs)
    {
        if (!output.id.IsValid() || !output.execution.IsValid() || !output.operation.type.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.construction.restore_invalid", "invalid construction outbox snapshot"));
        if (std::any_of(new_outbox.begin(), new_outbox.end(), [&](const auto &existing) { return existing.id == output.id; }))
            return foundation::Result<void>::Failure(Error("gameplay.construction.restore_invalid", "duplicate construction output id"));
        new_outbox.push_back(output);
    }
    std::uint64_t previous_sequence = 0;
    for (const auto &change : snapshot.journal)
    {
        if (change.sequence == 0 || change.sequence <= previous_sequence || change.sequence >= snapshot.next_change_sequence)
            return foundation::Result<void>::Failure(Error("gameplay.construction.restore_invalid", "invalid construction journal"));
        previous_sequence = change.sequence;
    }

    plans_.swap(new_plans);
    sites_.swap(new_sites);
    sockets_.swap(new_sockets);
    placed_objects_ = std::move(snapshot.placed_objects);
    outbox_.swap(new_outbox);
    plan_ids_.Restore(snapshot.plan_ids);
    site_ids_.Restore(snapshot.site_ids);
    placed_ids_.Restore(snapshot.placed_ids);
    execution_ids_.Restore(snapshot.execution_ids);
    output_ids_.Restore(snapshot.output_ids);
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
    batch.oldest_available_sequence = changes_.empty() ? next_change_sequence_ : changes_.front().sequence;
    if (!changes_.empty() && sequence + 1 < changes_.front().sequence)
        batch.snapshot_required = true;
    if (!batch.snapshot_required)
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
    change.sequence = next_change_sequence_++;
    changes_.push_back(std::move(change));
    if (changes_.size() > kChangeJournalCapacity)
        changes_.pop_front();
}
} // namespace epidemic::gameplay::construction

