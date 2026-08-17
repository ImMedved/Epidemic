#include "Epidemic/GameFramework/Encounters/encounters.h"
#include "Epidemic/Foundation/error.h"
#include <algorithm>
#include <iterator>
#include <numeric>
#include <utility>

namespace epidemic::gameplay::encounters
{
namespace
{
foundation::Error Error(std::string_view c, std::string_view m)
{
    return foundation::Error::Create(c, m);
}
} // namespace
foundation::Result<void> EncountersService::RegisterSpawnTable(SpawnTable t)
{
    if (!t.id.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.encounters.invalid_table", "invalid spawn table"));
    if (tables_.contains(t.id))
        return foundation::Result<void>::Failure(Error("gameplay.encounters.duplicate_table", "duplicate spawn table"));
    for (const auto &e : t.entries)
    {
        if (!e.id.IsValid() || !e.archetype.IsValid() || e.min_count > e.max_count)
            return foundation::Result<void>::Failure(Error("gameplay.encounters.invalid_entry", "invalid spawn entry"));
    }
    Bump();
    t.revision = revision_;
    tables_.emplace(t.id, std::move(t));
    return foundation::Result<void>::Success();
}
foundation::Result<void> EncountersService::RegisterEncounterDefinition(EncounterDefinition d)
{
    if (!d.id.IsValid() || !d.spawn_table.IsValid() || !tables_.contains(d.spawn_table))
        return foundation::Result<void>::Failure(
            Error("gameplay.encounters.invalid_definition", "invalid encounter definition"));
    if (definitions_.contains(d.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.encounters.duplicate_definition", "duplicate encounter definition"));
    Bump();
    d.revision = revision_;
    definitions_.emplace(d.id, std::move(d));
    return foundation::Result<void>::Success();
}
foundation::Result<SpawnPointId> EncountersService::AddSpawnPoint(SpawnPoint p)
{
    if (!p.area.IsValid())
        return foundation::Result<SpawnPointId>::Failure(
            Error("gameplay.encounters.invalid_spawn_point", "invalid spawn point"));
    if (!p.id.IsValid())
        p.id = SpawnPointId{point_ids_.Next()};
    if (points_.contains(p.id))
        return foundation::Result<SpawnPointId>::Failure(
            Error("gameplay.encounters.duplicate_spawn_point", "duplicate spawn point"));
    Bump();
    p.revision = revision_;
    auto id = p.id;
    points_.emplace(id, p);
    return foundation::Result<SpawnPointId>::Success(id);
}
bool EncountersService::CanSpawnEncounter(SpawnRequest r) const
{
    if (!r.encounter.IsValid() || !r.area.IsValid())
        return false;
    auto it = definitions_.find(r.encounter);
    if (it == definitions_.end())
        return false;
    if (!tables_.contains(it->second.spawn_table))
        return false;
    std::uint32_t active = 0;
    for (const auto &[id, e] : instances_)
    {
        (void)id;
        if (e.area == r.area && e.state == EncounterState::Active)
            ++active;
    }
    return active < budgets_.max_active_encounters;
}
SpawnResult EncountersService::SpawnEncounter(SpawnRequest r)
{
    ++diagnostics_.spawn_requests;
    SpawnResult result;
    if (!r.id.IsValid())
        r.id = SpawnRequestId{request_ids_.Next()};
    result.request_id = r.id;
    result.revision = revision_;
    Record({0, EncounterChangeKind::SpawnRequested, {}, {}, r.area, r.context, revision_});
    if (!CanSpawnEncounter(r))
    {
        ++diagnostics_.spawn_budget_rejects;
        ++diagnostics_.spawn_failures;
        result.state = SpawnResultState::Rejected;
        Record({0, EncounterChangeKind::SpawnFailed, {}, {}, r.area, r.context, revision_});
        return result;
    }
    const auto &def = definitions_.at(r.encounter);
    const auto &table = tables_.at(def.spawn_table);
    const auto entries = ResolveEntries(table, r.seed);
    std::uint32_t total = 0;
    for (const auto &e : entries)
        total += e.max_count;
    if (total > budgets_.max_spawned_entities_per_encounter)
    {
        ++diagnostics_.spawn_budget_rejects;
        ++diagnostics_.spawn_failures;
        result.state = SpawnResultState::Rejected;
        Record({0, EncounterChangeKind::SpawnFailed, {}, {}, r.area, r.context, revision_});
        return result;
    }
    Bump();
    EncounterInstance instance;
    instance.id = EncounterInstanceId{instance_ids_.Next()};
    instance.definition = r.encounter;
    instance.area = r.area;
    instance.origin = r.origin.value_or(r.area);
    instance.state = EncounterState::Active;
    instance.created_at = r.context.time;
    instance.last_updated_at = r.context.time;
    instance.revision = revision_;
    for (const auto &entry : entries)
    {
        random::RandomSequence seq(
            random::DeriveSeed(r.seed, random::RandomStream::FromString("encounter.count"), entry.id.value.Raw()),
            random::RandomStream::FromString("encounter.count"));
        const auto count =
            entry.min_count + (entry.max_count > entry.min_count
                                   ? static_cast<std::uint32_t>(seq.Uniform(entry.max_count - entry.min_count + 1u))
                                   : 0u);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            SpawnedEntityRecord rec;
            rec.id = SpawnedEntityRecordId{spawned_ids_.Next()};
            rec.encounter = instance.id;
            rec.archetype = entry.archetype;
            rec.revision = revision_;
            instance.spawned_entities.push_back(rec.id);
            result.spawned_records.push_back(rec.id);
            spawned_.emplace(rec.id, rec);
        }
    }
    result.encounter_instance = instance.id;
    result.state = SpawnResultState::Succeeded;
    instances_.emplace(instance.id, instance);
    ++diagnostics_.spawn_successes;
    Record({0, EncounterChangeKind::EncounterCreated, instance.id, {}, r.area, r.context, revision_});
    Record({0, EncounterChangeKind::EncounterActivated, instance.id, {}, r.area, r.context, revision_});
    Record({0, EncounterChangeKind::SpawnSucceeded, instance.id, {}, r.area, r.context, revision_});
    return result;
}
foundation::Result<void> EncountersService::BindSpawnedEntity(SpawnedEntityRecordId id, GameplayObjectRef entity,
                                                              GameplayContext c)
{
    auto it = spawned_.find(id);
    if (it == spawned_.end() || !entity.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.encounters.spawned_record_missing", "spawned record missing or entity invalid"));
    if (it->second.entity.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.encounters.spawned_record_already_bound", "spawned record already bound"));
    Bump();
    it->second.entity = entity;
    it->second.revision = revision_;
    Record({0, EncounterChangeKind::EntitySpawned, it->second.encounter, entity, {}, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> EncountersService::CompleteEncounter(EncounterInstanceId id, GameplayContext c)
{
    auto it = instances_.find(id);
    if (it == instances_.end())
        return foundation::Result<void>::Failure(Error("gameplay.encounters.instance_missing", "encounter missing"));
    Bump();
    it->second.state = EncounterState::Completed;
    it->second.last_updated_at = c.time;
    it->second.revision = revision_;
    Record({0, EncounterChangeKind::EncounterCompleted, id, {}, it->second.area, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> EncountersService::FailEncounter(EncounterInstanceId id, GameplayContext c)
{
    auto it = instances_.find(id);
    if (it == instances_.end())
        return foundation::Result<void>::Failure(Error("gameplay.encounters.instance_missing", "encounter missing"));
    Bump();
    it->second.state = EncounterState::Failed;
    it->second.last_updated_at = c.time;
    it->second.revision = revision_;
    Record({0, EncounterChangeKind::EncounterFailed, id, {}, it->second.area, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<RespawnRuleId> EncountersService::ScheduleRespawn(RespawnRule r, GameplayContext c)
{
    if (!r.encounter.IsValid() || !definitions_.contains(r.encounter))
        return foundation::Result<RespawnRuleId>::Failure(
            Error("gameplay.encounters.invalid_respawn", "invalid respawn rule"));
    if (!r.id.IsValid())
        r.id = RespawnRuleId{respawn_ids_.Next()};
    if (respawns_.contains(r.id))
        return foundation::Result<RespawnRuleId>::Failure(
            Error("gameplay.encounters.duplicate_respawn", "duplicate respawn rule"));
    Bump();
    r.revision = revision_;
    auto id = r.id;
    respawns_.emplace(id, r);
    ++diagnostics_.respawn_schedules;
    Record({0, EncounterChangeKind::RespawnScheduled, {}, {}, {}, c, revision_});
    return foundation::Result<RespawnRuleId>::Success(id);
}
std::vector<EncounterDefinition> EncountersService::FindDefinitionsByTag(TagId tag) const
{
    std::vector<EncounterDefinition> out;
    for (const auto &[id, d] : definitions_)
    {
        (void)id;
        if (d.tags.HasExact(tag))
            out.push_back(d);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}
const EncounterInstance *EncountersService::GetEncounterInstance(EncounterInstanceId id) const noexcept
{
    auto it = instances_.find(id);
    return it == instances_.end() ? nullptr : &it->second;
}
const SpawnTable *EncountersService::GetSpawnTable(SpawnTableId id) const noexcept
{
    auto it = tables_.find(id);
    return it == tables_.end() ? nullptr : &it->second;
}
const SpawnedEntityRecord *EncountersService::GetSpawnedEntityRecord(SpawnedEntityRecordId id) const noexcept
{
    auto it = spawned_.find(id);
    return it == spawned_.end() ? nullptr : &it->second;
}
std::vector<EncounterInstance> EncountersService::FindActiveEncountersInArea(GameplayObjectRef a) const
{
    std::vector<EncounterInstance> out;
    for (const auto &[id, e] : instances_)
    {
        (void)id;
        if (e.area == a && e.state == EncounterState::Active)
            out.push_back(e);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}
std::vector<SpawnPoint> EncountersService::FindSpawnPointsInArea(GameplayObjectRef a) const
{
    std::vector<SpawnPoint> out;
    for (const auto &[id, p] : points_)
    {
        (void)id;
        if (p.area == a)
            out.push_back(p);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}
std::vector<EncounterChange> EncountersService::ChangesSince(std::uint64_t s) const
{
    std::vector<EncounterChange> out;
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(out),
                 [s](const auto &c) { return c.sequence > s; });
    return out;
}
EncountersSnapshot EncountersService::CaptureSnapshot() const
{
    EncountersSnapshot s;
    for (const auto &[id, d] : definitions_)
    {
        (void)id;
        s.definitions.push_back(d);
    }
    for (const auto &[id, t] : tables_)
    {
        (void)id;
        s.tables.push_back(t);
    }
    for (const auto &[id, p] : points_)
    {
        (void)id;
        s.points.push_back(p);
    }
    for (const auto &[id, i] : instances_)
    {
        (void)id;
        s.instances.push_back(i);
    }
    for (const auto &[id, e] : spawned_)
    {
        (void)id;
        s.spawned.push_back(e);
    }
    for (const auto &[id, r] : respawns_)
    {
        (void)id;
        s.respawn_rules.push_back(r);
    }
    std::sort(s.definitions.begin(), s.definitions.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.tables.begin(), s.tables.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.points.begin(), s.points.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.instances.begin(), s.instances.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.spawned.begin(), s.spawned.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.respawn_rules.begin(), s.respawn_rules.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    s.instance_ids = instance_ids_.GetSnapshot();
    s.point_ids = point_ids_.GetSnapshot();
    s.request_ids = request_ids_.GetSnapshot();
    s.spawned_ids = spawned_ids_.GetSnapshot();
    s.respawn_ids = respawn_ids_.GetSnapshot();
    s.revision = revision_;
    return s;
}
foundation::Result<void> EncountersService::RestoreSnapshot(EncountersSnapshot s)
{
    definitions_.clear();
    tables_.clear();
    points_.clear();
    instances_.clear();
    spawned_.clear();
    respawns_.clear();
    for (auto &d : s.definitions)
    {
        if (!d.id.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.encounters.restore_invalid", "invalid definition"));
        definitions_[d.id] = d;
    }
    for (auto &t : s.tables)
    {
        if (!t.id.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.encounters.restore_invalid", "invalid table"));
        tables_[t.id] = t;
    }
    for (auto &p : s.points)
    {
        if (!p.id.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.encounters.restore_invalid", "invalid point"));
        points_[p.id] = p;
    }
    for (auto &i : s.instances)
    {
        if (!i.id.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.encounters.restore_invalid", "invalid instance"));
        instances_[i.id] = i;
    }
    for (auto &e : s.spawned)
    {
        if (!e.id.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.encounters.restore_invalid", "invalid spawned"));
        spawned_[e.id] = e;
    }
    for (auto &r : s.respawn_rules)
    {
        if (!r.id.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.encounters.restore_invalid", "invalid respawn"));
        respawns_[r.id] = r;
    }
    instance_ids_.Restore(s.instance_ids);
    point_ids_.Restore(s.point_ids);
    request_ids_.Restore(s.request_ids);
    spawned_ids_.Restore(s.spawned_ids);
    respawn_ids_.Restore(s.respawn_ids);
    revision_ = s.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    return foundation::Result<void>::Success();
}
EncounterDiagnostics EncountersService::GetDiagnostics() const noexcept
{
    auto d = diagnostics_;
    d.definitions = definitions_.size();
    d.tables = tables_.size();
    for (const auto &[id, e] : instances_)
    {
        (void)id;
        if (e.state == EncounterState::Active)
            ++d.active_encounters;
    }
    d.spawned_entities = spawned_.size();
    return d;
}
std::vector<SpawnEntry> EncountersService::ResolveEntries(const SpawnTable &table, random::RandomSeed seed) const
{
    auto entries = table.entries;
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    if (table.roll_policy == SpawnRollPolicy::GuaranteedAll || table.roll_policy == SpawnRollPolicy::PopulationBacked)
        return entries;
    std::vector<SpawnEntry> out;
    random::RandomSequence seq(seed, random::RandomStream::FromString("encounter.spawn"));
    if (table.roll_policy == SpawnRollPolicy::WeightedOne)
    {
        std::uint64_t total = 0;
        for (const auto &e : entries)
            total += e.weight;
        auto roll = seq.Uniform(total == 0 ? 1 : total);
        for (const auto &e : entries)
        {
            if (roll < e.weight)
            {
                out.push_back(e);
                break;
            }
            roll -= e.weight;
        }
        return out;
    }
    if (table.roll_policy == SpawnRollPolicy::WeightedMany ||
        table.roll_policy == SpawnRollPolicy::PickNWithoutReplacement)
    {
        const auto count = std::min<std::size_t>(entries.size(), 2);
        for (std::size_t i = 0; i < count && !entries.empty(); ++i)
        {
            auto index = seq.Uniform(entries.size());
            out.push_back(entries.at(index));
            if (table.roll_policy == SpawnRollPolicy::PickNWithoutReplacement)
                entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
        }
        return out;
    }
    if (table.roll_policy == SpawnRollPolicy::IndependentChance)
    {
        for (const auto &e : entries)
        {
            if (seq.RollMicro(static_cast<std::uint32_t>(std::min<std::uint64_t>(e.weight, 1'000'000))))
                out.push_back(e);
        }
        return out;
    }
    return out;
}
void EncountersService::Record(EncounterChange c)
{
    c.sequence = next_change_sequence_++;
    changes_.push_back(c);
}
} // namespace epidemic::gameplay::encounters
