#include "Epidemic/GameFramework/WorldIntegration/world_adapters.h"

#include <cstdlib>
#include <iostream>

#define CHECK(expr)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
        {                                                                                                              \
            std::cerr << "CHECK failed at line " << __LINE__ << ": " #expr << "\n";                            \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)

using namespace epidemic::gameplay;

namespace
{
struct InteractionProvider final : interaction::IInteractionProvider
{
    interaction::InteractionTypeId type{};
    interaction::InteractionProviderId id = interaction::InteractionProviderId::FromString("test.world.provider");

    [[nodiscard]] interaction::InteractionProviderId Id() const noexcept override { return id; }

    [[nodiscard]] std::vector<interaction::InteractionCandidate> Collect(
        const interaction::InteractionContext& context) const override
    {
        return {{type, context.actor, context.target, id, 1, interaction::InteractionAvailability::Available, {}, {}}};
    }
};

struct InteractionExecutor final : interaction::IInteractionExecutor
{
    [[nodiscard]] epidemic::foundation::Result<void> Validate(const interaction::InteractionPlan&) const override
    {
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] epidemic::foundation::Result<void> Commit(const interaction::InteractionPlan&,
                                                            interaction::InteractionExecutionId) noexcept override
    {
        return epidemic::foundation::Result<void>::Success();
    }
};
}

int main()
{
    world::WorldService world;
    environment::EnvironmentService environment;
    interaction::InteractionService interaction;
    queries::GameplayQueryService queries;
    facts::GameplayFactsService facts;

    const auto alteration_type = world::WorldAlterationTypeId::FromString("test.alt");
    CHECK(world.RegisterAlterationType(alteration_type, "test.alt"));
    const auto feature_type = world::WorldFeatureTypeId::FromString("test.feature");
    CHECK(world.RegisterFeatureType(feature_type, "test.feature"));
    const auto environment_type = environment::EnvironmentLayerTypeId::FromString("test.env");
    CHECK(environment.RegisterLayerType(environment_type, "test.env"));

    world::WorldAreaDefinition area;
    area.id = world::WorldAreaId::FromString("test.area");
    area.canonical_name = "test.area";
    area.bounds = {{0, 0, 0}, {1000, 1000, 1000}};
    CHECK(world.RegisterArea(area));

    const auto interaction_type = interaction::InteractionTypeId::FromString("test.inspect");
    interaction::InteractionDefinition interaction_definition;
    interaction_definition.type = interaction_type;
    interaction_definition.canonical_name = "test.inspect";
    interaction_definition.mode = interaction::InteractionExecutionMode::Timed;
    interaction_definition.materialization = interaction::InteractionMaterializationPolicy::AbstractAllowed;
    interaction_definition.persistence = interaction::InteractionPersistence::PersistentSession;
    interaction_definition.duration = GameplayDuration{100};
    CHECK(interaction.RegisterDefinition(interaction_definition));
    InteractionProvider provider;
    provider.type = interaction_type;
    InteractionExecutor executor;
    CHECK(interaction.RegisterProvider(provider));
    CHECK(interaction.RegisterExecutor(interaction_type, executor));

    CHECK(world.Freeze());
    environment.Freeze();
    interaction.Freeze();

    world_integration::WorldQueryAdapter query_adapter(world, environment, interaction, queries);
    CHECK(query_adapter.RegisterProviders());
    world_integration::WorldFactsAdapter facts_adapter(world, environment, interaction, facts);
    CHECK(facts_adapter.RegisterContracts());
    queries.Freeze();
    facts.Freeze();

    const auto area_query = queries.Execute(world_integration::AreasAtPositionQuery{{10, 10, 10}}, queries::QueryContext{});
    CHECK(area_query && area_query.Value().value && area_query.Value().value->size() == 1);

    // H66: Environment query revision depends on both World and Environment.
    world_integration::EnvironmentSampleQuery sample_query;
    sample_query.position = {10, 10, 10};
    sample_query.time = GameplayTimePoint{1};
    const auto sample_before = queries.Execute(sample_query, queries::QueryContext{});
    CHECK(sample_before);
    const auto sample_revision_before = sample_before.Value().metadata.revision;

    auto transaction = world.BeginTransaction();
    world::WorldAlterationRecord alteration;
    alteration.type = alteration_type;
    alteration.affected_area = area.bounds;
    CHECK(transaction.Create(alteration));
    CHECK(transaction.Commit());

    const auto sample_after_world = queries.Execute(sample_query, queries::QueryContext{});
    CHECK(sample_after_world);
    CHECK(sample_after_world.Value().metadata.revision != sample_revision_before);
    const auto sample_revision_after_world = sample_after_world.Value().metadata.revision;

    environment::EnvironmentLayer layer;
    layer.type = environment_type;
    layer.values.temperature_milli_c = 12000;
    layer.persistence = environment::EnvironmentPersistence::Persistent;
    CHECK(environment.AddLayer(layer));
    const auto sample_after_environment = queries.Execute(sample_query, queries::QueryContext{});
    CHECK(sample_after_environment);
    CHECK(sample_after_environment.Value().metadata.revision != sample_revision_after_world);

    // H67: ActiveInteraction query exposes the authoritative Interaction revision.
    interaction::InteractionContext interaction_context;
    interaction_context.actor = {GameplayDomainId::FromString("test.actor"), GameplayObjectId::FromString("actor.1")};
    interaction_context.target = {GameplayDomainId::FromString("test.target"), GameplayObjectId::FromString("target.1")};
    interaction_context.gameplay.time = GameplayTimePoint{5};
    const auto candidates = interaction.GetAvailableInteractions(interaction_context);
    CHECK(candidates.size() == 1);
    const auto plan = interaction.Prepare(interaction_context, candidates.front());
    CHECK(plan);
    const auto committed = interaction.Commit(plan.Value());
    CHECK(committed);
    const auto active_query = queries.Execute(world_integration::ActiveInteractionsQuery{interaction_context.actor}, queries::QueryContext{});
    CHECK(active_query && active_query.Value().value && active_query.Value().value->size() == 1);
    CHECK(active_query.Value().metadata.revision == interaction.CurrentRevision());

    // M26: Object placement events use the object itself as the semantic subject.
    const GameplayObjectRef placed_object{GameplayDomainId::FromString("test.object"), GameplayObjectId::FromString("object.1")};
    world::ObjectPlacementRecord placement;
    placement.object = placed_object;
    placement.area = area.id;
    placement.position = {20, 20, 20};
    CHECK(world.PlaceObject(placement));

    const auto published = facts_adapter.PublishPending();
    CHECK(published && published.Value() >= 4);
    CHECK(facts.Dispatch());
    const auto world_event = EventTypeId::FromString("framework.world.changed");
    const auto history = facts.FindHistory(world_event);
    CHECK(!history.empty());
    bool found_object_subject = false;
    for (const auto& event : history)
    {
        if (event.envelope.subject == placed_object)
            found_object_subject = true;
        CHECK(event.envelope.subject.IsValid());
    }
    CHECK(found_object_subject);

    // H68: checkpoint preserves cursor/revision state and normalizes correctly when owner restore resets journals.
    const auto checkpoint = facts_adapter.CaptureCheckpoint();
    const auto world_snapshot = world.CaptureSnapshot();
    const auto environment_snapshot = environment.CaptureSnapshot();
    const auto interaction_snapshot = interaction.CaptureSnapshot();

    world::WorldFeatureRecord feature;
    feature.id = world::WorldFeatureId::FromString("test.feature.dynamic");
    feature.type = feature_type;
    feature.bounds = area.bounds;
    CHECK(world.AddDynamicFeature(feature));
    CHECK(environment.RemoveLayer(environment.CaptureSnapshot().layers.front().id));
    CHECK(interaction.Cancel(committed.Value().execution));

    CHECK(world.RestoreSnapshot(world_snapshot));
    CHECK(environment.RestoreSnapshot(environment_snapshot));
    CHECK(interaction.RestoreSnapshot(interaction_snapshot));
    CHECK(world.LatestChangeSequence() == 0);
    CHECK(environment.LatestChangeSequence() == 0);
    CHECK(interaction.LatestChangeSequence() == 0);
    CHECK(facts_adapter.RestoreCheckpoint(checkpoint));

    // New owner changes start a fresh sequence epoch and must not be skipped by the old checkpoint.
    auto transaction_after_restore = world.BeginTransaction();
    world::WorldAlterationRecord second_alteration;
    second_alteration.type = alteration_type;
    second_alteration.affected_area = area.bounds;
    CHECK(transaction_after_restore.Create(second_alteration));
    CHECK(transaction_after_restore.Commit());
    const auto after_restore_publish = facts_adapter.PublishPending();
    CHECK(after_restore_publish && after_restore_publish.Value() == 1);

    return 0;
}
