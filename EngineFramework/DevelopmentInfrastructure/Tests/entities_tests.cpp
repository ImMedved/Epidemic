#include "Epidemic/GameFramework/Entities/entities.h"

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::entities;

int main()
{
    EntityService service;
    EntityArchetypeDefinition actor;
    actor.canonical_name = "test.entity.actor";
    actor.persistence = EntityPersistencePolicy::Persistent;
    actor.materialization = EntityMaterializationPolicy::RequireMaterialized;
    EntityPartDefinition body_part;
    body_part.id = EntityPartId::FromString("test.entity.actor.body");
    body_part.canonical_name = "test.entity.actor.body";
    EntityPartDefinition hand_part;
    hand_part.id = EntityPartId::FromString("test.entity.actor.hand");
    hand_part.canonical_name = "test.entity.actor.hand";
    hand_part.parent = body_part.id;
    actor.parts = {body_part, hand_part};
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
    if (!service.Resolve(first_handle).has_value() || service.HandleOf(first_id) != first_handle) return 5;
    if (!service.RequiresMaterialization(first_id) || service.MaterializationPolicyOf(first_id) != EntityMaterializationPolicy::RequireMaterialized) return 53;
    const auto hand = service.GetPart(first_id, hand_part.id);
    const auto ancestors = hand ? service.GetPartAncestors(*hand) : std::vector<EntityPartRef>{};
    if (!hand || ancestors.size() != 1 || ancestors.front().part != body_part.id) return 51;
    const auto generic_hand = EntityService::ToGameplayObjectPartRef(*hand);
    if (!generic_hand.IsValid() || EntityService::FromGameplayObjectPartRef(generic_hand) != *hand) return 52;

    if (!service.SetMaterializationState(first_id, EntityMaterializationState::Materialized)) return 6;
    const auto materialized = service.Find(first_id);
    if (!materialized || materialized->materialization != EntityMaterializationState::Materialized) return 61;
    if (!service.Convert(first_id, object_id.Value())) return 7;
    const auto converted = service.Find(first_id);
    if (!converted || converted->archetype != object_id.Value() || !converted->instance_tags.HasExact(living.Value())) return 8;
    if (service.RequiresMaterialization(first_id) || service.MaterializationPolicyOf(first_id) != EntityMaterializationPolicy::AbstractAllowed) return 81;

    const auto snapshot = service.CaptureSnapshot();
    if (snapshot.records.size() != 1) return 9;

    GameplayContext destroy_context;
    destroy_context.operation = OperationId::FromString("test.destroy.operation");
    if (!service.RequestDestroy(first_id, EntityDestroyReason::Consumed, destroy_context)) return 10;
    const auto pending = service.Find(first_id);
    if (!pending || pending->lifecycle != EntityLifecycleState::PendingDestroy) return 11;
    if (service.RequestDestroy(first_id, EntityDestroyReason::Destroyed)) return 12;
    const auto pending_snapshot = service.CaptureSnapshot();
    if (pending_snapshot.pending_destruction.size() != 1 ||
        pending_snapshot.pending_destruction.front().reason != EntityDestroyReason::Consumed ||
        pending_snapshot.pending_destruction.front().context.operation != destroy_context.operation) return 121;
    const auto destroyed = service.CommitPendingDestruction();
    const auto destroyed_record = service.Find(first_id);
    if (!destroyed || destroyed.Value().size() != 1 || !destroyed_record || destroyed_record->lifecycle != EntityLifecycleState::Destroyed ||
        !service.Resolve(first_handle).has_value()) return 13;
    if (service.RequestDestroy(first_id, EntityDestroyReason::Destroyed)) return 131;

    CreateEntityRequest second_create;
    second_create.archetype = actor_id.Value();
    const auto second = service.Create(second_create);
    if (!second || second.Value().handle.slot == first_handle.slot) return 14;

    if (!service.RestoreSnapshot(snapshot)) return 15;
    const auto restored = service.Find(first_id);
    if (!restored || restored->archetype != object_id.Value() || restored->materialization != EntityMaterializationState::Abstract) return 16;
    if (service.HandleOf(first_id).generation == 0 || service.Resolve(first_handle).has_value()) return 17;

    auto invalid_snapshot = snapshot;
    invalid_snapshot.records.front().revision = Revision{snapshot.revision.value + 1};
    if (service.RestoreSnapshot(invalid_snapshot)) return 170;

    if (!service.Deactivate(first_id) || !service.Find(first_id) || service.Find(first_id)->lifecycle != EntityLifecycleState::Dormant) return 171;
    if (service.SetMaterializationState(first_id, EntityMaterializationState::Materialized)) return 172;
    if (!service.Activate(first_id) || !service.Find(first_id) || service.Find(first_id)->lifecycle != EntityLifecycleState::Alive) return 173;

    // Requested IDs are supported for deterministic restore/import but duplicates are rejected.
    CreateEntityRequest requested;
    requested.archetype = actor_id.Value();
    requested.requested_id = EntityId::FromString("test.entity.requested");
    if (!service.Create(requested) || service.Create(requested)) return 18;

    CreateEntityRequest removable_request;
    removable_request.archetype = actor_id.Value();
    auto removable = service.Create(removable_request);
    if (!removable || !service.RequestDestroy(removable.Value().id, EntityDestroyReason::Removed)) return 181;
    auto removable_destroyed = service.CommitPendingDestruction();
    if (!removable_destroyed || !service.Remove(removable.Value().id) || service.Exists(removable.Value().id)) return 182;

    // Pending destruction provenance survives save/restore and still drives the terminal transition.
    if (!service.RestoreSnapshot(pending_snapshot)) return 183;
    auto restored_destroy = service.CommitPendingDestruction();
    if (!restored_destroy || restored_destroy.Value().size() != 1) return 184;
    const auto restored_destroy_change = service.ReadChangesSince(ChangeCursor{}).changes;
    if (restored_destroy_change.empty() || restored_destroy_change.back().destroy_reason != EntityDestroyReason::Consumed) return 185;
    if (!service.RestoreSnapshot(snapshot)) return 186;


    // Requested IDs in the service-owned scope advance the internal generator, so the next
    // generated entity cannot collide with accepted imported identity.
    const auto entity_scope = GameplayObjectId::FromString("framework.entities.instances").High();
    CreateEntityRequest requested_forward;
    requested_forward.archetype = actor_id.Value();
    requested_forward.requested_id = EntityId::FromRaw(entity_scope, 1000000);
    const auto own_scope_requested = service.Create(requested_forward);
    if (!own_scope_requested) return 187;
    CreateEntityRequest generated_after_requested;
    generated_after_requested.archetype = actor_id.Value();
    const auto generated_after = service.Create(generated_after_requested);
    if (!generated_after || generated_after.Value().id.High() != entity_scope || generated_after.Value().id.Low() <= 1000000) return 188;
    if (service.Create(requested_forward)) return 189;

    CreateEntityRequest requested_behind;
    requested_behind.archetype = actor_id.Value();
    requested_behind.requested_id = EntityId::FromRaw(entity_scope, 42);
    const auto own_scope_behind = service.Create(requested_behind);
    const auto generated_after_behind = service.Create(generated_after_requested);
    if (!own_scope_behind || !generated_after_behind || generated_after_behind.Value().id.Low() <= generated_after.Value().id.Low()) return 190;

    // Removed slots may be reused, but old generation-aware handles must never resolve to the
    // replacement entity. This exercises the Windows Debug stress failure path without changing
    // the slot/generation design.
    for (int i = 0; i < 256; ++i)
    {
        CreateEntityRequest transient_request;
        transient_request.archetype = actor_id.Value();
        const auto transient = service.Create(transient_request);
        if (!transient || !service.RequestDestroy(transient.Value().id, EntityDestroyReason::Removed)) return 191;
        const auto transient_destroyed = service.CommitPendingDestruction();
        if (!transient_destroyed || !service.Remove(transient.Value().id)) return 192;
        const auto replacement = service.Create(transient_request);
        if (!replacement || service.Resolve(transient.Value().handle).has_value() || !service.Resolve(replacement.Value().handle).has_value()) return 193;
    }

    // Basic capacity/stability stress.
    for (int i = 0; i < 100000; ++i)
    {
        CreateEntityRequest stress;
        stress.archetype = actor_id.Value();
        if (!service.Create(stress)) return 19;
    }
    const auto diagnostics = service.GetDiagnostics();
    if (diagnostics.entity_count < 100001 || service.AllEntities().size() != diagnostics.entity_count) return 20;

    const auto changes = service.ReadChangesSince(ChangeCursor{}).changes;
    if (changes.empty() || changes.back().sequence != service.LatestChangeCursor().sequence) return 21;
    return 0;
}
