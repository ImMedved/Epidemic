#include "Epidemic/GameFramework/WorldIntegration/world_adapters.h"

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace epidemic::gameplay::world_integration
{
namespace
{
[[nodiscard]] constexpr std::uint64_t MixRevision(std::uint64_t value) noexcept
{
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31u);
}

[[nodiscard]] constexpr Revision CompositeRevision(Revision first, Revision second) noexcept
{
    // Queries currently expose one scalar revision. Preserve both owner dependencies in a stable
    // composite stamp so a change to either owner invalidates equality-based query caches.
    return Revision{MixRevision(first.Raw()) ^ (MixRevision(second.Raw()) + 0x517cc1b727220a95ull)};
}

[[nodiscard]] constexpr GameplayObjectRef SubjectForWorldChange(const world::WorldChange& change) noexcept
{
    switch (change.kind)
    {
    case world::WorldChangeKind::AlterationCreated:
    case world::WorldChangeKind::AlterationUpdated:
    case world::WorldChangeKind::AlterationExpired:
    case world::WorldChangeKind::AlterationRemoved:
    case world::WorldChangeKind::AlterationCompacted:
        return change.alteration.IsValid()
                   ? GameplayObjectRef{world::WorldService::Domain(), change.alteration.value}
                   : GlobalWorldScope();
    case world::WorldChangeKind::FeatureChanged:
    case world::WorldChangeKind::FeatureRemoved:
        return change.feature.IsValid() ? GameplayObjectRef{world::WorldService::Domain(), change.feature.value}
                                        : GlobalWorldScope();
    case world::WorldChangeKind::ObjectPlaced:
    case world::WorldChangeKind::ObjectPlacementRemoved:
        return change.object.IsValid() ? change.object : GlobalWorldScope();
    }
    return GlobalWorldScope();
}
}
foundation::Result<void> WorldQueryAdapter::RegisterProviders()
{
    const queries::QueryProviderCapabilities caps{true, false, false, false};
    auto a = queries_.RegisterProvider<AreasAtPositionQuery>(
        "framework.query.world.areas_at", caps, [this](const AreasAtPositionQuery &q, const queries::QueryContext &) {
            auto v = world_.FindAreasAt(q.position);
            const auto n = v.size();
            return foundation::Result<queries::QueryResponse<AreasAtPositionQuery::ResultType>>::Success(
                {std::move(v), {{}, world_.CurrentRevision(), queries::QueryCoverage::Complete, n, n, true}});
        });
    if (!a)
        return a;
    auto b = queries_.RegisterProvider<AlterationsInAreaQuery>(
        "framework.query.world.alterations", caps,
        [this](const AlterationsInAreaQuery &q, const queries::QueryContext &) {
            auto v = world_.FindAlterations(q.area, q.type);
            auto n = v.size();
            return foundation::Result<queries::QueryResponse<AlterationsInAreaQuery::ResultType>>::Success(
                {std::move(v), {{}, world_.CurrentRevision(), queries::QueryCoverage::Complete, n, n, true}});
        });
    if (!b)
        return b;
    auto c = queries_.RegisterProvider<EnvironmentSampleQuery>(
        "framework.query.environment.sample", caps,
        [this](const EnvironmentSampleQuery &q, const queries::QueryContext &) {
            // Cross-owner current reads are valid only inside the composition gameplay read phase.
            // Keep a defensive revision guard so re-entrant/interleaved mutation cannot return a
            // response whose dependency stamp describes a different state than its value.
            const auto world_revision = world_.CurrentRevision();
            const auto environment_revision = environment_.CurrentRevision();
            auto scopes = q.scopes;
            const world::WorldPosition world_position{q.position.x_mm, q.position.y_mm, q.position.z_mm};
            for (const auto &area : world_.FindAreasAt(world_position))
            {
                scopes.push_back(GameplayObjectRef{world::WorldService::Domain(), area.id.value});
                for (const auto region : area.regions)
                {
                    scopes.push_back(GameplayObjectRef{world::WorldService::Domain(), region.value});
                }
            }
            std::sort(scopes.begin(), scopes.end());
            scopes.erase(std::unique(scopes.begin(), scopes.end()), scopes.end());
            auto sample = environment_.Sample(q.position, q.time, scopes);
            if (!sample)
                return foundation::Result<queries::QueryResponse<EnvironmentSampleQuery::ResultType>>::Failure(
                    sample.GetError());
            if (world_.CurrentRevision() != world_revision || environment_.CurrentRevision() != environment_revision)
                return foundation::Result<queries::QueryResponse<EnvironmentSampleQuery::ResultType>>::Failure(
                    foundation::Error::Create("gameplay.world_integration.incoherent_read",
                                              "World or Environment changed during a cross-owner query"));
            const auto dependency_revision = CompositeRevision(world_revision, environment_revision);
            return foundation::Result<queries::QueryResponse<EnvironmentSampleQuery::ResultType>>::Success(
                {std::move(sample).Value(),
                 {{}, dependency_revision, queries::QueryCoverage::Complete, 1, 1, true}});
        });
    if (!c)
        return c;
    return queries_.RegisterProvider<ActiveInteractionsQuery>(
        "framework.query.interaction.active", caps,
        [this](const ActiveInteractionsQuery &q, const queries::QueryContext &) {
            auto v = interaction_.FindActive(q.actor);
            auto n = v.size();
            return foundation::Result<queries::QueryResponse<ActiveInteractionsQuery::ResultType>>::Success(
                {std::move(v), {{}, interaction_.CurrentRevision(), queries::QueryCoverage::Complete, n, n, true}});
        });
}
foundation::Result<void> WorldFactsAdapter::RegisterContracts()
{
    auto w = facts_.RegisterEventType<world::WorldChange>("framework.world.changed", world::WorldService::Domain(),
                                                          facts::HistoryPolicy::Recent, 4096);
    if (!w)
        return foundation::Result<void>::Failure(w.GetError());
    world_event_ = w.Value();
    auto e = facts_.RegisterEventType<environment::EnvironmentChange>(
        "framework.environment.changed", environment::EnvironmentService::Domain(), facts::HistoryPolicy::Recent, 4096);
    if (!e)
        return foundation::Result<void>::Failure(e.GetError());
    environment_event_ = e.Value();
    auto i = facts_.RegisterEventType<interaction::InteractionChange>(
        "framework.interaction.changed", interaction::InteractionService::Domain(), facts::HistoryPolicy::Recent, 4096);
    if (!i)
        return foundation::Result<void>::Failure(i.GetError());
    interaction_event_ = i.Value();
    return foundation::Result<void>::Success();
}
foundation::Result<std::uint64_t> WorldFactsAdapter::PublishPending(GameplayContext context)
{
    std::uint64_t n = 0;
    const auto producer = ProducerId::FromString("framework.world_integration");
    const auto world_changes = world_.ReadChangesSince(wc_);
    if (world_changes.snapshot_required)
        return foundation::Result<std::uint64_t>::Failure(
            foundation::Error::Create("gameplay.world_integration.snapshot_required",
                                      "world change journal no longer contains the requested sequence"));
    for (const auto &c : world_changes.changes)
    {
        auto r = facts_.Publish(world_event_, c.context.tick.IsValid() ? c.context : context,
                                SubjectForWorldChange(c), c, producer);
        if (!r)
            return foundation::Result<std::uint64_t>::Failure(r.GetError());
        wc_ = {world_changes.latest_cursor.epoch, c.sequence};
        ++n;
    }
    const auto environment_changes = environment_.ReadChangesSince(ec_);
    if (environment_changes.snapshot_required)
        return foundation::Result<std::uint64_t>::Failure(foundation::Error::Create(
            "gameplay.world_integration.environment_snapshot_required",
            "environment change journal no longer contains the requested sequence"));
    for (const auto &c : environment_changes.changes)
    {
        auto r =
            facts_.Publish(environment_event_, c.context.tick.IsValid() ? c.context : context,
                           GameplayObjectRef{environment::EnvironmentService::Domain(), c.layer.value}, c, producer);
        if (!r)
            return foundation::Result<std::uint64_t>::Failure(r.GetError());
        ec_ = {environment_changes.latest_cursor.epoch, c.sequence};
        ++n;
    }
    const auto interaction_changes = interaction_.ReadChangesSince(ic_);
    if (interaction_changes.snapshot_required)
        return foundation::Result<std::uint64_t>::Failure(foundation::Error::Create(
            "gameplay.world_integration.interaction_snapshot_required",
            "interaction change journal no longer contains the requested sequence"));
    for (const auto &c : interaction_changes.changes)
    {
        auto r =
            facts_.Publish(interaction_event_, c.context.tick.IsValid() ? c.context : context, c.actor, c, producer);
        if (!r)
            return foundation::Result<std::uint64_t>::Failure(r.GetError());
        ic_ = {interaction_changes.latest_cursor.epoch, c.sequence};
        ++n;
    }
    if (!wc_.IsValid()) wc_.epoch = world_changes.latest_cursor.epoch;
    if (!ec_.IsValid()) ec_.epoch = environment_changes.latest_cursor.epoch;
    if (!ic_.IsValid()) ic_.epoch = interaction_changes.latest_cursor.epoch;
    return foundation::Result<std::uint64_t>::Success(n);
}

