#include "Epidemic/GameFramework/Encounters/encounters.h"
#include "Epidemic/Foundation/error.h"
#include <algorithm>
#include <limits>
#include <numeric>
#include <utility>

namespace epidemic::gameplay::encounters
{
namespace
{
foundation::Error Error(std::string_view c, std::string_view m) { return foundation::Error::Create(c, m); }

bool HasAllExact(const GameplayTagSet& actual, const GameplayTagSet& required) noexcept
{
    for (const auto tag : required.Values()) if (!actual.HasExact(tag)) return false;
    return true;
}
bool HasAnyExact(const GameplayTagSet& actual, const GameplayTagSet& blocked) noexcept
{
    for (const auto tag : blocked.Values()) if (actual.HasExact(tag)) return true;
    return false;
}
bool IsTerminal(EncounterState state) noexcept
{
    return state == EncounterState::Completed || state == EncounterState::Failed || state == EncounterState::Expired;
}
bool CountsAgainstActiveBudget(EncounterState state) noexcept
{
    return state == EncounterState::AwaitingPopulationBinding || state == EncounterState::Active || state == EncounterState::Despawning;
}

template <class TId>
void AdvanceForCallerId(MonotonicIdGenerator<GameplayObjectId>& generator, TId id) noexcept
{
    if (!id.IsValid()) return;
    auto snapshot = generator.GetSnapshot();
    if (id.value.High() != snapshot.scope || snapshot.next == 0 || id.value.Low() < snapshot.next) return;
    snapshot.next = id.value.Low() == std::numeric_limits<std::uint64_t>::max() ? 0 : id.value.Low() + 1;
    generator.Restore(snapshot);
}
template <class TId>
void ObserveMaxLow(TId id, std::uint64_t scope, std::uint64_t& max_low) noexcept
{
    if (id.IsValid() && id.value.High() == scope && id.value.Low() > max_low) max_low = id.value.Low();
}
}

foundation::Result<void> EncountersService::RegisterSpawnTable(SpawnTable t)
{
    if (definitions_frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.encounters.definitions_frozen", "encounter definitions are frozen"));
    if (!t.id.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.encounters.invalid_table", "invalid spawn table"));
    if (tables_.contains(t.id))
        return foundation::Result<void>::Failure(Error("gameplay.encounters.duplicate_table", "duplicate spawn table"));
    if (t.selection_min_count > t.selection_max_count)
        return foundation::Result<void>::Failure(Error("gameplay.encounters.invalid_selection_count", "invalid spawn table selection count"));
    if ((t.roll_policy == SpawnRollPolicy::WeightedMany || t.roll_policy == SpawnRollPolicy::PickNWithoutReplacement) &&
        t.selection_max_count == 0)
        return foundation::Result<void>::Failure(Error("gameplay.encounters.invalid_selection_count", "selection policy requires a positive count"));
    for (const auto& e : t.entries)
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
    if (definitions_frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.encounters.definitions_frozen", "encounter definitions are frozen"));
    if (!d.id.IsValid() || !d.spawn_table.IsValid() || !tables_.contains(d.spawn_table))
        return foundation::Result<void>::Failure(Error("gameplay.encounters.invalid_definition", "invalid encounter definition"));
    if (definitions_.contains(d.id))
        return foundation::Result<void>::Failure(Error("gameplay.encounters.duplicate_definition", "duplicate encounter definition"));
    Bump(); d.revision = revision_; definitions_.emplace(d.id, std::move(d));
    return foundation::Result<void>::Success();
}

foundation::Result<void> EncountersService::FreezeDefinitions()
{
    if (definitions_frozen_) return foundation::Result<void>::Success();
    for (const auto& [id, d] : definitions_)
        if (!id.IsValid() || !tables_.contains(d.spawn_table))
            return foundation::Result<void>::Failure(Error("gameplay.encounters.freeze_invalid_definition", "encounter definition references missing table"));
    definitions_frozen_ = true;
    return foundation::Result<void>::Success();
}

foundation::Result<SpawnPointId> EncountersService::AddSpawnPoint(SpawnPoint p)
{
    if (!p.area.IsValid() || !p.position.IsValid())
        return foundation::Result<SpawnPointId>::Failure(Error("gameplay.encounters.invalid_spawn_point", "spawn point requires valid area and position"));
    if (!p.id.IsValid())
    {
        const auto next = point_ids_.Next();
        if (!next.IsValid()) return foundation::Result<SpawnPointId>::Failure(Error("gameplay.encounters.id_exhausted", "spawn point id exhausted"));
        p.id = SpawnPointId{next};
    }
    else AdvanceForCallerId(point_ids_, p.id);
    if (points_.contains(p.id))
        return foundation::Result<SpawnPointId>::Failure(Error("gameplay.encounters.duplicate_spawn_point", "duplicate spawn point"));
    Bump(); p.revision = revision_; const auto id = p.id; points_.emplace(id, std::move(p));
    return foundation::Result<SpawnPointId>::Success(id);
}

bool EncountersService::EntryMatches(const SpawnEntry& e, const SpawnRequest& r) const noexcept
{
    return HasAllExact(r.area_tags, e.required_area_tags) && !HasAnyExact(r.area_tags, e.blocked_area_tags) &&
           HasAllExact(r.request_tags, e.required_request_tags) && !HasAnyExact(r.request_tags, e.blocked_request_tags);
}

bool EncountersService::RequestMatches(const SpawnRequest& a, const SpawnRequest& b) const noexcept
{
    return a.encounter == b.encounter && a.area == b.area && a.origin == b.origin && a.spawn_point == b.spawn_point &&
           a.seed == b.seed && a.persistence == b.persistence && a.area_tags.Values() == b.area_tags.Values() &&
           a.request_tags.Values() == b.request_tags.Values();
}

bool EncountersService::CanSpawnEncounter(SpawnRequest r) const
{
    if (!r.encounter.IsValid() || !r.area.IsValid()) return false;
    const auto dit = definitions_.find(r.encounter);
    if (dit == definitions_.end() || !tables_.contains(dit->second.spawn_table)) return false;
    if (!HasAllExact(r.area_tags, dit->second.required_area_tags) || HasAnyExact(r.area_tags, dit->second.blocked_area_tags)) return false;
    if (r.spawn_point)
    {
        const auto pit = points_.find(*r.spawn_point);
        if (pit == points_.end() || pit->second.area != r.area || pit->second.state != SpawnPointState::Available) return false;
        if (!HasAllExact(pit->second.tags, dit->second.required_spawn_point_tags) ||
            HasAnyExact(pit->second.tags, dit->second.blocked_spawn_point_tags)) return false;
    }
    std::uint32_t global = 0, area = 0;
    for (const auto& [id, e] : instances_)
    {
        (void)id;
        if (!CountsAgainstActiveBudget(e.state)) continue;
        ++global; if (e.area == r.area) ++area;
    }
    return global < budgets_.max_active_global && area < budgets_.max_active_per_area;
}

foundation::Result<std::vector<SpawnEntry>> EncountersService::ResolveEntries(const SpawnTable& table,
                                                                                const SpawnRequest& request) const
{
    std::vector<SpawnEntry> entries;
    for (const auto& entry : table.entries) if (EntryMatches(entry, request)) entries.push_back(entry);
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b){ return a.id < b.id; });
    if (table.roll_policy == SpawnRollPolicy::GuaranteedAll || table.roll_policy == SpawnRollPolicy::PopulationBacked)
        return foundation::Result<std::vector<SpawnEntry>>::Success(std::move(entries));

    std::vector<SpawnEntry> out;
    random::RandomSequence seq(request.seed, random::RandomStream::FromString("encounter.spawn"));
    if (table.roll_policy == SpawnRollPolicy::WeightedOne)
    {
        std::vector<std::uint64_t> weights; for (const auto& e : entries) weights.push_back(e.weight);
        if (const auto index = seq.WeightedIndex(weights); index) out.push_back(entries[*index]);
        return foundation::Result<std::vector<SpawnEntry>>::Success(std::move(out));
    }
    if (table.roll_policy == SpawnRollPolicy::IndependentChance)
    {
        for (const auto& e : entries)
            if (const auto rolled = seq.TryRollMicro(static_cast<std::uint32_t>(std::min<std::uint64_t>(e.weight, 1'000'000))); rolled && *rolled)
                out.push_back(e);
        return foundation::Result<std::vector<SpawnEntry>>::Success(std::move(out));
    }
    if (table.roll_policy == SpawnRollPolicy::WeightedMany || table.roll_policy == SpawnRollPolicy::PickNWithoutReplacement)
    {
        if (entries.empty()) return foundation::Result<std::vector<SpawnEntry>>::Success({});
        const auto min_count = std::min<std::size_t>(table.selection_min_count, entries.size());
        const auto max_count = std::min<std::size_t>(table.selection_max_count, entries.size());
        if (min_count > max_count)
            return foundation::Result<std::vector<SpawnEntry>>::Failure(Error("gameplay.encounters.selection_impossible", "selection count exceeds filtered entry count"));
        std::size_t count = min_count;
        if (max_count > min_count)
        {
            const auto v = seq.TryUniform(static_cast<std::uint64_t>(max_count - min_count + 1));
            if (!v) return foundation::Result<std::vector<SpawnEntry>>::Failure(Error("gameplay.encounters.random_exhausted", "random sequence exhausted"));
            count += static_cast<std::size_t>(*v);
        }
        for (std::size_t i = 0; i < count; ++i)
        {
            std::size_t index = 0;
            if (table.roll_policy == SpawnRollPolicy::WeightedMany)
            {
                std::vector<std::uint64_t> weights; for (const auto& e : entries) weights.push_back(e.weight);
                const auto weighted = seq.WeightedIndex(weights);
                if (!weighted) return foundation::Result<std::vector<SpawnEntry>>::Failure(Error("gameplay.encounters.invalid_weights", "weighted selection has no selectable entry"));
                index = *weighted;
            }
            else
            {
                const auto selected = seq.TryUniform(static_cast<std::uint64_t>(entries.size()));
                if (!selected) return foundation::Result<std::vector<SpawnEntry>>::Failure(Error("gameplay.encounters.random_exhausted", "random sequence exhausted"));
                index = static_cast<std::size_t>(*selected);
            }
            out.push_back(entries[index]);
            entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
        }
        return foundation::Result<std::vector<SpawnEntry>>::Success(std::move(out));
    }
    return foundation::Result<std::vector<SpawnEntry>>::Success(std::move(out));
}

foundation::Result<std::vector<TypeId>> EncountersService::PreviewSpawnArchetypes(SpawnRequest r) const
{
    if (!CanSpawnEncounter(r))
        return foundation::Result<std::vector<TypeId>>::Failure(
            Error("gameplay.encounters.spawn_preview_rejected", "encounter spawn request is not currently valid"));
    const auto& def = definitions_.at(r.encounter);
    const auto& table = tables_.at(def.spawn_table);
    const auto resolved = ResolveEntries(table, r);
    if (!resolved)
        return foundation::Result<std::vector<TypeId>>::Failure(resolved.GetError());

    std::vector<TypeId> archetypes;
    std::uint64_t total = 0;
    for (const auto& entry : resolved.Value())
    {
        random::RandomSequence seq(random::DeriveSeed(r.seed, random::RandomStream::FromString("encounter.count"), entry.id.value.Raw()),
                                   random::RandomStream::FromString("encounter.count"));
        std::uint32_t count = entry.min_count;
        if (entry.max_count > entry.min_count)
        {
            const auto rolled = seq.TryUniform(static_cast<std::uint64_t>(entry.max_count - entry.min_count) + 1u);
            if (!rolled)
                return foundation::Result<std::vector<TypeId>>::Failure(
                    Error("gameplay.encounters.random_exhausted", "random sequence exhausted"));
            count += static_cast<std::uint32_t>(*rolled);
        }
        if (total > std::numeric_limits<std::uint64_t>::max() - count)
            return foundation::Result<std::vector<TypeId>>::Failure(
                Error("gameplay.encounters.spawn_preview_overflow", "encounter spawn preview count overflow"));
        total += count;
        if (total > budgets_.max_spawned_entities_per_encounter)
            return foundation::Result<std::vector<TypeId>>::Failure(
                Error("gameplay.encounters.spawn_preview_budget", "encounter spawn preview exceeds entity budget"));
        for (std::uint32_t i = 0; i < count; ++i)
            archetypes.push_back(entry.archetype);
    }
    return foundation::Result<std::vector<TypeId>>::Success(std::move(archetypes));
}

SpawnResult EncountersService::SpawnEncounter(SpawnRequest r){
    ++diagnostics_.spawn_requests;
    SpawnResult result;
    if (!r.id.IsValid())
    {
        const auto next = request_ids_.Next();
        if (!next.IsValid()) { ++diagnostics_.spawn_failures; result.state = SpawnResultState::Failed; return result; }
        r.id = SpawnRequestId{next};
    }
    else AdvanceForCallerId(request_ids_, r.id);
    result.request_id = r.id;
    if (r.spawn_point)
    {
        const auto point = points_.find(*r.spawn_point);
        if (point != points_.end() && !r.origin) r.origin = point->second.position;
    }
    if (const auto existing = requests_.find(r.id); existing != requests_.end())
    {
        if (RequestMatches(existing->second.request, r)) return existing->second.result;
        ++diagnostics_.spawn_failures; result.state = SpawnResultState::Rejected; result.revision = revision_; return result;
    }

    auto store_request = [&](SpawnResult value)
    {
        SpawnRequestRecord record{r, value, revision_}; requests_.emplace(r.id, std::move(record)); return value;
    };
    result.revision = revision_;
    Record({0, EncounterChangeKind::SpawnRequested, {}, {}, r.area, r.context, revision_});
    if (!CanSpawnEncounter(r))
    {
        ++diagnostics_.spawn_budget_rejects; ++diagnostics_.spawn_failures; result.state = SpawnResultState::Rejected;
        Record({0, EncounterChangeKind::SpawnFailed, {}, {}, r.area, r.context, revision_}); return store_request(result);
    }
    const auto& def = definitions_.at(r.encounter);
    const auto& table = tables_.at(def.spawn_table);
    const auto resolved = ResolveEntries(table, r);
    if (!resolved)
    {
        ++diagnostics_.spawn_failures; result.state = SpawnResultState::Rejected;
        Record({0, EncounterChangeKind::SpawnFailed, {}, {}, r.area, r.context, revision_}); return store_request(result);
    }

    struct Planned { SpawnEntry entry; std::uint32_t count = 0; };
    std::vector<Planned> planned;
    std::uint64_t total = 0;
    for (const auto& entry : resolved.Value())
    {
        random::RandomSequence seq(random::DeriveSeed(r.seed, random::RandomStream::FromString("encounter.count"), entry.id.value.Raw()),
                                   random::RandomStream::FromString("encounter.count"));
        std::uint32_t count = entry.min_count;
        if (entry.max_count > entry.min_count)
        {
            const auto v = seq.TryUniform(static_cast<std::uint64_t>(entry.max_count - entry.min_count) + 1u);
            if (!v) { ++diagnostics_.spawn_failures; result.state = SpawnResultState::Failed; return store_request(result); }
            count += static_cast<std::uint32_t>(*v);
        }
        if (total > std::numeric_limits<std::uint64_t>::max() - count) { ++diagnostics_.spawn_failures; result.state = SpawnResultState::Rejected; return store_request(result); }
        total += count; planned.push_back({entry, count});
    }
    if (total > budgets_.max_spawned_entities_per_encounter)
    {
        ++diagnostics_.spawn_budget_rejects; ++diagnostics_.spawn_failures; result.state = SpawnResultState::Rejected;
        Record({0, EncounterChangeKind::SpawnFailed, {}, {}, r.area, r.context, revision_}); return store_request(result);
    }
    if (spawn_budget_tick_ != r.context.tick) { spawn_budget_tick_ = r.context.tick; spawn_operations_used_ = 0; }
    if (total > budgets_.max_spawn_operations_per_tick ||
        spawn_operations_used_ > budgets_.max_spawn_operations_per_tick - static_cast<std::uint32_t>(total))
    {
        result.state = SpawnResultState::Deferred;
        return result;
    }

    auto staged_instance_ids = instance_ids_;
    auto staged_spawned_ids = spawned_ids_;
    const auto instance_raw = staged_instance_ids.Next();
    if (!instance_raw.IsValid()) { ++diagnostics_.spawn_failures; result.state = SpawnResultState::Failed; return store_request(result); }
    EncounterInstance instance;
    instance.id = EncounterInstanceId{instance_raw}; instance.definition = r.encounter; instance.area = r.area;
    instance.origin = r.origin.value_or(r.area); instance.spawn_point = r.spawn_point; instance.created_at = r.context.time; instance.last_updated_at = r.context.time;
    const bool population_backed = r.persistence == SpawnPersistencePolicy::PopulationBacked ||
                                   def.persistence == SpawnPersistencePolicy::PopulationBacked ||
                                   table.roll_policy == SpawnRollPolicy::PopulationBacked;
    instance.state = population_backed ? EncounterState::AwaitingPopulationBinding : EncounterState::Active;

    std::vector<SpawnedEntityRecord> staged_records;
    for (const auto& p : planned) for (std::uint32_t i = 0; i < p.count; ++i)
    {
        const auto raw = staged_spawned_ids.Next();
        if (!raw.IsValid()) { ++diagnostics_.spawn_failures; result.state = SpawnResultState::Failed; return store_request(result); }
        SpawnedEntityRecord rec; rec.id = SpawnedEntityRecordId{raw}; rec.encounter = instance.id; rec.archetype = p.entry.archetype;
        staged_records.push_back(rec); instance.spawned_entities.push_back(rec.id); result.spawned_records.push_back(rec.id);
    }

    Bump(); instance.revision = revision_;
    for (auto& rec : staged_records) { rec.revision = revision_; spawned_.emplace(rec.id, rec); }
    instance_ids_ = staged_instance_ids; spawned_ids_ = staged_spawned_ids;
    result.encounter_instance = instance.id; result.state = population_backed ? SpawnResultState::Deferred : SpawnResultState::Succeeded;
    result.revision = revision_; instances_.emplace(instance.id, instance); instances_by_area_[instance.area].push_back(instance.id);
    std::sort(instances_by_area_[instance.area].begin(), instances_by_area_[instance.area].end()); instances_by_state_[instance.state].push_back(instance.id); std::sort(instances_by_state_[instance.state].begin(), instances_by_state_[instance.state].end());
    spawn_operations_used_ += static_cast<std::uint32_t>(total); ++diagnostics_.spawn_successes;
    Record({0, EncounterChangeKind::EncounterCreated, instance.id, {}, r.area, r.context, revision_});
    if (!population_backed) Record({0, EncounterChangeKind::EncounterActivated, instance.id, {}, r.area, r.context, revision_});
    Record({0, EncounterChangeKind::SpawnSucceeded, instance.id, {}, r.area, r.context, revision_});
    return store_request(result);
}

foundation::Result<void> EncountersService::BindPopulationUnit(SpawnedEntityRecordId id, GameplayObjectRef unit, GameplayContext c)
{
    auto it = spawned_.find(id);
    if (it == spawned_.end() || !unit.IsValid()) return foundation::Result<void>::Failure(Error("gameplay.encounters.spawned_record_missing", "spawned record missing or population unit invalid"));
    auto encounter = instances_.find(it->second.encounter);
    if (encounter == instances_.end() || encounter->second.state != EncounterState::AwaitingPopulationBinding)
        return foundation::Result<void>::Failure(Error("gameplay.encounters.population_binding_not_expected", "encounter is not awaiting population binding"));
    if (it->second.population_unit.IsValid())
        return it->second.population_unit == unit ? foundation::Result<void>::Success() : foundation::Result<void>::Failure(Error("gameplay.encounters.population_already_bound", "spawn record already bound to another population unit"));
    for (const auto& [rid, rec] : spawned_) if (rid != id && rec.population_unit == unit)
        return foundation::Result<void>::Failure(Error("gameplay.encounters.population_unit_in_use", "population unit already bound to another spawn record"));
    Bump(); it->second.population_unit = unit; it->second.revision = revision_; encounter->second.revision = revision_; encounter->second.last_updated_at = c.time;
    return foundation::Result<void>::Success();
}

foundation::Result<void> EncountersService::ActivatePopulationBackedEncounter(EncounterInstanceId id, GameplayContext c)
{
    auto it = instances_.find(id);
    if (it == instances_.end()) return foundation::Result<void>::Failure(Error("gameplay.encounters.instance_missing", "encounter missing"));
    if (it->second.state == EncounterState::Active) return foundation::Result<void>::Success();
    if (it->second.state != EncounterState::AwaitingPopulationBinding)
        return foundation::Result<void>::Failure(Error("gameplay.encounters.invalid_state", "encounter is not awaiting population binding"));
    for (const auto record_id : it->second.spawned_entities)
    {
        const auto rec = spawned_.find(record_id);
        if (rec == spawned_.end() || !rec->second.population_unit.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.encounters.population_binding_incomplete", "all population-backed spawn slots must be bound"));
    }
    Bump(); it->second.state = EncounterState::Active; it->second.last_updated_at = c.time; it->second.revision = revision_; RebuildIndexes();
    Record({0, EncounterChangeKind::EncounterActivated, id, {}, it->second.area, c, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> EncountersService::BindSpawnedEntity(SpawnedEntityRecordId id, GameplayObjectRef entity, GameplayContext c)
{
    auto it = spawned_.find(id);
    if (it == spawned_.end() || !entity.IsValid()) return foundation::Result<void>::Failure(Error("gameplay.encounters.spawned_record_missing", "spawned record missing or entity invalid"));
    if (it->second.entity.IsValid()) return it->second.entity == entity ? foundation::Result<void>::Success() : foundation::Result<void>::Failure(Error("gameplay.encounters.spawned_record_already_bound", "spawned record already bound"));
    Bump(); it->second.entity = entity; it->second.revision = revision_;
    Record({0, EncounterChangeKind::EntitySpawned, it->second.encounter, entity, {}, c, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> EncountersService::CompleteEncounter(EncounterInstanceId id, GameplayContext c)
{
    auto it = instances_.find(id); if (it == instances_.end()) return foundation::Result<void>::Failure(Error("gameplay.encounters.instance_missing", "encounter missing"));
    if (IsTerminal(it->second.state)) return it->second.state == EncounterState::Completed ? foundation::Result<void>::Success() : foundation::Result<void>::Failure(Error("gameplay.encounters.invalid_state", "encounter already terminal"));
    Bump(); it->second.state = EncounterState::Completed; it->second.last_updated_at = c.time; it->second.revision = revision_; RebuildIndexes();
    Record({0, EncounterChangeKind::EncounterCompleted, id, {}, it->second.area, c, revision_}); return foundation::Result<void>::Success();
}
foundation::Result<void> EncountersService::FailEncounter(EncounterInstanceId id, GameplayContext c)
{
    auto it = instances_.find(id); if (it == instances_.end()) return foundation::Result<void>::Failure(Error("gameplay.encounters.instance_missing", "encounter missing"));
    if (IsTerminal(it->second.state)) return it->second.state == EncounterState::Failed ? foundation::Result<void>::Success() : foundation::Result<void>::Failure(Error("gameplay.encounters.invalid_state", "encounter already terminal"));
    Bump(); it->second.state = EncounterState::Failed; it->second.last_updated_at = c.time; it->second.revision = revision_; RebuildIndexes();
    Record({0, EncounterChangeKind::EncounterFailed, id, {}, it->second.area, c, revision_}); return foundation::Result<void>::Success();
}
foundation::Result<void> EncountersService::BeginDespawningEncounter(EncounterInstanceId id, GameplayContext c)
{
    auto it = instances_.find(id); if (it == instances_.end()) return foundation::Result<void>::Failure(Error("gameplay.encounters.instance_missing", "encounter missing"));
    if (it->second.state != EncounterState::Active)
        return foundation::Result<void>::Failure(Error("gameplay.encounters.invalid_state", "only active encounter can despawn"));
    Bump(); it->second.state = EncounterState::Despawning; it->second.last_updated_at = c.time; it->second.revision = revision_; RebuildIndexes(); return foundation::Result<void>::Success();
}
foundation::Result<void> EncountersService::ExpireEncounter(EncounterInstanceId id, GameplayContext c)
{
    auto it = instances_.find(id); if (it == instances_.end()) return foundation::Result<void>::Failure(Error("gameplay.encounters.instance_missing", "encounter missing"));
    if (IsTerminal(it->second.state)) return it->second.state == EncounterState::Expired ? foundation::Result<void>::Success() : foundation::Result<void>::Failure(Error("gameplay.encounters.invalid_state", "encounter already terminal"));
    Bump(); it->second.state = EncounterState::Expired; it->second.last_updated_at = c.time; it->second.revision = revision_; RebuildIndexes();
    Record({0, EncounterChangeKind::EncounterExpired, id, {}, it->second.area, c, revision_}); return foundation::Result<void>::Success();
}

std::size_t EncountersService::PruneTerminalEncounters(std::size_t max_to_prune)
{
    std::vector<EncounterInstanceId> ids;
    for (const auto& [id, e] : instances_) if (IsTerminal(e.state)) ids.push_back(id);
    std::sort(ids.begin(), ids.end()); if (ids.size() > max_to_prune) ids.resize(max_to_prune);
    for (const auto id : ids)
    {
        const auto instance = instances_.at(id);
        for (const auto record : instance.spawned_entities) spawned_.erase(record);
        instances_.erase(id);
        for (auto it = requests_.begin(); it != requests_.end();) it = it->second.result.encounter_instance == id ? requests_.erase(it) : std::next(it);
    }
    if (!ids.empty()) RebuildIndexes();
    return ids.size();
}

foundation::Result<RespawnRuleId> EncountersService::ScheduleRespawn(RespawnRule r, GameplayContext c)
{
    if (!r.encounter.IsValid() || !definitions_.contains(r.encounter) || !r.area.IsValid() || r.delay.ticks <= 0)
        return foundation::Result<RespawnRuleId>::Failure(Error("gameplay.encounters.invalid_respawn", "invalid respawn rule"));
    if (r.spawn_point)
    {
        const auto pit = points_.find(*r.spawn_point);
        if (pit == points_.end() || pit->second.area != r.area)
            return foundation::Result<RespawnRuleId>::Failure(Error("gameplay.encounters.invalid_respawn_point", "respawn point missing or in another area"));
    }
    if (!r.id.IsValid())
    {
        const auto raw = respawn_ids_.Next(); if (!raw.IsValid()) return foundation::Result<RespawnRuleId>::Failure(Error("gameplay.encounters.id_exhausted", "respawn id exhausted"));
        r.id = RespawnRuleId{raw};
    }
    else AdvanceForCallerId(respawn_ids_, r.id);
    if (respawns_.contains(r.id)) return foundation::Result<RespawnRuleId>::Failure(Error("gameplay.encounters.duplicate_respawn", "duplicate respawn rule"));
    const auto due = CheckedAdd(c.time, r.delay); if (!due) return foundation::Result<RespawnRuleId>::Failure(Error("gameplay.encounters.respawn_time_overflow", "respawn due time overflow"));
    r.next_due_at = *due; Bump(); r.revision = revision_; const auto id = r.id; respawns_.emplace(id, r); ++diagnostics_.respawn_schedules;
    Record({0, EncounterChangeKind::RespawnScheduled, {}, {}, r.area, c, revision_}); return foundation::Result<RespawnRuleId>::Success(id);
}

std::vector<SpawnResult> EncountersService::ProcessDueRespawns(GameplayTimePoint now, std::size_t max_rules, GameplayContext c)
{
    struct Due { RespawnRuleId id; GameplayTimePoint at; };
    std::vector<Due> due; for (const auto& [id, r] : respawns_) if (r.next_due_at <= now) due.push_back({id, r.next_due_at});
    std::sort(due.begin(), due.end(), [](const auto& a, const auto& b){ return a.at == b.at ? a.id < b.id : a.at < b.at; });
    if (due.size() > max_rules) due.resize(max_rules);
    std::vector<SpawnResult> results;
    for (const auto& item : due)
    {
        auto it = respawns_.find(item.id); if (it == respawns_.end()) continue;
        auto& rule = it->second; SpawnRequest request; request.encounter = rule.encounter; request.area = rule.area;
        request.origin = rule.origin; request.spawn_point = rule.spawn_point;
        request.seed = random::DeriveSeed(rule.seed, random::RandomStream::FromString("encounter.respawn"), rule.trigger_count);
        request.context = c; request.context.time = rule.next_due_at;
        auto result = SpawnEncounter(request); results.push_back(result);
        if (result.state == SpawnResultState::Deferred || result.state == SpawnResultState::Rejected) continue;
        if (result.state == SpawnResultState::Failed) continue;
        Bump(); ++rule.trigger_count; rule.revision = revision_;
        Record({0, EncounterChangeKind::RespawnTriggered, result.encounter_instance, {}, rule.area, request.context, revision_});
        if (rule.remaining_limit > 0)
        {
            --rule.remaining_limit; if (rule.remaining_limit == 0) { respawns_.erase(it); continue; }
        }
        const auto next = CheckedAdd(rule.next_due_at, rule.delay);
        if (!next) { respawns_.erase(it); continue; }
        rule.next_due_at = *next;
    }
    return results;
}

std::vector<EncounterDefinition> EncountersService::FindDefinitionsByTag(TagId tag) const
{
    std::vector<EncounterDefinition> out; for (const auto& [id,d] : definitions_) { (void)id; if (d.tags.HasExact(tag)) out.push_back(d); }
    std::sort(out.begin(), out.end(), [](const auto& a,const auto& b){return a.id<b.id;}); return out;
}
const EncounterInstance* EncountersService::GetEncounterInstance(EncounterInstanceId id) const noexcept { const auto it=instances_.find(id); return it==instances_.end()?nullptr:&it->second; }
const SpawnTable* EncountersService::GetSpawnTable(SpawnTableId id) const noexcept { const auto it=tables_.find(id); return it==tables_.end()?nullptr:&it->second; }
const SpawnedEntityRecord* EncountersService::GetSpawnedEntityRecord(SpawnedEntityRecordId id) const noexcept { const auto it=spawned_.find(id); return it==spawned_.end()?nullptr:&it->second; }

std::vector<EncounterInstance> EncountersService::FindActiveEncountersInArea(GameplayObjectRef area) const
{
    std::vector<EncounterInstance> out; const auto found=instances_by_area_.find(area); if(found==instances_by_area_.end()) return out;
    for(const auto id:found->second){const auto it=instances_.find(id); if(it!=instances_.end() && CountsAgainstActiveBudget(it->second.state)) out.push_back(it->second);}
    std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.id<b.id;}); return out;
}
std::vector<EncounterInstance> EncountersService::FindEncountersByState(EncounterState state) const
{
    std::vector<EncounterInstance> out; const auto found=instances_by_state_.find(state); if(found==instances_by_state_.end()) return out;
    for(const auto id:found->second){const auto it=instances_.find(id);if(it!=instances_.end())out.push_back(it->second);}
    return out;
}

std::vector<SpawnPoint> EncountersService::FindSpawnPointsInArea(GameplayObjectRef area) const
{
    std::vector<SpawnPoint> out; for(const auto&[id,p]:points_){(void)id;if(p.area==area)out.push_back(p);} std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.id<b.id;}); return out;
}

EncounterChangeBatch EncountersService::ReadChangesSince(std::uint64_t sequence) const
{
    EncounterChangeBatch batch;
    batch.latest_sequence = next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max()
                                                        : next_change_sequence_ - 1;
    batch.oldest_available_sequence = changes_.empty() ? next_change_sequence_ : changes_.front().sequence;
    if (next_change_sequence_ == 0 || sequence > batch.latest_sequence)
    {
        batch.snapshot_required = true;
        return batch;
    }
    if (changes_.empty())
    {
        batch.snapshot_required = sequence < batch.latest_sequence;
        return batch;
    }
    if (sequence < batch.oldest_available_sequence && batch.oldest_available_sequence - sequence > 1)
    {
        batch.snapshot_required = true;
        return batch;
    }
    for (const auto& change : changes_)
        if (change.sequence > sequence)
            batch.changes.push_back(change);
    return batch;
}

EncountersSnapshot EncountersService::CaptureSnapshot() const
{
    EncountersSnapshot s; for(const auto&[id,d]:definitions_){(void)id;s.definitions.push_back(d);} for(const auto&[id,t]:tables_){(void)id;s.tables.push_back(t);}
    for(const auto&[id,p]:points_){(void)id;s.points.push_back(p);} for(const auto&[id,i]:instances_){(void)id;s.instances.push_back(i);}
    for(const auto&[id,e]:spawned_){(void)id;s.spawned.push_back(e);} for(const auto&[id,r]:respawns_){(void)id;s.respawn_rules.push_back(r);}
    for(const auto&[id,r]:requests_){(void)id;s.requests.push_back(r);}
    std::sort(s.definitions.begin(),s.definitions.end(),[](const auto&a,const auto&b){return a.id<b.id;}); std::sort(s.tables.begin(),s.tables.end(),[](const auto&a,const auto&b){return a.id<b.id;});
    std::sort(s.points.begin(),s.points.end(),[](const auto&a,const auto&b){return a.id<b.id;}); std::sort(s.instances.begin(),s.instances.end(),[](const auto&a,const auto&b){return a.id<b.id;});
    std::sort(s.spawned.begin(),s.spawned.end(),[](const auto&a,const auto&b){return a.id<b.id;}); std::sort(s.respawn_rules.begin(),s.respawn_rules.end(),[](const auto&a,const auto&b){return a.id<b.id;});
    std::sort(s.requests.begin(),s.requests.end(),[](const auto&a,const auto&b){return a.request.id<b.request.id;});
    s.instance_ids=instance_ids_.GetSnapshot(); s.point_ids=point_ids_.GetSnapshot(); s.request_ids=request_ids_.GetSnapshot(); s.spawned_ids=spawned_ids_.GetSnapshot(); s.respawn_ids=respawn_ids_.GetSnapshot(); s.revision=revision_; return s;
}

foundation::Result<void> EncountersService::RestoreSnapshot(EncountersSnapshot s)
{
    decltype(definitions_) definitions; decltype(tables_) tables; decltype(points_) points; decltype(instances_) instances; decltype(spawned_) spawned; decltype(respawns_) respawns; decltype(requests_) requests;
    if (definitions_frozen_)
    {
        definitions = definitions_;
        tables = tables_;
    }
    else
    {
        for(auto& t:s.tables){if(!t.id.IsValid()||tables.contains(t.id)||t.selection_min_count>t.selection_max_count)return foundation::Result<void>::Failure(Error("gameplay.encounters.restore_invalid","invalid or duplicate table")); for(const auto&e:t.entries)if(!e.id.IsValid()||!e.archetype.IsValid()||e.min_count>e.max_count)return foundation::Result<void>::Failure(Error("gameplay.encounters.restore_invalid","invalid spawn entry")); tables.emplace(t.id,t);}
        for(auto& d:s.definitions){if(!d.id.IsValid()||definitions.contains(d.id)||!tables.contains(d.spawn_table))return foundation::Result<void>::Failure(Error("gameplay.encounters.restore_invalid","invalid definition"));definitions.emplace(d.id,d);}
    }
    std::uint64_t max_point=0,max_instance=0,max_spawned=0,max_respawn=0,max_request=0;
    for(auto& p:s.points){if(!p.id.IsValid()||!p.area.IsValid()||!p.position.IsValid()||points.contains(p.id))return foundation::Result<void>::Failure(Error("gameplay.encounters.restore_invalid","invalid point"));ObserveMaxLow(p.id,s.point_ids.scope,max_point);points.emplace(p.id,p);}
    for(auto& i:s.instances){if(!i.id.IsValid()||!i.area.IsValid()||!definitions.contains(i.definition)||instances.contains(i.id)||(i.spawn_point&&!points.contains(*i.spawn_point)))return foundation::Result<void>::Failure(Error("gameplay.encounters.restore_invalid","invalid instance"));ObserveMaxLow(i.id,s.instance_ids.scope,max_instance);instances.emplace(i.id,i);}
    for(auto& e:s.spawned){if(!e.id.IsValid()||!instances.contains(e.encounter)||spawned.contains(e.id))return foundation::Result<void>::Failure(Error("gameplay.encounters.restore_invalid","invalid spawned record"));ObserveMaxLow(e.id,s.spawned_ids.scope,max_spawned);spawned.emplace(e.id,e);}
    for(const auto&[id,i]:instances){for(const auto rid:i.spawned_entities){const auto it=spawned.find(rid);if(it==spawned.end()||it->second.encounter!=id)return foundation::Result<void>::Failure(Error("gameplay.encounters.restore_invalid","instance spawn reference mismatch"));}}
    for(auto& r:s.respawn_rules){if(!r.id.IsValid()||!definitions.contains(r.encounter)||!r.area.IsValid()||r.delay.ticks<=0||respawns.contains(r.id))return foundation::Result<void>::Failure(Error("gameplay.encounters.restore_invalid","invalid respawn")); if(r.spawn_point&&!points.contains(*r.spawn_point))return foundation::Result<void>::Failure(Error("gameplay.encounters.restore_invalid","respawn point missing"));ObserveMaxLow(r.id,s.respawn_ids.scope,max_respawn);respawns.emplace(r.id,r);}
    for(auto& r:s.requests){if(!r.request.id.IsValid()||requests.contains(r.request.id))return foundation::Result<void>::Failure(Error("gameplay.encounters.restore_invalid","invalid request record")); if(r.result.encounter_instance.IsValid()&&!instances.contains(r.result.encounter_instance))return foundation::Result<void>::Failure(Error("gameplay.encounters.restore_invalid","request references missing encounter"));ObserveMaxLow(r.request.id,s.request_ids.scope,max_request);requests.emplace(r.request.id,r);}
    if(!ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(s.instance_ids,instance_ids_.Scope(),max_instance)||!ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(s.point_ids,point_ids_.Scope(),max_point)||!ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(s.request_ids,request_ids_.Scope(),max_request)||!ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(s.spawned_ids,spawned_ids_.Scope(),max_spawned)||!ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(s.respawn_ids,respawn_ids_.Scope(),max_respawn))return foundation::Result<void>::Failure(Error("gameplay.encounters.restore_invalid_generator","invalid encounter id generator snapshot"));
    definitions_=std::move(definitions);tables_=std::move(tables);points_=std::move(points);instances_=std::move(instances);spawned_=std::move(spawned);respawns_=std::move(respawns);requests_=std::move(requests);
    instance_ids_.Restore(s.instance_ids);point_ids_.Restore(s.point_ids);request_ids_.Restore(s.request_ids);spawned_ids_.Restore(s.spawned_ids);respawn_ids_.Restore(s.respawn_ids);revision_=s.revision;changes_.clear();next_change_sequence_=1;spawn_budget_tick_={};spawn_operations_used_=0;RebuildIndexes();return foundation::Result<void>::Success();
}

void EncountersService::RebuildIndexes()
{
    instances_by_area_.clear(); instances_by_state_.clear(); for(const auto&[id,e]:instances_){instances_by_area_[e.area].push_back(id);instances_by_state_[e.state].push_back(id);} for(auto&[area,ids]:instances_by_area_){(void)area;std::sort(ids.begin(),ids.end());} for(auto&[state,ids]:instances_by_state_){(void)state;std::sort(ids.begin(),ids.end());}
}

EncounterDiagnostics EncountersService::GetDiagnostics() const noexcept
{
    auto d=diagnostics_;d.definitions=definitions_.size();d.tables=tables_.size();d.active_encounters=0;for(const auto&[id,e]:instances_){(void)id;if(CountsAgainstActiveBudget(e.state))++d.active_encounters;}d.spawned_entities=spawned_.size();d.respawn_schedules=respawns_.size();return d;
}

void EncountersService::Record(EncounterChange c)
{
    if(next_change_sequence_==0)return;c.sequence=next_change_sequence_;if(next_change_sequence_==std::numeric_limits<std::uint64_t>::max())next_change_sequence_=0;else++next_change_sequence_;
    changes_.push_back(std::move(c));const auto capacity=budgets_.change_journal_capacity;while(changes_.size()>capacity)changes_.pop_front();
}
} // namespace epidemic::gameplay::encounters
