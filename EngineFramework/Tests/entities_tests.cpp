#include "Epidemic/GameFramework/Entities/entities.h"

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::entities;

int main()
{
    EntityService service;
    EntityArchetypeDefinition actor;
    actor.canonical_name = "test.entity.actor";
    actor.persistence = EntityPersistencePolicy::Persistent;
    const auto actor_id = service.RegisterArchetype(actor);
    EntityArchetypeDefinition object;
    object.canonical_name = "test.entity.object";
    object.persistence = EntityPersistencePolicy::Session;
    const auto object_id = service.RegisterArchetype(object);
    if (!actor_id || !object_id || service.RegisterArchetype(actor)) return 1;
    service.Freeze();
    EntityArchetypeDefinition late_archetype;
    late_archetype.canonical_name = "test.entity.late";
    if (service.RegisterArchetype(late_archetype)) return 2;

    GameplayTagRegistry tags;
    const auto living = tags.Register("entity.living");
    if (!living) return 3;
    tags.Freeze();

    CreateEntityRequest create;
    create.archetype = actor_id.Value();
    create.additional_tags.Add(living.Value());
    create.context.tick = GameplayTickId{1};
    const auto created = service.Create(create);
    if (!created || !created.Value().id.IsValid() || !created.Value().handle.IsValid()) return 4;
    const auto first_handle = created.Value().handle;
    const auto first_id = created.Value().id;
    if (service.Resolve(first_handle) == nullptr || service.HandleOf(first_id) != first_handle) return 5;

    if (!service.SetMaterializationState(first_id, EntityMaterializationState::Materialized) ||
        service.Find(first_id)->materialization != EntityMaterializationState::Materialized) return 6;
    if (!service.Convert(first_id, object_id.Value()) || service.Find(first_id)->archetype != object_id.Value()) return 7;
    if (!service.Find(first_id)->instance_tags.HasExact(living.Value())) return 8;

    const auto snapshot = service.CaptureSnapshot();
    if (snapshot.records.size() != 1) return 9;

    if (!service.RequestDestroy(first_id, EntityDestroyReason::Destroyed)) return 10;
    if (service.Find(first_id)->lifecycle != EntityLifecycleState::PendingDestroy) return 11;
    if (service.RequestDestroy(first_id, EntityDestroyReason::Destroyed)) return 12;
    const auto destroyed = service.CommitPendingDestruction();
    if (destroyed.size() != 1 || service.Find(first_id) != nullptr || service.Resolve(first_handle) != nullptr) return 13;

    CreateEntityRequest second_create;
    second_create.archetype = actor_id.Value();
    const auto second = service.Create(second_create);
    if (!second || second.Value().handle.slot != first_handle.slot || second.Value().handle.generation == first_handle.generation) return 14;

    if (!service.RestoreSnapshot(snapshot)) return 15;
    const auto* restored = service.Find(first_id);
    if (restored == nullptr || restored->archetype != object_id.Value() || restored->materialization != EntityMaterializationState::Materialized) return 16;
    if (service.HandleOf(first_id).generation == 0) return 17;

    // Requested IDs are supported for deterministic restore/import but duplicates are rejected.
    CreateEntityRequest requested;
    requested.archetype = actor_id.Value();
    requested.requested_id = EntityId::FromString("test.entity.requested");
    if (!service.Create(requested) || service.Create(requested)) return 18;

    // Basic capacity/stability stress.
    for (int i = 0; i < 100000; ++i)
    {
        CreateEntityRequest stress;
        stress.archetype = actor_id.Value();
        if (!service.Create(stress)) return 19;
    }
    const auto diagnostics = service.GetDiagnostics();
    if (diagnostics.entity_count < 100002 || service.AllEntities().size() != diagnostics.entity_count) return 20;

    const auto changes = service.ChangesSince(0);
    if (changes.empty() || changes.back().sequence != service.LatestChangeSequence()) return 21;
    return 0;
}