WorldFactsCheckpoint WorldFactsAdapter::CaptureCheckpoint() const noexcept
{
    return WorldFactsCheckpoint{WorldFactsCheckpoint::kSchemaVersion,
                                wc_.sequence,
                                ec_.sequence,
                                ic_.sequence,
                                wc_.epoch,
                                ec_.epoch,
                                ic_.epoch,
                                world_.CurrentRevision(),
                                environment_.CurrentRevision(),
                                interaction_.CurrentRevision()};
}

foundation::Result<void> WorldFactsAdapter::RestoreCheckpoint(WorldFactsCheckpoint checkpoint)
{
    if (checkpoint.schema_version != WorldFactsCheckpoint::kSchemaVersion)
        return foundation::Result<void>::Failure(foundation::Error::Create(
            "gameplay.world_integration.checkpoint_version", "unsupported WorldFacts checkpoint schema version"));

    const auto restore_cursor = [](ChangeCursor saved_cursor, Revision saved_revision, Revision current_revision,
                                   ChangeCursor latest_cursor, bool snapshot_required, std::string_view code)
        -> foundation::Result<ChangeCursor> {
        if (!snapshot_required && saved_cursor.epoch == latest_cursor.epoch &&
            saved_cursor.sequence <= latest_cursor.sequence)
        {
            return foundation::Result<ChangeCursor>::Success(saved_cursor);
        }

        // A restored owner deliberately starts a new journal epoch. Equal authoritative revisions mean
        // the persisted Facts snapshot already represents pre-save history, so consume the new epoch from zero.
        if (latest_cursor.sequence == 0 && current_revision == saved_revision)
            return foundation::Result<ChangeCursor>::Success({latest_cursor.epoch, 0});

        return foundation::Result<ChangeCursor>::Failure(foundation::Error::Create(
            code, "saved cursor belongs to an incompatible owner journal; historical events require reconciliation"));
    };

    const ChangeCursor saved_world{checkpoint.world_epoch, checkpoint.world_sequence};
    const auto world_batch = world_.ReadChangesSince(saved_world);
    auto world_cursor = restore_cursor(saved_world, checkpoint.world_revision, world_.CurrentRevision(),
                                       world_.LatestChangeCursor(), world_batch.snapshot_required,
                                       "gameplay.world_integration.world_checkpoint_gap");
    if (!world_cursor)
        return foundation::Result<void>::Failure(world_cursor.GetError());

    const ChangeCursor saved_environment{checkpoint.environment_epoch, checkpoint.environment_sequence};
    const auto environment_batch = environment_.ReadChangesSince(saved_environment);
    auto environment_cursor =
        restore_cursor(saved_environment, checkpoint.environment_revision, environment_.CurrentRevision(),
                       environment_.LatestChangeCursor(), environment_batch.snapshot_required,
                       "gameplay.world_integration.environment_checkpoint_gap");
    if (!environment_cursor)
        return foundation::Result<void>::Failure(environment_cursor.GetError());

    const ChangeCursor saved_interaction{checkpoint.interaction_epoch, checkpoint.interaction_sequence};
    const auto interaction_batch = interaction_.ReadChangesSince(saved_interaction);
    auto interaction_cursor =
        restore_cursor(saved_interaction, checkpoint.interaction_revision, interaction_.CurrentRevision(),
                       interaction_.LatestChangeCursor(), interaction_batch.snapshot_required,
                       "gameplay.world_integration.interaction_checkpoint_gap");
    if (!interaction_cursor)
        return foundation::Result<void>::Failure(interaction_cursor.GetError());

    wc_ = world_cursor.Value();
    ec_ = environment_cursor.Value();
    ic_ = interaction_cursor.Value();
    return foundation::Result<void>::Success();
}

void WorldFactsAdapter::ResetCursorsToLatest() noexcept
{
    wc_ = world_.LatestChangeCursor();
    ec_ = environment_.LatestChangeCursor();
    ic_ = interaction_.LatestChangeCursor();
}
bool EntityInteractionStateProvider::IsMaterialized(GameplayObjectRef object) const
{
    auto id = entities::EntityService::FromGameplayObjectRef(object);
    const auto r = entities_.Find(id);
    return r && r->materialization == entities::EntityMaterializationState::Materialized;
}
Revision EntityInteractionStateProvider::RevisionOf(GameplayObjectRef object) const
{
    auto id = entities::EntityService::FromGameplayObjectRef(object);
    const auto r = entities_.Find(id);
    return r ? r->revision : Revision{};
}
} // namespace epidemic::gameplay::world_integration
