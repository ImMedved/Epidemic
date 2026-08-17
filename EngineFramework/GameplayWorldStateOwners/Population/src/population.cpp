#include "Epidemic/GameFramework/Population/population.h"
#include "Epidemic/Foundation/error.h"
#include <algorithm>
#include <iterator>
#include <utility>

namespace epidemic::gameplay::population
{
namespace
{
foundation::Error Error(std::string_view c, std::string_view m)
{
    return foundation::Error::Create(c, m);
}
} // namespace
PopulationUnit *PopulationService::FindMutableUnit(PopulationUnitId id) noexcept
{
    auto it = units_.find(id);
    return it == units_.end() ? nullptr : &it->second;
}
PopulationGroup *PopulationService::FindMutableGroup(PopulationGroupId id) noexcept
{
    auto it = groups_.find(id);
    return it == groups_.end() ? nullptr : &it->second;
}
foundation::Result<void> PopulationService::RegisterTemplate(PopulationTemplate d)
{
    if (!d.id.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.population.invalid_template", "invalid population template"));
    if (templates_.contains(d.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.population.duplicate_template", "duplicate population template"));
    Bump();
    d.revision = revision_;
    templates_.emplace(d.id, std::move(d));
    return foundation::Result<void>::Success();
}
foundation::Result<PopulationGroupId> PopulationService::CreateGroup(PopulationGroup g)
{
    if (!g.area.IsValid())
        return foundation::Result<PopulationGroupId>::Failure(
            Error("gameplay.population.invalid_group", "invalid population group"));
    if (!g.id.IsValid())
        g.id = PopulationGroupId{group_ids_.Next()};
    if (groups_.contains(g.id))
        return foundation::Result<PopulationGroupId>::Failure(
            Error("gameplay.population.duplicate_group", "duplicate population group"));
    Bump();
    g.revision = revision_;
    auto id = g.id;
    groups_.emplace(id, g);
    Record({0, PopulationChangeKind::GroupCreated, id, {}, {}, g.area, {}, revision_});
    return foundation::Result<PopulationGroupId>::Success(id);
}
foundation::Result<PopulationUnitId> PopulationService::CreateUnit(PopulationUnit u, GameplayContext c)
{
    if (!u.group.IsValid() || !groups_.contains(u.group))
        return foundation::Result<PopulationUnitId>::Failure(
            Error("gameplay.population.invalid_unit_group", "invalid population group"));
    if (u.template_id.IsValid() && !templates_.contains(u.template_id))
        return foundation::Result<PopulationUnitId>::Failure(
            Error("gameplay.population.unknown_template", "unknown population template"));
    if (!u.id.IsValid())
        u.id = PopulationUnitId{unit_ids_.Next()};
    if (units_.contains(u.id))
        return foundation::Result<PopulationUnitId>::Failure(
            Error("gameplay.population.duplicate_unit", "duplicate population unit"));
    if (!u.current_area.IsValid())
        u.current_area = groups_.at(u.group).area;
    if (!u.home_area.IsValid())
        u.home_area = u.current_area;
    Bump();
    u.revision = revision_;
    auto id = u.id;
    auto group = u.group;
    units_.emplace(id, u);
    RecountGroup(group);
    Record({0, PopulationChangeKind::UnitCreated, group, id, u.entity.value_or(GameplayObjectRef{}), u.current_area, c,
            revision_});
    return foundation::Result<PopulationUnitId>::Success(id);
}
foundation::Result<void> PopulationService::SetUnitEntity(PopulationUnitId id, GameplayObjectRef e, GameplayContext c)
{
    auto *u = FindMutableUnit(id);
    if (!u || !e.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.population.invalid_entity_link", "invalid entity link"));
    Bump();
    u->entity = e;
    u->revision = revision_;
    Record({0, PopulationChangeKind::GroupChanged, u->group, id, e, u->current_area, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> PopulationService::MaterializeUnit(PopulationUnitId id, GameplayObjectRef e, GameplayContext c)
{
    auto *u = FindMutableUnit(id);
    if (!u || !e.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.population.invalid_materialize", "invalid materialization request"));
    Bump();
    u->entity = e;
    u->state = PopulationUnitState::Materialized;
    u->revision = revision_;
    ++diagnostics_.materialization_requests;
    RecountGroup(u->group);
    Record({0, PopulationChangeKind::UnitMaterialized, u->group, id, e, u->current_area, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> PopulationService::DematerializeUnit(PopulationUnitId id, GameplayContext c)
{
    auto *u = FindMutableUnit(id);
    if (!u)
        return foundation::Result<void>::Failure(Error("gameplay.population.unit_missing", "population unit missing"));
    Bump();
    u->state = PopulationUnitState::Abstract;
    u->revision = revision_;
    ++diagnostics_.dematerialization_requests;
    RecountGroup(u->group);
    Record({0, PopulationChangeKind::UnitDematerialized, u->group, id, u->entity.value_or(GameplayObjectRef{}),
            u->current_area, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> PopulationService::MarkUnitDead(PopulationUnitId id, GameplayContext c)
{
    auto *u = FindMutableUnit(id);
    if (!u)
        return foundation::Result<void>::Failure(Error("gameplay.population.unit_missing", "population unit missing"));
    Bump();
    u->state = PopulationUnitState::Dead;
    u->revision = revision_;
    RecountGroup(u->group);
    Record({0, PopulationChangeKind::UnitDied, u->group, id, u->entity.value_or(GameplayObjectRef{}), u->current_area,
            c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> PopulationService::RetireUnit(PopulationUnitId id, GameplayContext c)
{
    auto *u = FindMutableUnit(id);
    if (!u)
        return foundation::Result<void>::Failure(Error("gameplay.population.unit_missing", "population unit missing"));
    Bump();
    u->state = PopulationUnitState::Removed;
    u->revision = revision_;
    RecountGroup(u->group);
    Record({0, PopulationChangeKind::UnitRetired, u->group, id, u->entity.value_or(GameplayObjectRef{}),
            u->current_area, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<PopulationResidenceId> PopulationService::AssignResidence(PopulationResidence r, GameplayContext c)
{
    if (!r.unit.IsValid() || !units_.contains(r.unit) || !r.home_area.IsValid())
        return foundation::Result<PopulationResidenceId>::Failure(
            Error("gameplay.population.invalid_residence", "invalid residence"));
    if (!r.id.IsValid())
        r.id = PopulationResidenceId{residence_ids_.Next()};
    if (residences_.contains(r.id))
        return foundation::Result<PopulationResidenceId>::Failure(
            Error("gameplay.population.duplicate_residence", "duplicate residence"));
    Bump();
    r.revision = revision_;
    auto id = r.id;
    auto unit = r.unit;
    auto *u = FindMutableUnit(unit);
    if (u)
    {
        u->home_area = r.home_area;
        u->revision = revision_;
    }
    residences_.emplace(id, r);
    Record({0,
            PopulationChangeKind::ResidenceAssigned,
            u ? u->group : PopulationGroupId{},
            unit,
            {},
            r.home_area,
            c,
            revision_});
    return foundation::Result<PopulationResidenceId>::Success(id);
}
foundation::Result<PopulationMigrationId> PopulationService::StartMigration(PopulationMigration m, GameplayContext c)
{
    auto *u = FindMutableUnit(m.unit);
    if (!u || !m.to.IsValid())
        return foundation::Result<PopulationMigrationId>::Failure(
            Error("gameplay.population.invalid_migration", "invalid migration"));
    if (!m.id.IsValid())
        m.id = PopulationMigrationId{migration_ids_.Next()};
    if (migrations_.contains(m.id))
        return foundation::Result<PopulationMigrationId>::Failure(
            Error("gameplay.population.duplicate_migration", "duplicate migration"));
    if (!m.from.IsValid())
        m.from = u->current_area;
    m.state = MigrationState::Active;
    Bump();
    m.revision = revision_;
    auto id = m.id;
    migrations_.emplace(id, m);
    Record({0, PopulationChangeKind::MigrationStarted, u->group, m.unit, u->entity.value_or(GameplayObjectRef{}), m.to,
            c, revision_});
    return foundation::Result<PopulationMigrationId>::Success(id);
}
foundation::Result<void> PopulationService::CompleteMigration(PopulationMigrationId id, GameplayContext c)
{
    auto it = migrations_.find(id);
    if (it == migrations_.end())
        return foundation::Result<void>::Failure(Error("gameplay.population.migration_missing", "migration missing"));
    auto *u = FindMutableUnit(it->second.unit);
    if (!u)
        return foundation::Result<void>::Failure(Error("gameplay.population.unit_missing", "population unit missing"));
    Bump();
    it->second.state = MigrationState::Completed;
    it->second.revision = revision_;
    u->current_area = it->second.to;
    u->state = PopulationUnitState::Migrated;
    u->revision = revision_;
    RecountGroup(u->group);
    Record({0, PopulationChangeKind::MigrationCompleted, u->group, u->id, u->entity.value_or(GameplayObjectRef{}),
            u->current_area, c, revision_});
    return foundation::Result<void>::Success();
}
const PopulationGroup *PopulationService::GetGroup(PopulationGroupId id) const noexcept
{
    auto it = groups_.find(id);
    return it == groups_.end() ? nullptr : &it->second;
}
const PopulationUnit *PopulationService::GetUnit(PopulationUnitId id) const noexcept
{
    auto it = units_.find(id);
    return it == units_.end() ? nullptr : &it->second;
}
std::vector<PopulationGroup> PopulationService::FindGroupsInArea(GameplayObjectRef a) const
{
    std::vector<PopulationGroup> o;
    for (const auto &[id, g] : groups_)
    {
        (void)id;
        if (g.area == a)
            o.push_back(g);
    }
    std::sort(o.begin(), o.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return o;
}
std::vector<PopulationUnit> PopulationService::FindUnitsByGroup(PopulationGroupId g) const
{
    std::vector<PopulationUnit> o;
    for (const auto &[id, u] : units_)
    {
        (void)id;
        if (u.group == g)
            o.push_back(u);
    }
    std::sort(o.begin(), o.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return o;
}
std::vector<PopulationUnit> PopulationService::FindUnitsByState(PopulationUnitState s) const
{
    std::vector<PopulationUnit> o;
    for (const auto &[id, u] : units_)
    {
        (void)id;
        if (u.state == s)
            o.push_back(u);
    }
    std::sort(o.begin(), o.end(), [](const auto &a, const auto &b) {
        if (a.group != b.group)
            return a.group < b.group;
        return a.id < b.id;
    });
    return o;
}
std::vector<PopulationUnit> PopulationService::FindResidentsOfArea(GameplayObjectRef a) const
{
    std::vector<PopulationUnit> o;
    for (const auto &[id, u] : units_)
    {
        (void)id;
        if (u.home_area == a)
            o.push_back(u);
    }
    std::sort(o.begin(), o.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return o;
}
std::vector<PopulationUnit> PopulationService::FindMaterializationCandidates(GameplayObjectRef a,
                                                                             std::size_t limit) const
{
    std::vector<PopulationUnit> o;
    for (const auto &[id, u] : units_)
    {
        (void)id;
        if (u.current_area == a && (u.state == PopulationUnitState::Latent || u.state == PopulationUnitState::Abstract))
            o.push_back(u);
    }
    std::sort(o.begin(), o.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    if (o.size() > limit)
        o.resize(limit);
    return o;
}
PopulationCounts PopulationService::GetPopulationCounts(PopulationGroupId g) const
{
    PopulationCounts c;
    for (const auto &[id, u] : units_)
    {
        (void)id;
        if (g.IsValid() && u.group != g)
            continue;
        switch (u.state)
        {
        case PopulationUnitState::Latent:
            ++c.latent;
            break;
        case PopulationUnitState::Abstract:
            ++c.abstract_units;
            break;
        case PopulationUnitState::Materializing:
            ++c.materializing;
            break;
        case PopulationUnitState::Materialized:
            ++c.materialized;
            break;
        case PopulationUnitState::Dematerializing:
            ++c.dematerializing;
            break;
        case PopulationUnitState::Dead:
            ++c.dead;
            break;
        case PopulationUnitState::Removed:
            ++c.removed;
            break;
        case PopulationUnitState::Migrated:
            ++c.migrated;
            break;
        }
    }
    return c;
}
std::vector<PopulationChange> PopulationService::ChangesSince(std::uint64_t s) const
{
    std::vector<PopulationChange> o;
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(o),
                 [s](const auto &c) { return c.sequence > s; });
    return o;
}
PopulationSnapshot PopulationService::CaptureSnapshot() const
{
    PopulationSnapshot s;
    for (const auto &[id, t] : templates_)
    {
        (void)id;
        s.templates.push_back(t);
    }
    for (const auto &[id, g] : groups_)
    {
        (void)id;
        s.groups.push_back(g);
    }
    for (const auto &[id, u] : units_)
    {
        (void)id;
        s.units.push_back(u);
    }
    for (const auto &[id, r] : residences_)
    {
        (void)id;
        s.residences.push_back(r);
    }
    for (const auto &[id, m] : migrations_)
    {
        (void)id;
        s.migrations.push_back(m);
    }
    std::sort(s.templates.begin(), s.templates.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.groups.begin(), s.groups.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.units.begin(), s.units.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.residences.begin(), s.residences.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.migrations.begin(), s.migrations.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    s.group_ids = group_ids_.GetSnapshot();
    s.unit_ids = unit_ids_.GetSnapshot();
    s.residence_ids = residence_ids_.GetSnapshot();
    s.migration_ids = migration_ids_.GetSnapshot();
    s.revision = revision_;
    return s;
}
foundation::Result<void> PopulationService::RestoreSnapshot(PopulationSnapshot s)
{
    templates_.clear();
    groups_.clear();
    units_.clear();
    residences_.clear();
    migrations_.clear();
    for (auto &t : s.templates)
    {
        if (!t.id.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.population.restore_invalid", "invalid template snapshot"));
        templates_[t.id] = t;
    }
    for (auto &g : s.groups)
    {
        if (!g.id.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.population.restore_invalid", "invalid group snapshot"));
        groups_[g.id] = g;
    }
    for (auto &u : s.units)
    {
        if (!u.id.IsValid() || !u.group.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.population.restore_invalid", "invalid unit snapshot"));
        units_[u.id] = u;
    }
    for (auto &r : s.residences)
    {
        if (!r.id.IsValid() || !r.unit.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.population.restore_invalid", "invalid residence snapshot"));
        residences_[r.id] = r;
    }
    for (auto &m : s.migrations)
    {
        if (!m.id.IsValid() || !m.unit.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.population.restore_invalid", "invalid migration snapshot"));
        migrations_[m.id] = m;
    }
    group_ids_.Restore(s.group_ids);
    unit_ids_.Restore(s.unit_ids);
    residence_ids_.Restore(s.residence_ids);
    migration_ids_.Restore(s.migration_ids);
    revision_ = s.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    return foundation::Result<void>::Success();
}
PopulationDiagnostics PopulationService::GetDiagnostics() const noexcept
{
    auto d = diagnostics_;
    d.groups = groups_.size();
    d.units = units_.size();
    d.migrations = migrations_.size();
    d.residences = residences_.size();
    const auto c = GetPopulationCounts();
    d.latent = c.latent;
    d.abstract_units = c.abstract_units;
    d.materialized = c.materialized;
    return d;
}
void PopulationService::RecountGroup(PopulationGroupId id)
{
    auto *g = FindMutableGroup(id);
    if (!g)
        return;
    std::uint32_t known = 0;
    std::uint32_t materialized = 0;
    for (const auto &[uid, u] : units_)
    {
        (void)uid;
        if (u.group == id && u.state != PopulationUnitState::Removed)
        {
            ++known;
            if (u.state == PopulationUnitState::Materialized)
                ++materialized;
        }
    }
    g->current_known_count = known;
    g->materialized_count = materialized;
    g->revision = revision_;
}
void PopulationService::Record(PopulationChange c)
{
    c.sequence = next_change_sequence_++;
    changes_.push_back(c);
}
} // namespace epidemic::gameplay::population
