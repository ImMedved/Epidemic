#include "Epidemic/GameFramework/Construction/construction.h"

#include <limits>
using namespace epidemic;
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::construction;

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
} // namespace

int main()
{
    ConstructionService c;
    PlacementProvider provider;
    c.SetPlacementProvider(&provider);
    PlacementDefinition free_def;
    free_def.id = PlacementRuleId::FromString("game.free");
    free_def.canonical_name = "game.free";
    free_def.target_kind = PlacementTargetKind::Free;
    free_def.commit_policy = PlacementCommitPolicy::CreateSite;
    if (!c.RegisterPlacementDefinition(free_def))
        return 1;
    PlacementDefinition socket_def;
    socket_def.id = PlacementRuleId::FromString("game.socket");
    socket_def.canonical_name = "game.socket";
    socket_def.target_kind = PlacementTargetKind::Socket;
    if (!c.RegisterPlacementDefinition(socket_def))
        return 2;
    ConstructionRecipe recipe;
    recipe.id = ConstructionRecipeId::FromString("game.bridge");
    recipe.canonical_name = "game.bridge";
    recipe.result_entity_archetype = TypeId::FromString("game.bridge.entity");
    recipe.footprint_size_mm = {100, 100, 100};
    if (!c.RegisterRecipe(recipe))
        return 3;
    GameplayObjectRef wall{GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("wall")};
    PlacementSocket sock;
    sock.id = PlacementSocketId::FromString("socket.wall.1");
    sock.owner = wall;
    if (!c.RegisterSocket(sock))
        return 9;
    c.Freeze();
    PlacementSocket late_socket = sock;
    late_socket.id = PlacementSocketId::FromString("socket.wall.late");
    if (c.RegisterSocket(late_socket))
        return 54;
    GameplayObjectRef actor{GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("actor")};
    GameplayObjectRef area{GameplayDomainId::FromString("test.area"), GameplayObjectId::FromString("river")};
    PlacementRequest req;
    req.actor = actor;
    req.recipe = recipe.id;
    req.placement_rule = free_def.id;
    req.target.position = WorldPosition{1, 2, 3};
    req.target.area = area;
    auto val = c.ValidatePlacement(req);
    if (!val || val.Value().availability != PlacementAvailability::Available)
        return 4;
    auto plan = c.PreparePlacementPlan(req);
    if (!plan)
        return 5;
    auto committed = c.CommitPlacement(plan.Value());
    if (!committed || !committed.Value().site.IsValid() || !committed.Value().outputs.empty())
        return 8;
    auto site = c.StartConstructionSite(plan.Value());
    if (!site)
        return 6;
    if (!c.AdvanceConstructionProgress(site.Value(), 1'000'000))
        return 37;
    if (!c.CompleteConstructionSite(site.Value()))
        return 7;
    const auto outputs = c.PendingOutputs();
    if (outputs.empty())
        return 18;
    if (outputs.front().operation.subject.IsValid())
        return 19;
    if (!outputs.front().operation.placed_record.IsValid())
        return 20;
    if (!c.DeadLetterOutput(outputs.front().id, PlacementReasonId::FromString("test.unsupported")))
        return 38;
    if (!c.PruneTerminalSite(site.Value()))
        return 39;
    if (!c.CompactPlacementState(plan.Value().id))
        return 40;
    if (c.CommitPlacement(plan.Value()))
        return 21;
    PlacementRequest sreq;
    sreq.actor = actor;
    sreq.recipe = recipe.id;
    sreq.placement_rule = socket_def.id;
    sreq.target.socket = sock.id;
    auto stale = c.PreparePlacementPlan(sreq);
    if (!stale)
        return 22;
    auto socket_reservation = c.ReserveSocket(sock.id, actor);
    if (!socket_reservation)
        return 23;
    if (c.CommitPlacement(stale.Value()))
        return 24;
    if (!c.ReleaseSocket(socket_reservation.Value()))
        return 25;
    auto spl = c.PreparePlacementPlan(sreq);
    if (!spl)
        return 10;
    if (!c.CommitPlacement(spl.Value()))
        return 11;
    if (c.FindSocket(sock.id)->state != SocketState::Occupied)
        return 12;
    auto snap = c.CaptureSnapshot();
    ConstructionService restored;
    if (!restored.RegisterPlacementDefinition(free_def))
        return 15;
    if (!restored.RegisterPlacementDefinition(socket_def))
        return 16;
    if (!restored.RegisterRecipe(recipe))
        return 17;
    restored.Freeze();
    if (!restored.RestoreSnapshot(std::move(snap)))
        return 13;
    if (restored.FindSocket(sock.id)->state != SocketState::Occupied)
        return 14;
    if (!restored.ReadChangesSince(restored.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required)
        return 46;
    ConstructionService empty_journal;
    if (!empty_journal.ReadChangesSince(empty_journal.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required)
        return 53;

    auto construction_journal_seed = restored.CaptureSnapshot();
    construction_journal_seed.journal.clear();
    construction_journal_seed.next_change_sequence = std::numeric_limits<std::uint64_t>::max();
    PlacementSocket journal_socket_a = sock;
    journal_socket_a.id = PlacementSocketId::FromString("test.journal.socket.a");
    journal_socket_a.state = SocketState::Free;
    journal_socket_a.revision = construction_journal_seed.revision;
    PlacementSocket journal_socket_b = journal_socket_a;
    journal_socket_b.id = PlacementSocketId::FromString("test.journal.socket.b");
    construction_journal_seed.sockets.push_back(journal_socket_a);
    construction_journal_seed.sockets.push_back(journal_socket_b);
    if (!restored.RestoreSnapshot(construction_journal_seed))
        return 47;
    const auto epoch_before_rollover = restored.LatestChangeCursor().epoch;
    auto journal_reservation = restored.ReserveSocket(journal_socket_a.id, actor);
    if (!journal_reservation)
        return 48;
    const auto construction_exhausted = restored.CaptureSnapshot();
    if (construction_exhausted.change_epoch <= epoch_before_rollover || construction_exhausted.next_change_sequence != 2 ||
        construction_exhausted.journal.size() != 1 || construction_exhausted.journal.front().sequence != 1)
        return 49;
    if (!restored.ReadChangesSince(ChangeCursor{epoch_before_rollover, 0}).snapshot_required)
        return 50;
    ConstructionService construction_exhausted_restore;
    if (!construction_exhausted_restore.RegisterPlacementDefinition(free_def) ||
        !construction_exhausted_restore.RegisterPlacementDefinition(socket_def) ||
        !construction_exhausted_restore.RegisterRecipe(recipe))
        return 51;
    construction_exhausted_restore.Freeze();
    if (!construction_exhausted_restore.RestoreSnapshot(construction_exhausted))
        return 52;

    ConstructionService staged;
    if (!staged.RegisterPlacementDefinition(free_def))
        return 26;
    PlacementDefinition instant_def;
    instant_def.id = PlacementRuleId::FromString("game.instant");
    instant_def.canonical_name = "game.instant";
    instant_def.target_kind = PlacementTargetKind::Free;
    instant_def.commit_policy = PlacementCommitPolicy::Instant;
    if (!staged.RegisterPlacementDefinition(instant_def))
        return 27;
    ConstructionRecipe staged_recipe;
    staged_recipe.id = ConstructionRecipeId::FromString("game.instant.bridge");
    staged_recipe.canonical_name = "game.instant.bridge";
    staged_recipe.result_entity_archetype = TypeId::FromString("game.instant.bridge.entity");
    staged_recipe.footprint_size_mm = {100, 100, 100};
    PlacementOutputTemplate extra_output;
    extra_output.type = PlacementOutputTypeId::FromString("construction.output.custom");
    extra_output.subject_is_target_area = true;
    staged_recipe.output_templates.push_back(extra_output);
    if (!staged.RegisterRecipe(staged_recipe))
        return 28;
    staged.Freeze();
    auto staged_snapshot = staged.CaptureSnapshot();
    staged_snapshot.output_ids.next = std::numeric_limits<std::uint64_t>::max();
    ConstructionService exhausted;
    if (!exhausted.RegisterPlacementDefinition(free_def))
        return 29;
    if (!exhausted.RegisterPlacementDefinition(instant_def))
        return 30;
    if (!exhausted.RegisterRecipe(staged_recipe))
        return 31;
    exhausted.Freeze();
    if (!exhausted.RestoreSnapshot(std::move(staged_snapshot)))
        return 32;
    PlacementRequest instant_req;
    instant_req.actor = actor;
    instant_req.recipe = staged_recipe.id;
    instant_req.placement_rule = instant_def.id;
    instant_req.target.position = WorldPosition{4, 5, 6};
    instant_req.target.area = area;
    auto instant_plan = exhausted.PreparePlacementPlan(instant_req);
    if (!instant_plan)
        return 33;
    auto instant_commit = exhausted.CommitPlacement(instant_plan.Value());
    if (instant_commit || !instant_commit.GetError().HasCode("gameplay.construction.id_exhausted"))
        return 34;
    const auto *preserved_plan = exhausted.FindPlan(instant_plan.Value().id);
    if (preserved_plan == nullptr || preserved_plan->state != PlacementPlanState::Prepared)
        return 35;
    if (!exhausted.PendingOutputs().empty())
        return 36;

    ConstructionService expiring;
    expiring.SetPlacementProvider(&provider);
    if (!expiring.RegisterPlacementDefinition(free_def) || !expiring.RegisterRecipe(recipe))
        return 41;
    expiring.Freeze();
    PlacementRequest expiring_req = req;
    expiring_req.context.time = {100};
    auto expiring_plan = expiring.PreparePlacementPlan(expiring_req);
    if (!expiring_plan)
        return 42;
    if (expiring.ExpirePlacementPlans({399}) != 0)
        return 43;
    if (expiring.ExpirePlacementPlans({400}) != 1)
        return 44;
    if (expiring.CommitPlacement(expiring_plan.Value()))
        return 45;

    // CST-03: time overflow is rejected before the placement-plan generator advances.
    PlacementRequest overflow_plan_request = req;
    overflow_plan_request.context.time = GameplayTimePoint{std::numeric_limits<std::int64_t>::max()};
    const auto plan_generator_before = expiring.CaptureSnapshot().plan_ids;
    auto overflow_plan = expiring.PreparePlacementPlan(overflow_plan_request);
    const auto plan_generator_after = expiring.CaptureSnapshot().plan_ids;
    if (overflow_plan || !overflow_plan.GetError().HasCode("gameplay.time_overflow") ||
        plan_generator_before.scope != plan_generator_after.scope || plan_generator_before.next != plan_generator_after.next)
        return 55;

    return 0;
}


