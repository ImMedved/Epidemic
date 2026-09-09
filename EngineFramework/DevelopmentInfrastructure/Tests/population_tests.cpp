#include "Epidemic/GameFramework/Population/population.h"

#include <cstdlib>
#include <iostream>
#include <limits>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::population;

namespace
{
void Check(bool value, const char *message)
{
    if (!value)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

GameplayObjectRef Ref(const char *domain, const char *id)
{
    return {GameplayDomainId::FromString(domain), GameplayObjectId::FromString(id)};
}

PopulationTemplate CitizenTemplate(bool persistent = true)
{
    PopulationTemplate templ;
    templ.id = PopulationTemplateId::FromString(persistent ? "citizen.template" : "transient.template");
    templ.entity_archetype = TypeId::FromString(persistent ? "entity.citizen" : "entity.transient");
    templ.persistent = persistent;
    return templ;
}
} // namespace

int main()
{
    PopulationService service;
    const auto templ = CitizenTemplate();
    const auto transient_templ = CitizenTemplate(false);
    Check(static_cast<bool>(service.RegisterTemplate(templ)), "register template");
    Check(static_cast<bool>(service.RegisterTemplate(transient_templ)), "register transient template");
    Check(static_cast<bool>(service.FreezeDefinitions()), "freeze definitions");
    Check(service.DefinitionsFrozen(), "definitions frozen flag");
    Check(!service.RegisterTemplate(PopulationTemplate{}), "definitions reject late registration");

    PopulationGroup group;
    group.area = Ref("world.area", "town");
    group.society_group = Ref("society.group", "citizens");
    group.desired_count = 200;
    group.current_known_count = 999;
    group.materialized_count = 999;
    auto group_id = service.CreateGroup(group);
    Check(static_cast<bool>(group_id), "create group");
    Check(service.GetGroup(group_id.Value())->current_known_count == 0, "group counters are derived");

    PopulationUnit unit;
    unit.group = group_id.Value();
    unit.template_id = templ.id;
    unit.state = PopulationUnitState::Latent;
    auto unit_id = service.CreateUnit(unit, {.time = GameplayTimePoint{10}});
    Check(static_cast<bool>(unit_id), "create unit");
    Check(service.GetPopulationCounts(group_id.Value()).latent == 1, "latent count");

    const auto entity = Ref("entities", "npc.1");
    Check(static_cast<bool>(service.MaterializeUnit(unit_id.Value(), entity)), "materialize");
    Check(service.GetPopulationCounts(group_id.Value()).materialized == 1, "materialized count");
    Check(service.FindMaterializationCandidates(group.area, 10).empty(), "no candidate after materialize");

    PopulationUnit other;
    other.group = group_id.Value();
    other.template_id = templ.id;
    other.state = PopulationUnitState::Abstract;
    auto other_id = service.CreateUnit(other);
    Check(static_cast<bool>(other_id), "create second unit");
    Check(!service.MaterializeUnit(other_id.Value(), entity), "entity binding is unique");

    Check(static_cast<bool>(service.DematerializeUnit(unit_id.Value())), "dematerialize");
    Check(service.GetPopulationCounts(group_id.Value()).abstract_units == 2, "abstract count");
    Check(!service.GetUnit(unit_id.Value())->entity.has_value(), "dematerialize clears representation binding");
    Check(service.FindMaterializationCandidates(group.area, 10).size() == 2, "candidate after dematerialize");
    Check(!service.DematerializeUnit(unit_id.Value()), "cannot dematerialize an abstract unit twice");

    PopulationResidence residence;
    residence.unit = unit_id.Value();
    residence.home_area = group.area;
    residence.home_property = Ref("property", "house.1");
    auto residence_id = service.AssignResidence(residence);
    Check(static_cast<bool>(residence_id), "assign residence");
    Check(service.FindResidentsOfArea(group.area).size() == 1, "resident query");

    PopulationResidence replacement = residence;
    replacement.home_area = Ref("world.area", "suburb");
    replacement.home_property = Ref("property", "house.2");
    auto replacement_id = service.AssignResidence(replacement);
    Check(static_cast<bool>(replacement_id) && replacement_id.Value() == residence_id.Value(),
          "residence assignment replaces the single active primary residence");
    Check(service.FindResidentsOfArea(group.area).empty(), "old residence no longer active");
    Check(service.FindResidentsOfArea(replacement.home_area).size() == 1, "replacement residence active");

    PopulationMigration migration;
    migration.unit = unit_id.Value();
    migration.to = Ref("world.area", "port");
    auto migration_id = service.StartMigration(migration, {.time = GameplayTimePoint{20}});
    Check(static_cast<bool>(migration_id), "start migration");
    Check(!service.StartMigration(migration), "second active migration rejected");
    Check(static_cast<bool>(service.CompleteMigration(migration_id.Value())), "complete migration");
    Check(service.GetUnit(unit_id.Value())->current_area == migration.to, "migration area");
    Check(service.GetUnit(unit_id.Value())->state == PopulationUnitState::Abstract,
          "completed migration returns resident to normal lifecycle state");
    Check(service.FindMaterializationCandidates(migration.to, 10).size() == 1,
          "migrated resident remains materialization candidate");

    PopulationMigration cancelled;
    cancelled.unit = unit_id.Value();
    cancelled.to = Ref("world.area", "capital");
    auto cancelled_id = service.StartMigration(cancelled);
    Check(static_cast<bool>(cancelled_id), "start cancellable migration");
    Check(static_cast<bool>(service.CancelMigration(cancelled_id.Value())), "cancel migration");
    Check(service.GetUnit(unit_id.Value())->current_area == migration.to, "cancel keeps current area");

    Check(static_cast<bool>(service.MarkUnitDead(other_id.Value())), "mark unit dead");
    Check(!service.MaterializeUnit(other_id.Value(), Ref("entities", "dead.npc")), "dead unit cannot materialize");
    Check(!service.StartMigration(PopulationMigration{.unit = other_id.Value(), .to = Ref("world.area", "elsewhere")}),
          "dead unit cannot migrate");

    PopulationUnit transient_unit;
    transient_unit.group = group_id.Value();
    transient_unit.template_id = transient_templ.id;
    transient_unit.state = PopulationUnitState::Latent;
    auto transient_id = service.CreateUnit(transient_unit);
    Check(static_cast<bool>(transient_id), "create transient unit");

    const auto pre_restore_cursor = service.ReadChangesSince(ChangeCursor{}).latest_cursor;
    Check(pre_restore_cursor.sequence >= 2, "pre-restore population journal has multiple changes");
    auto snapshot = service.CaptureSnapshot();
    bool transient_in_snapshot = false;
    for (const auto &saved : snapshot.units)
        transient_in_snapshot = transient_in_snapshot || saved.id == transient_id.Value();
    Check(!transient_in_snapshot, "nonpersistent template unit omitted from snapshot");
    PopulationService restored;
    Check(static_cast<bool>(restored.RegisterTemplate(templ)), "restore register current template");
    Check(static_cast<bool>(restored.RegisterTemplate(transient_templ)), "restore register transient template");
    Check(static_cast<bool>(restored.FreezeDefinitions()), "restore freeze definitions");
    Check(static_cast<bool>(restored.RestoreSnapshot(snapshot)), "restore");
    Check(restored.ReadChangesSince(pre_restore_cursor).snapshot_required,
          "pre-restore population cursor requires snapshot in new journal epoch");
    Check(restored.ReadChangesSince(restored.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required,
          "future population cursor is incompatible");
    Check(restored.GetUnit(unit_id.Value())->current_area == migration.to, "restore unit");
    Check(restored.GetUnit(transient_id.Value()) == nullptr, "transient unit not restored");
    Check(restored.ReadChangesSince(ChangeCursor{}).changes.empty(), "restore no events");
    Check(restored.GetGroup(group_id.Value())->current_known_count == 2, "restore recomputes group counters");

    const auto original_revision = restored.CurrentRevision();
    const auto original_units = restored.GetDiagnostics().units;
    auto broken = snapshot;
    broken.unit_ids.next = 1;
    Check(!restored.RestoreSnapshot(broken), "stale generator snapshot rejected");
    Check(restored.CurrentRevision() == original_revision && restored.GetDiagnostics().units == original_units,
          "failed restore leaves current state unchanged");

    PopulationUnit collision_probe;
    collision_probe.group = group_id.Value();
    collision_probe.template_id = templ.id;
    collision_probe.state = PopulationUnitState::Abstract;
    auto collision_probe_id = restored.CreateUnit(collision_probe);
    Check(static_cast<bool>(collision_probe_id), "generator continues after restore");
    Check(restored.ReadChangesSince(pre_restore_cursor).snapshot_required,
          "old population cursor remains incompatible after new epoch change");
    const auto new_epoch = restored.ReadChangesSince(ChangeCursor{});
    Check(!new_epoch.snapshot_required && !new_epoch.changes.empty(), "new population epoch readable from zero");
    const auto current_epoch = restored.ReadChangesSince(new_epoch.latest_cursor);
    Check(!current_epoch.snapshot_required && current_epoch.changes.empty(), "exact population cursor is current");

    for (int i = 0; i < 4200; ++i)
    {
        PopulationResidence update;
        update.unit = unit_id.Value();
        update.home_area = (i % 2 == 0) ? Ref("world.area", "suburb") : Ref("world.area", "town");
        update.home_property = Ref("property", "house.rotating");
        Check(static_cast<bool>(restored.AssignResidence(update)), "journal stress residence update");
    }
    const auto journal = restored.ReadChangesSince(ChangeCursor{});
    Check(journal.snapshot_required, "bounded journal requires snapshot for stale consumer");

    return 0;
}
