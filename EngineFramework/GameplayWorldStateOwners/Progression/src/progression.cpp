#include "Epidemic/GameFramework/Progression/progression.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <utility>

namespace epidemic::gameplay::progression
{
namespace
{
[[nodiscard]] foundation::Error Error(std::string code, std::string message)
{
    return foundation::Error::Create(std::move(code), std::move(message));
}

[[nodiscard]] std::int64_t AddSat(std::int64_t a, std::int64_t b) noexcept
{
    if (b > 0 && a > std::numeric_limits<std::int64_t>::max() - b)
        return std::numeric_limits<std::int64_t>::max();
    if (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)
        return std::numeric_limits<std::int64_t>::min();
    return a + b;
}

[[nodiscard]] std::int64_t MulSat(std::int64_t a, std::int64_t b) noexcept
{
    if (a == 0 || b == 0)
        return 0;
    if ((a == -1 && b == std::numeric_limits<std::int64_t>::min()) ||
        (b == -1 && a == std::numeric_limits<std::int64_t>::min()))
        return std::numeric_limits<std::int64_t>::max();
    if (a > 0)
    {
        if (b > 0 && a > std::numeric_limits<std::int64_t>::max() / b)
            return std::numeric_limits<std::int64_t>::max();
        if (b < 0 && b < std::numeric_limits<std::int64_t>::min() / a)
            return std::numeric_limits<std::int64_t>::min();
    }
    else
    {
        if (b > 0 && a < std::numeric_limits<std::int64_t>::min() / b)
            return std::numeric_limits<std::int64_t>::min();
        if (b < 0 && a < std::numeric_limits<std::int64_t>::max() / b)
            return std::numeric_limits<std::int64_t>::max();
    }
    return a * b;
}

[[nodiscard]] std::int64_t MulMicro(std::int64_t a, std::int64_t b) noexcept
{
    constexpr std::int64_t scale = 1'000'000;
    const auto aq = a / scale;
    const auto ar = a % scale;
    const auto bq = b / scale;
    const auto br = b % scale;
    auto result = MulSat(MulSat(aq, bq), scale);
    result = AddSat(result, MulSat(aq, br));
    result = AddSat(result, MulSat(ar, bq));
    result = AddSat(result, MulSat(ar, br) / scale);
    return result;
}

[[nodiscard]] std::uint32_t RankFor(const ProgressionTrackDefinition& definition, std::int64_t progress) noexcept
{
    return static_cast<std::uint32_t>(
        std::upper_bound(definition.rank_thresholds_micro.begin(), definition.rank_thresholds_micro.end(), progress) -
        definition.rank_thresholds_micro.begin());
}

[[nodiscard]] bool StrictlyIncreasing(const std::vector<std::int64_t>& values) noexcept
{
    for (std::size_t i = 1; i < values.size(); ++i)
        if (values[i - 1] >= values[i])
            return false;
    return true;
}

[[nodiscard]] bool ValidGeneratorSnapshot(const MonotonicIdGenerator<GameplayObjectId>::Snapshot& snapshot,
                                          std::uint64_t expected_scope, std::uint64_t max_restored_low) noexcept
{
    return snapshot.scope != 0 && snapshot.scope == expected_scope &&
           (snapshot.next == 0 || snapshot.next > max_restored_low);
}

void AdvanceGeneratorPast(MonotonicIdGenerator<GameplayObjectId>& generator, GameplayObjectId id) noexcept
{
    if (!id.IsValid() || id.High() != generator.Scope().Raw())
        return;
    auto snapshot = generator.GetSnapshot();
    if (snapshot.next == 0 || id.Low() < snapshot.next)
        return;
    snapshot.next = id.Low() == std::numeric_limits<std::uint64_t>::max() ? 0 : id.Low() + 1;
    generator.Restore(snapshot);
}
} // namespace

ProgressionService::ProgressionService()
    : modifier_ids_(GameplayObjectId::FromString("framework.progression.modifiers").High()),
      grant_reservation_ids_(GameplayObjectId::FromString("framework.progression.grant_reservations").High())
{
}

foundation::Result<AttributeTypeId> ProgressionService::RegisterAttribute(AttributeDefinition definition)
{
    if (frozen_)
        return foundation::Result<AttributeTypeId>::Failure(
            Error("gameplay.registry_frozen", "progression registry is frozen"));
    if (definition.canonical_name.empty())
        return foundation::Result<AttributeTypeId>::Failure(
            Error("gameplay.progression.attribute_invalid", "attribute name is required"));
    const auto expected = AttributeTypeId::FromString(definition.canonical_name);
    if (!definition.id.IsValid())
        definition.id = expected;
    if (definition.id != expected || definition.min_micro > definition.max_micro || attributes_.contains(definition.id))
        return foundation::Result<AttributeTypeId>::Failure(
            Error("gameplay.progression.attribute_invalid", "invalid or duplicate attribute"));
    for (const auto& term : definition.derived_terms)
        if (!term.source.IsValid())
            return foundation::Result<AttributeTypeId>::Failure(
                Error("gameplay.progression.attribute_invalid", "derived term has an invalid source"));
    const auto id = definition.id;
    attributes_.emplace(id, std::move(definition));
    return foundation::Result<AttributeTypeId>::Success(id);
}

foundation::Result<ProgressionTrackId> ProgressionService::RegisterTrack(ProgressionTrackDefinition definition)
{
    if (frozen_)
        return foundation::Result<ProgressionTrackId>::Failure(
            Error("gameplay.registry_frozen", "progression registry is frozen"));
    if (definition.canonical_name.empty())
        return foundation::Result<ProgressionTrackId>::Failure(
            Error("gameplay.progression.track_invalid", "track name is required"));
    const auto expected = ProgressionTrackId::FromString(definition.canonical_name);
    if (!definition.id.IsValid())
        definition.id = expected;
    const auto& policy = definition.policy;
    if (definition.id != expected || tracks_.contains(definition.id) ||
        policy.min_progress_micro > policy.max_progress_micro || !StrictlyIncreasing(definition.rank_thresholds_micro))
        return foundation::Result<ProgressionTrackId>::Failure(
            Error("gameplay.progression.track_invalid", "invalid or duplicate progression track"));
    for (const auto threshold : definition.rank_thresholds_micro)
        if (threshold < policy.min_progress_micro || threshold > policy.max_progress_micro)
            return foundation::Result<ProgressionTrackId>::Failure(
                Error("gameplay.progression.track_invalid", "rank threshold is outside the track policy range"));
    const auto id = definition.id;
    tracks_.emplace(id, std::move(definition));
    return foundation::Result<ProgressionTrackId>::Success(id);
}

foundation::Result<PerkDefinitionId> ProgressionService::RegisterPerk(PerkDefinition definition)
{
    if (frozen_)
        return foundation::Result<PerkDefinitionId>::Failure(
            Error("gameplay.registry_frozen", "progression registry is frozen"));
    if (definition.canonical_name.empty())
        return foundation::Result<PerkDefinitionId>::Failure(
            Error("gameplay.progression.perk_invalid", "perk name is required"));
    const auto expected = PerkDefinitionId::FromString(definition.canonical_name);
    if (!definition.id.IsValid())
        definition.id = expected;
    if (definition.id != expected || perks_.contains(definition.id))
        return foundation::Result<PerkDefinitionId>::Failure(
            Error("gameplay.progression.perk_invalid", "invalid or duplicate perk"));
    const auto id = definition.id;
    perks_.emplace(id, std::move(definition));
    return foundation::Result<PerkDefinitionId>::Success(id);
}

foundation::Result<UnlockDefinitionId> ProgressionService::RegisterUnlockDefinition(UnlockDefinition definition)
{
    if (frozen_)
        return foundation::Result<UnlockDefinitionId>::Failure(
            Error("gameplay.registry_frozen", "progression registry is frozen"));
    if (definition.canonical_name.empty() || !definition.type.IsValid() || !definition.value.IsValid())
        return foundation::Result<UnlockDefinitionId>::Failure(
            Error("gameplay.progression.unlock_definition_invalid", "unlock definition is invalid"));
    const auto expected = UnlockDefinitionId::FromString(definition.canonical_name);
    if (!definition.id.IsValid())
        definition.id = expected;
    if (definition.id != expected || unlock_definitions_.contains(definition.id))
        return foundation::Result<UnlockDefinitionId>::Failure(
            Error("gameplay.progression.unlock_definition_invalid", "invalid or duplicate unlock definition"));
    const auto id = definition.id;
    unlock_definitions_.emplace(id, std::move(definition));
    return foundation::Result<UnlockDefinitionId>::Success(id);
}

foundation::Result<MilestoneId> ProgressionService::RegisterMilestone(MilestoneDefinition definition)
{
    if (frozen_)
        return foundation::Result<MilestoneId>::Failure(
            Error("gameplay.registry_frozen", "progression registry is frozen"));
    if (definition.canonical_name.empty() || !definition.track.IsValid())
        return foundation::Result<MilestoneId>::Failure(
            Error("gameplay.progression.milestone_invalid", "milestone definition is invalid"));
    const auto expected = MilestoneId::FromString(definition.canonical_name);
    if (!definition.id.IsValid())
        definition.id = expected;
    if (definition.id != expected || milestones_.contains(definition.id))
        return foundation::Result<MilestoneId>::Failure(
            Error("gameplay.progression.milestone_invalid", "invalid or duplicate milestone"));
    const auto id = definition.id;
    milestones_.emplace(id, std::move(definition));
    return foundation::Result<MilestoneId>::Success(id);
}

foundation::Result<void> ProgressionService::Freeze()
{
    if (frozen_)
        return foundation::Result<void>::Success();

    std::unordered_map<AttributeTypeId, std::uint8_t, IdHash> marks;
    const auto visit = [&](auto&& self, AttributeTypeId id) -> bool {
        auto& mark = marks[id];
        if (mark == 1)
            return false;
        if (mark == 2)
            return true;
        mark = 1;
        const auto& def = attributes_.at(id);
        for (const auto& term : def.derived_terms)
            if (!attributes_.contains(term.source) || !self(self, term.source))
                return false;
        mark = 2;
        return true;
    };
    for (const auto& [id, def] : attributes_)
    {
        (void)def;
        if (!visit(visit, id))
            return foundation::Result<void>::Failure(
                Error("gameplay.progression.attribute_cycle", "derived attribute dependency cycle or unknown dependency"));
    }

    const auto prerequisite_valid = [&](const ProgressionPrerequisite& p) {
        switch (p.kind)
        {
        case ProgressionPrerequisiteKind::TrackProgressAtLeast:
        case ProgressionPrerequisiteKind::TrackRankAtLeast:
            return tracks_.contains(p.track);
        case ProgressionPrerequisiteKind::HasPerk:
            return perks_.contains(p.perk);
        case ProgressionPrerequisiteKind::HasUnlock:
            return p.unlock_type.IsValid() && p.unlock_value.IsValid();
        }
        return false;
    };
    for (const auto& [id, milestone] : milestones_)
    {
        (void)id;
        const auto track = tracks_.find(milestone.track);
        if (track == tracks_.end() || milestone.threshold_micro < track->second.policy.min_progress_micro ||
            milestone.threshold_micro > track->second.policy.max_progress_micro)
            return foundation::Result<void>::Failure(
                Error("gameplay.progression.milestone_invalid", "milestone references an invalid track or threshold"));
        if (!std::all_of(milestone.prerequisites.begin(), milestone.prerequisites.end(), prerequisite_valid))
            return foundation::Result<void>::Failure(
                Error("gameplay.progression.prerequisite_invalid", "milestone contains an invalid prerequisite"));
        for (const auto unlock : milestone.unlocks)
            if (!unlock_definitions_.contains(unlock))
                return foundation::Result<void>::Failure(
                    Error("gameplay.progression.unlock_definition_missing", "milestone references an unknown unlock definition"));
    }
    frozen_ = true;
    return foundation::Result<void>::Success();
}

foundation::Result<void> ProgressionService::EnsureProfile(GameplayObjectRef subject, GameplayContext context)
{
    if (!subject.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.progression.subject_invalid", "profile subject is invalid"));
    if (profiles_.contains(subject))
        return foundation::Result<void>::Success();
    Profile profile;
    profile.subject = subject;
    Bump(profile);
    profiles_.emplace(subject, std::move(profile));
    Record({0, ProgressionChangeKind::ProfileCreated, subject, {}, {}, {}, {}, revision_, context});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ProgressionService::RemoveProfile(GameplayObjectRef subject, GameplayContext context)
{
    const auto it = profiles_.find(subject);
    if (it == profiles_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.progression.profile_missing", "profile missing"));
    if (HasPendingProgressGrant(subject))
        return foundation::Result<void>::Failure(
            Error("gameplay.progression.profile_reserved", "profile has an active progress reservation"));
    profiles_.erase(it);
    ++revision_.value;
    Record({0, ProgressionChangeKind::ProfileRemoved, subject, {}, {}, {}, {}, revision_, context});
    return foundation::Result<void>::Success();
}

bool ProgressionService::HasProfile(GameplayObjectRef subject) const noexcept
{
    return profiles_.contains(subject);
}

ProgressionService::Profile* ProgressionService::FindProfile(GameplayObjectRef subject) noexcept
{
    const auto it = profiles_.find(subject);
    return it == profiles_.end() ? nullptr : &it->second;
}

const ProgressionService::Profile* ProgressionService::FindProfile(GameplayObjectRef subject) const noexcept
{
    const auto it = profiles_.find(subject);
    return it == profiles_.end() ? nullptr : &it->second;
}

bool ProgressionService::HasPendingProgressGrant(GameplayObjectRef subject) const noexcept
{
    return std::any_of(pending_progress_grants_.begin(), pending_progress_grants_.end(),
                       [&](const auto& entry) { return entry.second.subject == subject; });
}

void ProgressionService::Bump(Profile& profile) noexcept
{
    ++revision_.value;
    ++profile.revision.value;
}

void ProgressionService::Record(ProgressionChange change)
{
    if (next_change_sequence_ == 0)
        return;
    change.sequence = next_change_sequence_;
    if (next_change_sequence_ == std::numeric_limits<std::uint64_t>::max())
        next_change_sequence_ = 0;
    else
        ++next_change_sequence_;
    changes_.push_back(std::move(change));
    while (changes_.size() > kChangeJournalCapacity)
        changes_.pop_front();
}

foundation::Result<void> ProgressionService::SetBaseAttribute(GameplayObjectRef subject, AttributeTypeId attribute,
                                                              std::int64_t value, GameplayContext context)
{
    auto* profile = FindProfile(subject);
    const auto def = attributes_.find(attribute);
    if (profile == nullptr || def == attributes_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.progression.attribute_missing", "profile or attribute is missing"));
    value = std::clamp(value, def->second.min_micro, def->second.max_micro);
    profile->base_attributes[attribute] = value;
    Bump(*profile);
    Record({0, ProgressionChangeKind::AttributeChanged, subject, attribute, {}, {}, {}, revision_, context});
    return foundation::Result<void>::Success();
}

foundation::Result<std::int64_t> ProgressionService::EvaluateAttribute(
    const Profile& profile, AttributeTypeId attribute, std::unordered_set<AttributeTypeId, IdHash>& visiting) const
{
    const auto def = attributes_.find(attribute);
    if (def == attributes_.end())
        return foundation::Result<std::int64_t>::Failure(
            Error("gameplay.progression.attribute_missing", "attribute definition is missing"));
    if (!visiting.insert(attribute).second)
        return foundation::Result<std::int64_t>::Failure(
            Error("gameplay.progression.attribute_cycle", "attribute cycle during evaluation"));

    std::int64_t value = def->second.default_micro;
    if (const auto base = profile.base_attributes.find(attribute); base != profile.base_attributes.end())
        value = base->second;
    if (!def->second.derived_terms.empty())
    {
        ++derived_evaluations_;
        value = def->second.derived_bias_micro;
        for (const auto& term : def->second.derived_terms)
        {
            auto source = EvaluateAttribute(profile, term.source, visiting);
            if (!source)
            {
                visiting.erase(attribute);
                return source;
            }
            value = AddSat(value, MulMicro(source.Value(), term.coefficient_micro));
        }
    }
    visiting.erase(attribute);

    std::vector<const ProgressionModifier*> mods;
    for (const auto& m : profile.modifiers)
        if (m.target == attribute)
            mods.push_back(&m);
    std::sort(mods.begin(), mods.end(), [](const auto* a, const auto* b) {
        if (a->operation != b->operation)
            return a->operation < b->operation;
        if (a->priority != b->priority)
            return a->priority < b->priority;
        if (a->modifier_type != b->modifier_type)
            return a->modifier_type < b->modifier_type;
        if (a->source != b->source)
            return a->source < b->source;
        return a->id < b->id;
    });
    for (const auto* m : mods)
    {
        switch (m->operation)
        {
        case ModifierOperation::BaseAdd: value = AddSat(value, m->value_micro); break;
        case ModifierOperation::BaseMultiply: value = MulMicro(value, m->value_micro); break;
        case ModifierOperation::FinalAdd: value = AddSat(value, m->value_micro); break;
        case ModifierOperation::FinalMultiply: value = MulMicro(value, m->value_micro); break;
        case ModifierOperation::Override: value = m->value_micro; break;
        case ModifierOperation::ClampMin: value = std::max(value, m->value_micro); break;
        case ModifierOperation::ClampMax: value = std::min(value, m->value_micro); break;
        }
    }
    return foundation::Result<std::int64_t>::Success(
        std::clamp(value, def->second.min_micro, def->second.max_micro));
}

foundation::Result<std::int64_t> ProgressionService::GetAttribute(GameplayObjectRef subject,
                                                                  AttributeTypeId attribute) const
{
    ++attribute_reads_;
    const auto* profile = FindProfile(subject);
    if (profile == nullptr)
        return foundation::Result<std::int64_t>::Failure(
            Error("gameplay.progression.profile_missing", "progression profile is missing"));
    std::unordered_set<AttributeTypeId, IdHash> visiting;
    return EvaluateAttribute(*profile, attribute, visiting);
}

foundation::Result<ProgressionModifierId> ProgressionService::AddModifier(GameplayObjectRef subject,
                                                                          ProgressionModifier modifier,
                                                                          GameplayContext context)
{
    auto* profile = FindProfile(subject);
    if (profile == nullptr || !attributes_.contains(modifier.target))
        return foundation::Result<ProgressionModifierId>::Failure(
            Error("gameplay.progression.modifier_invalid", "profile or target attribute missing"));
    if (!modifier.id.IsValid())
    {
        modifier.id = {modifier_ids_.Next()};
        if (!modifier.id.IsValid())
            return foundation::Result<ProgressionModifierId>::Failure(
                Error("gameplay.progression.id_exhausted", "progression modifier id exhausted"));
    }
    for (const auto& [other_subject, other_profile] : profiles_)
    {
        (void)other_subject;
        if (std::any_of(other_profile.modifiers.begin(), other_profile.modifiers.end(),
                        [&](const auto& existing) { return existing.id == modifier.id; }))
            return foundation::Result<ProgressionModifierId>::Failure(
                Error("gameplay.already_registered", "modifier id already exists"));
    }
    const auto id = modifier.id;
    AdvanceGeneratorPast(modifier_ids_, id.value);
    profile->modifiers.push_back(std::move(modifier));
    Bump(*profile);
    Record({0, ProgressionChangeKind::ModifierAdded, subject, {}, {}, {}, id, revision_, context});
    return foundation::Result<ProgressionModifierId>::Success(id);
}

foundation::Result<void> ProgressionService::RemoveModifier(GameplayObjectRef subject, ProgressionModifierId modifier,
                                                            GameplayContext context)
{
    auto* p = FindProfile(subject);
    if (!p)
        return foundation::Result<void>::Failure(Error("gameplay.progression.profile_missing", "profile missing"));
    const auto it = std::find_if(p->modifiers.begin(), p->modifiers.end(), [&](const auto& m) { return m.id == modifier; });
    if (it == p->modifiers.end())
        return foundation::Result<void>::Failure(Error("gameplay.progression.modifier_missing", "modifier missing"));
    p->modifiers.erase(it);
    Bump(*p);
    Record({0, ProgressionChangeKind::ModifierRemoved, subject, {}, {}, {}, modifier, revision_, context});
    return foundation::Result<void>::Success();
}

std::uint64_t ProgressionService::RemoveModifiersBySource(GameplayObjectRef subject, GameplayObjectRef source,
                                                          GameplayContext context)
{
    auto* p = FindProfile(subject);
    if (!p)
        return 0;
    std::vector<ProgressionModifierId> ids;
    for (const auto& m : p->modifiers)
        if (m.source == source)
            ids.push_back(m.id);
    for (const auto id : ids)
        (void)RemoveModifier(subject, id, context);
    return ids.size();
}

foundation::Result<std::vector<ProgressionModifierId>> ProgressionService::ReplaceModifiersBySource(
    GameplayObjectRef subject, GameplayObjectRef source, std::vector<ProgressionModifier> modifiers,
    GameplayContext context)
{
    auto* profile = FindProfile(subject);
    if (!profile || !source.IsValid())
        return foundation::Result<std::vector<ProgressionModifierId>>::Failure(
            Error("gameplay.progression.modifier_invalid", "profile or modifier source missing"));

    std::unordered_set<ProgressionModifierId, IdHash> replaceable_ids;
    std::vector<ProgressionModifierId> removed_ids;
    for (const auto& current : profile->modifiers)
        if (current.source == source)
        {
            removed_ids.push_back(current.id);
            replaceable_ids.insert(current.id);
        }

    auto staged_generator = modifier_ids_;
    std::unordered_set<ProgressionModifierId, IdHash> allocated;
    std::vector<ProgressionModifierId> ids;
    ids.reserve(modifiers.size());
    for (auto& modifier : modifiers)
    {
        if (!attributes_.contains(modifier.target) || (modifier.source.IsValid() && modifier.source != source))
            return foundation::Result<std::vector<ProgressionModifierId>>::Failure(
                Error("gameplay.progression.modifier_invalid", "replacement modifier target or source is invalid"));
        modifier.source = source;
        if (!modifier.id.IsValid())
        {
            modifier.id = {staged_generator.Next()};
            if (!modifier.id.IsValid())
                return foundation::Result<std::vector<ProgressionModifierId>>::Failure(
                    Error("gameplay.progression.id_exhausted", "progression modifier id exhausted"));
        }
        if (!allocated.insert(modifier.id).second)
            return foundation::Result<std::vector<ProgressionModifierId>>::Failure(
                Error("gameplay.already_registered", "duplicate replacement modifier id"));
        for (const auto& [other_subject, other_profile] : profiles_)
        {
            const auto exists = std::any_of(other_profile.modifiers.begin(), other_profile.modifiers.end(),
                                            [&](const auto& existing) { return existing.id == modifier.id; });
            const bool replacing_same_record = other_subject == subject && replaceable_ids.contains(modifier.id);
            if (exists && !replacing_same_record)
                return foundation::Result<std::vector<ProgressionModifierId>>::Failure(
                    Error("gameplay.already_registered", "replacement modifier id already exists"));
        }
        AdvanceGeneratorPast(staged_generator, modifier.id.value);
        ids.push_back(modifier.id);
    }

    modifier_ids_.Restore(staged_generator.GetSnapshot());
    profile->modifiers.erase(
        std::remove_if(profile->modifiers.begin(), profile->modifiers.end(),
                       [&](const auto& current) { return current.source == source; }),
        profile->modifiers.end());
    for (auto& modifier : modifiers)
        profile->modifiers.push_back(std::move(modifier));
    Bump(*profile);
    for (const auto id : removed_ids)
        Record({0, ProgressionChangeKind::ModifierRemoved, subject, {}, {}, {}, id, revision_, context});
    for (const auto id : ids)
        Record({0, ProgressionChangeKind::ModifierAdded, subject, {}, {}, {}, id, revision_, context});
    return foundation::Result<std::vector<ProgressionModifierId>>::Success(std::move(ids));
}

foundation::Result<ProgressionTrackState> ProgressionService::GrantProgress(GameplayObjectRef subject,
                                                                            ProgressionTrackId track,
                                                                            std::int64_t amount,
                                                                            GameplayContext context)
{
    auto* p = FindProfile(subject);
    if (HasPendingProgressGrant(subject))
        return foundation::Result<ProgressionTrackState>::Failure(
            Error("gameplay.progression.profile_reserved", "profile has an active progress reservation"));
    const auto d = tracks_.find(track);
    if (!p || d == tracks_.end())
        return foundation::Result<ProgressionTrackState>::Failure(
            Error("gameplay.progression.track_missing", "profile or track missing"));
    if (amount < 0 && !d->second.policy.allow_decrease)
        return foundation::Result<ProgressionTrackState>::Failure(
            Error("gameplay.progression.decrease_forbidden", "track policy forbids decreasing progress"));

    auto& state = p->tracks[track];
    state.id = track;
    const auto old_progress = state.progress_micro;
    const auto old_rank = state.rank;
    state.progress_micro = std::clamp(AddSat(state.progress_micro, amount), d->second.policy.min_progress_micro,
                                      d->second.policy.max_progress_micro);
    state.rank = RankFor(d->second, state.progress_micro);
    if (state.progress_micro == old_progress && state.rank == old_rank)
        return foundation::Result<ProgressionTrackState>::Success(state);
    ++state.revision.value;
    ++track_grants_;
    Bump(*p);
    Record({0, ProgressionChangeKind::TrackProgressChanged, subject, {}, track, {}, {}, revision_, context});
    if (state.rank != old_rank)
    {
        ++rank_changes_;
        Record({0, ProgressionChangeKind::TrackRankChanged, subject, {}, track, {}, {}, revision_, context});
    }
    return foundation::Result<ProgressionTrackState>::Success(state);
}

foundation::Result<ProgressionTrackState> ProgressionService::SetProgress(GameplayObjectRef subject,
                                                                          ProgressionTrackId track,
                                                                          std::int64_t progress,
                                                                          GameplayContext context)
{
    auto* p = FindProfile(subject);
    if (HasPendingProgressGrant(subject))
        return foundation::Result<ProgressionTrackState>::Failure(
            Error("gameplay.progression.profile_reserved", "profile has an active progress reservation"));
    const auto d = tracks_.find(track);
    if (!p || d == tracks_.end())
        return foundation::Result<ProgressionTrackState>::Failure(
            Error("gameplay.progression.track_missing", "profile or track missing"));
    if (!d->second.policy.allow_direct_set)
        return foundation::Result<ProgressionTrackState>::Failure(
            Error("gameplay.progression.direct_set_forbidden", "track policy forbids direct progress assignment"));

    auto& state = p->tracks[track];
    state.id = track;
    const auto target = std::clamp(progress, d->second.policy.min_progress_micro, d->second.policy.max_progress_micro);
    if (target < state.progress_micro && !d->second.policy.allow_decrease)
        return foundation::Result<ProgressionTrackState>::Failure(
            Error("gameplay.progression.decrease_forbidden", "track policy forbids decreasing progress"));
    const auto old_rank = state.rank;
    if (target == state.progress_micro)
        return foundation::Result<ProgressionTrackState>::Success(state);
    state.progress_micro = target;
    state.rank = RankFor(d->second, target);
    ++state.revision.value;
    Bump(*p);
    Record({0, ProgressionChangeKind::TrackProgressChanged, subject, {}, track, {}, {}, revision_, context});
    if (state.rank != old_rank)
    {
        ++rank_changes_;
        Record({0, ProgressionChangeKind::TrackRankChanged, subject, {}, track, {}, {}, revision_, context});
    }
    return foundation::Result<ProgressionTrackState>::Success(state);
}

foundation::Result<ProgressionTrackState> ProgressionService::GetTrack(GameplayObjectRef subject,
                                                                       ProgressionTrackId track) const
{
    const auto* p = FindProfile(subject);
    const auto d = tracks_.find(track);
    if (!p || d == tracks_.end())
        return foundation::Result<ProgressionTrackState>::Failure(
            Error("gameplay.progression.track_missing", "profile or track missing"));
    const auto it = p->tracks.find(track);
    if (it == p->tracks.end())
        return foundation::Result<ProgressionTrackState>::Success(
            ProgressionTrackState{track, d->second.policy.min_progress_micro,
                                  RankFor(d->second, d->second.policy.min_progress_micro), {}});
    return foundation::Result<ProgressionTrackState>::Success(it->second);
}

foundation::Result<ProgressionGrantReservationId> ProgressionService::ReserveProgressGrant(
    GameplayObjectRef subject, ProgressionTrackId track, std::int64_t amount, GameplayContext context)
{
    auto* profile = FindProfile(subject);
    const auto definition = tracks_.find(track);
    if (!profile || definition == tracks_.end())
        return foundation::Result<ProgressionGrantReservationId>::Failure(
            Error("gameplay.progression.track_missing", "profile or track missing"));
    if (HasPendingProgressGrant(subject))
        return foundation::Result<ProgressionGrantReservationId>::Failure(
            Error("gameplay.progression.profile_reserved", "profile already has an active progress reservation"));
    if (amount < 0 && !definition->second.policy.allow_decrease)
        return foundation::Result<ProgressionGrantReservationId>::Failure(
            Error("gameplay.progression.decrease_forbidden", "track policy forbids decreasing progress"));

    PendingProgressGrant pending;
    pending.id = ProgressionGrantReservationId{grant_reservation_ids_.Next()};
    if (!pending.id.IsValid())
        return foundation::Result<ProgressionGrantReservationId>::Failure(
            Error("gameplay.progression.id_exhausted", "progress grant reservation id exhausted"));
    pending.subject = subject;
    pending.track = track;
    pending.context = context;

    const auto existing = profile->tracks.find(track);
    pending.track_existed = existing != profile->tracks.end();
    if (pending.track_existed)
        pending.before = existing->second;
    else
        pending.before = ProgressionTrackState{track, definition->second.policy.min_progress_micro,
                                               RankFor(definition->second, definition->second.policy.min_progress_micro), {}};

    pending.after = pending.before;
    pending.after.progress_micro = std::clamp(AddSat(pending.before.progress_micro, amount),
                                              definition->second.policy.min_progress_micro,
                                              definition->second.policy.max_progress_micro);
    pending.after.rank = RankFor(definition->second, pending.after.progress_micro);
    pending_progress_grants_.emplace(pending.id, pending);
    return foundation::Result<ProgressionGrantReservationId>::Success(pending.id);
}

void ProgressionService::CommitProgressGrant(ProgressionGrantReservationId reservation) noexcept
{
    const auto it = pending_progress_grants_.find(reservation);
    if (it == pending_progress_grants_.end())
        return;
    const auto pending = it->second;
    auto* profile = FindProfile(pending.subject);
    if (!profile)
    {
        pending_progress_grants_.erase(it);
        return;
    }

    const auto current = profile->tracks.find(pending.track);
    if (pending.track_existed)
    {
        if (current == profile->tracks.end() || current->second.id != pending.before.id ||
            current->second.progress_micro != pending.before.progress_micro || current->second.rank != pending.before.rank ||
            current->second.revision != pending.before.revision)
        {
            pending_progress_grants_.erase(it);
            return;
        }
    }
    else if (current != profile->tracks.end())
    {
        pending_progress_grants_.erase(it);
        return;
    }

    pending_progress_grants_.erase(it);
    const bool progress_changed = pending.after.progress_micro != pending.before.progress_micro;
    const bool rank_changed = pending.after.rank != pending.before.rank;
    if (!progress_changed && !rank_changed)
        return;

    auto committed = pending.after;
    committed.revision = pending.before.revision;
    ++committed.revision.value;
    profile->tracks[pending.track] = committed;
    ++track_grants_;
    Bump(*profile);
    Record({0, ProgressionChangeKind::TrackProgressChanged, pending.subject, {}, pending.track, {}, {}, revision_, pending.context});
    if (rank_changed)
    {
        ++rank_changes_;
        Record({0, ProgressionChangeKind::TrackRankChanged, pending.subject, {}, pending.track, {}, {}, revision_, pending.context});
    }
    (void)EvaluateMilestonesUnlocked(*profile, pending.subject, pending.context);
}

void ProgressionService::ReleaseProgressGrant(ProgressionGrantReservationId reservation) noexcept
{
    pending_progress_grants_.erase(reservation);
}

foundation::Result<void> ProgressionService::GrantPerk(GameplayObjectRef subject, PerkDefinitionId perk,
                                                       GameplayContext context)
{
    auto* p = FindProfile(subject);
    if (HasPendingProgressGrant(subject))
        return foundation::Result<void>::Failure(
            Error("gameplay.progression.profile_reserved", "profile has an active progress reservation"));
    if (!p || !perks_.contains(perk))
        return foundation::Result<void>::Failure(Error("gameplay.progression.perk_missing", "profile or perk missing"));
    if (!p->perks.insert(perk).second)
        return foundation::Result<void>::Success();
    Bump(*p);
    Record({0, ProgressionChangeKind::PerkGranted, subject, {}, {}, perk, {}, revision_, context});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ProgressionService::RevokePerk(GameplayObjectRef subject, PerkDefinitionId perk,
                                                        GameplayContext context)
{
    auto* p = FindProfile(subject);
    if (HasPendingProgressGrant(subject))
        return foundation::Result<void>::Failure(
            Error("gameplay.progression.profile_reserved", "profile has an active progress reservation"));
    if (!p)
        return foundation::Result<void>::Failure(Error("gameplay.progression.profile_missing", "profile missing"));
    if (p->perks.erase(perk) == 0)
        return foundation::Result<void>::Success();
    Bump(*p);
    Record({0, ProgressionChangeKind::PerkRevoked, subject, {}, {}, perk, {}, revision_, context});
    return foundation::Result<void>::Success();
}

bool ProgressionService::HasPerk(GameplayObjectRef subject, PerkDefinitionId perk) const noexcept
{
    const auto* p = FindProfile(subject);
    return p && p->perks.contains(perk);
}

foundation::Result<void> ProgressionService::GrantUnlock(GameplayObjectRef subject, UnlockRecord unlock,
                                                         GameplayContext context)
{
    auto* p = FindProfile(subject);
    if (HasPendingProgressGrant(subject))
        return foundation::Result<void>::Failure(
            Error("gameplay.progression.profile_reserved", "profile has an active progress reservation"));
    if (!p || !unlock.type.IsValid() || !unlock.value.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.progression.unlock_invalid", "invalid unlock"));
    if (std::find(p->unlocks.begin(), p->unlocks.end(), unlock) != p->unlocks.end())
        return foundation::Result<void>::Success();
    p->unlocks.push_back(unlock);
    Bump(*p);
    ProgressionChange change{0, ProgressionChangeKind::UnlockGranted, subject, {}, {}, {}, {}, revision_, context};
    change.unlock_type = unlock.type;
    change.unlock_value = unlock.value;
    Record(std::move(change));
    return foundation::Result<void>::Success();
}

foundation::Result<void> ProgressionService::RevokeUnlock(GameplayObjectRef subject, UnlockTypeId type, TypeId value,
                                                          GameplayContext context)
{
    auto* p = FindProfile(subject);
    if (HasPendingProgressGrant(subject))
        return foundation::Result<void>::Failure(
            Error("gameplay.progression.profile_reserved", "profile has an active progress reservation"));
    if (!p)
        return foundation::Result<void>::Failure(Error("gameplay.progression.profile_missing", "profile missing"));
    const auto before = p->unlocks.size();
    std::erase_if(p->unlocks, [&](const auto& u) { return u.type == type && u.value == value; });
    if (before != p->unlocks.size())
    {
        Bump(*p);
        ProgressionChange change{0, ProgressionChangeKind::UnlockRevoked, subject, {}, {}, {}, {}, revision_, context};
        change.unlock_type = type;
        change.unlock_value = value;
        Record(std::move(change));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> ProgressionService::RevokeUnlockFromSource(GameplayObjectRef subject, UnlockTypeId type,
                                                                    TypeId value, GameplayObjectRef source,
                                                                    GameplayContext context)
{
    auto* p = FindProfile(subject);
    if (HasPendingProgressGrant(subject))
        return foundation::Result<void>::Failure(
            Error("gameplay.progression.profile_reserved", "profile has an active progress reservation"));
    if (!p)
        return foundation::Result<void>::Failure(Error("gameplay.progression.profile_missing", "profile missing"));
    const auto before = p->unlocks.size();
    std::erase_if(p->unlocks, [&](const auto& u) { return u.type == type && u.value == value && u.source == source; });
    if (before != p->unlocks.size())
    {
        Bump(*p);
        ProgressionChange change{0, ProgressionChangeKind::UnlockRevoked, subject, {}, {}, {}, {}, revision_, context};
        change.unlock_type = type;
        change.unlock_value = value;
        Record(std::move(change));
    }
    return foundation::Result<void>::Success();
}

bool ProgressionService::HasUnlock(GameplayObjectRef subject, UnlockTypeId type, TypeId value) const noexcept
{
    const auto* p = FindProfile(subject);
    return p && std::any_of(p->unlocks.begin(), p->unlocks.end(),
                            [&](const auto& u) { return u.type == type && u.value == value; });
}

PrerequisiteEvaluation ProgressionService::QueryPrerequisites(
    GameplayObjectRef subject, const std::vector<ProgressionPrerequisite>& prerequisites) const
{
    PrerequisiteEvaluation result;
    const auto* profile = FindProfile(subject);
    for (std::size_t i = 0; i < prerequisites.size(); ++i)
    {
        const auto& p = prerequisites[i];
        bool ok = false;
        switch (p.kind)
        {
        case ProgressionPrerequisiteKind::TrackProgressAtLeast:
        {
            const auto state = GetTrack(subject, p.track);
            ok = state && state.Value().progress_micro >= p.progress_micro;
            break;
        }
        case ProgressionPrerequisiteKind::TrackRankAtLeast:
        {
            const auto state = GetTrack(subject, p.track);
            ok = state && state.Value().rank >= p.rank;
            break;
        }
        case ProgressionPrerequisiteKind::HasPerk:
            ok = profile && profile->perks.contains(p.perk);
            break;
        case ProgressionPrerequisiteKind::HasUnlock:
            ok = profile && std::any_of(profile->unlocks.begin(), profile->unlocks.end(), [&](const auto& u) {
                return u.type == p.unlock_type && u.value == p.unlock_value;
            });
            break;
        }
        if (!ok)
        {
            result.satisfied = false;
            result.unmet_indices.push_back(i);
        }
    }
    return result;
}

foundation::Result<std::vector<MilestoneId>> ProgressionService::EvaluateMilestones(GameplayObjectRef subject,
                                                                                    GameplayContext context)
{
    auto* profile = FindProfile(subject);
    if (!profile)
        return foundation::Result<std::vector<MilestoneId>>::Failure(
            Error("gameplay.progression.profile_missing", "profile missing"));
    if (HasPendingProgressGrant(subject))
        return foundation::Result<std::vector<MilestoneId>>::Failure(
            Error("gameplay.progression.profile_reserved", "profile has an active progress reservation"));
    return EvaluateMilestonesUnlocked(*profile, subject, context);
}

foundation::Result<std::vector<MilestoneId>> ProgressionService::EvaluateMilestonesUnlocked(
    Profile& profile, GameplayObjectRef subject, GameplayContext context)
{
    Profile staged = profile;
    std::vector<const MilestoneDefinition*> ordered;
    ordered.reserve(milestones_.size());
    for (const auto& [id, milestone] : milestones_)
    {
        (void)id;
        ordered.push_back(&milestone);
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) {
        if (a->track != b->track)
            return a->track < b->track;
        if (a->threshold_micro != b->threshold_micro)
            return a->threshold_micro < b->threshold_micro;
        return a->id < b->id;
    });

    const auto prerequisites_satisfied = [&](const Profile& candidate, const std::vector<ProgressionPrerequisite>& prerequisites) {
        for (const auto& prerequisite : prerequisites)
        {
            switch (prerequisite.kind)
            {
            case ProgressionPrerequisiteKind::TrackProgressAtLeast:
            {
                const auto definition = tracks_.find(prerequisite.track);
                if (definition == tracks_.end())
                    return false;
                const auto state = candidate.tracks.find(prerequisite.track);
                const auto progress = state == candidate.tracks.end() ? definition->second.policy.min_progress_micro
                                                                       : state->second.progress_micro;
                if (progress < prerequisite.progress_micro)
                    return false;
                break;
            }
            case ProgressionPrerequisiteKind::TrackRankAtLeast:
            {
                const auto definition = tracks_.find(prerequisite.track);
                if (definition == tracks_.end())
                    return false;
                const auto state = candidate.tracks.find(prerequisite.track);
                const auto rank = state == candidate.tracks.end()
                                      ? RankFor(definition->second, definition->second.policy.min_progress_micro)
                                      : state->second.rank;
                if (rank < prerequisite.rank)
                    return false;
                break;
            }
            case ProgressionPrerequisiteKind::HasPerk:
                if (!candidate.perks.contains(prerequisite.perk))
                    return false;
                break;
            case ProgressionPrerequisiteKind::HasUnlock:
                if (std::none_of(candidate.unlocks.begin(), candidate.unlocks.end(), [&](const auto& unlock) {
                        return unlock.type == prerequisite.unlock_type && unlock.value == prerequisite.unlock_value;
                    }))
                    return false;
                break;
            }
        }
        return true;
    };

    struct StagedMilestoneEvent
    {
        bool unlock = false;
        UnlockRecord unlock_record{};
        MilestoneId milestone{};
    };
    std::vector<MilestoneId> achieved;
    std::vector<StagedMilestoneEvent> staged_events;
    for (const auto* milestone : ordered)
    {
        if (staged.achieved_milestones.contains(milestone->id))
            continue;
        const auto definition = tracks_.find(milestone->track);
        if (definition == tracks_.end())
            return foundation::Result<std::vector<MilestoneId>>::Failure(
                Error("gameplay.progression.track_missing", "milestone track definition is missing"));
        const auto state = staged.tracks.find(milestone->track);
        const auto progress = state == staged.tracks.end() ? definition->second.policy.min_progress_micro
                                                            : state->second.progress_micro;
        if (progress < milestone->threshold_micro || !prerequisites_satisfied(staged, milestone->prerequisites))
            continue;

        const GameplayObjectRef source{Domain(), GameplayObjectId::FromRaw(
            milestone->id.value.Raw(), milestone->id.value.Raw() ^ 0x9E3779B97F4A7C15ull)};
        std::vector<UnlockRecord> milestone_unlocks;
        milestone_unlocks.reserve(milestone->unlocks.size());
        for (const auto unlock_id : milestone->unlocks)
        {
            const auto definition_it = unlock_definitions_.find(unlock_id);
            if (definition_it == unlock_definitions_.end())
                return foundation::Result<std::vector<MilestoneId>>::Failure(
                    Error("gameplay.progression.unlock_definition_missing", "milestone unlock definition is missing"));
            milestone_unlocks.push_back({definition_it->second.type, definition_it->second.value, source});
        }
        staged.achieved_milestones.insert(milestone->id);
        for (const auto& unlock : milestone_unlocks)
        {
            if (std::find(staged.unlocks.begin(), staged.unlocks.end(), unlock) == staged.unlocks.end())
            {
                staged.unlocks.push_back(unlock);
                staged_events.push_back(StagedMilestoneEvent{true, unlock, {}});
            }
        }
        achieved.push_back(milestone->id);
        staged_events.push_back(StagedMilestoneEvent{false, {}, milestone->id});
    }

    if (achieved.empty())
        return foundation::Result<std::vector<MilestoneId>>::Success({});

    profile.achieved_milestones = std::move(staged.achieved_milestones);
    profile.unlocks = std::move(staged.unlocks);
    for (const auto& event : staged_events)
    {
        if (event.unlock)
        {
            ProgressionChange unlock_change{0, ProgressionChangeKind::UnlockGranted, subject, {}, {}, {}, {}, revision_, context};
            unlock_change.unlock_type = event.unlock_record.type;
            unlock_change.unlock_value = event.unlock_record.value;
            Record(std::move(unlock_change));
            continue;
        }
        Bump(profile);
        ProgressionChange milestone_change{0, ProgressionChangeKind::MilestoneReached, subject, {}, {}, {}, {}, revision_, context};
        milestone_change.milestone = event.milestone;
        Record(std::move(milestone_change));
    }
    return foundation::Result<std::vector<MilestoneId>>::Success(std::move(achieved));
}

foundation::Result<ProgressionProfileSnapshot> ProgressionService::GetProfileSnapshot(GameplayObjectRef subject) const
{
    const auto* p = FindProfile(subject);
    if (!p)
        return foundation::Result<ProgressionProfileSnapshot>::Failure(
            Error("gameplay.progression.profile_missing", "profile missing"));
    ProgressionProfileSnapshot out;
    out.subject = subject;
    out.revision = p->revision;
    for (const auto& [id, value] : p->base_attributes)
        out.base_attributes.emplace_back(id, value);
    for (const auto& [id, track] : p->tracks)
    {
        (void)id;
        out.tracks.push_back(track);
    }
    for (const auto& modifier : p->modifiers)
        if (modifier.persistent)
            out.modifiers.push_back(modifier);
    out.perks.assign(p->perks.begin(), p->perks.end());
    out.unlocks = p->unlocks;
    out.achieved_milestones.assign(p->achieved_milestones.begin(), p->achieved_milestones.end());
    std::sort(out.base_attributes.begin(), out.base_attributes.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::sort(out.tracks.begin(), out.tracks.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(out.modifiers.begin(), out.modifiers.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(out.perks.begin(), out.perks.end());
    std::sort(out.unlocks.begin(), out.unlocks.end(), [](const auto& a, const auto& b) {
        if (a.type != b.type)
            return a.type < b.type;
        if (a.value != b.value)
            return a.value < b.value;
        return a.source < b.source;
    });
    std::sort(out.achieved_milestones.begin(), out.achieved_milestones.end());
    return foundation::Result<ProgressionProfileSnapshot>::Success(std::move(out));
}

std::vector<ProgressionChange> ProgressionService::ChangesSinceSequence(std::uint64_t sequence) const
{
    return ReadChangesSinceSequence(sequence).changes;
}

ProgressionChangeBatch ProgressionService::ReadChangesSinceSequence(std::uint64_t sequence) const
{
    ProgressionChangeBatch batch;
    const auto latest = next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max()
                                                   : next_change_sequence_ - 1;
    batch.oldest_available_sequence = changes_.empty() ? next_change_sequence_ : changes_.front().sequence;
    if (next_change_sequence_ == 0 || sequence > latest)
    {
        batch.snapshot_required = true;
        return batch;
    }
    if (changes_.empty())
    {
        batch.snapshot_required = sequence < latest;
        return batch;
    }
    if (sequence < batch.oldest_available_sequence && batch.oldest_available_sequence - sequence > 1)
    {
        batch.snapshot_required = true;
        return batch;
    }
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(batch.changes),
                 [&](const auto& c) { return c.sequence > sequence; });
    return batch;
}

ProgressionSnapshot ProgressionService::CaptureSnapshot() const
{
    ProgressionSnapshot snapshot;
    snapshot.modifier_ids = modifier_ids_.GetSnapshot();
    snapshot.revision = revision_;
    snapshot.journal.assign(changes_.begin(), changes_.end());
    snapshot.next_change_sequence = next_change_sequence_;
    for (const auto& [subject, p] : profiles_)
    {
        (void)p;
        auto one = GetProfileSnapshot(subject);
        if (one)
            snapshot.profiles.push_back(std::move(one).Value());
    }
    std::sort(snapshot.profiles.begin(), snapshot.profiles.end(), [](const auto& a, const auto& b) { return a.subject < b.subject; });
    snapshot.change_epoch = journal_epoch_;
    return snapshot;
}

foundation::Result<void> ProgressionService::RestoreSnapshot(ProgressionSnapshot snapshot)
{
    const auto next_journal_epoch = CheckedNextChangeEpoch(snapshot.change_epoch > journal_epoch_ ? snapshot.change_epoch : journal_epoch_);
    if (!next_journal_epoch)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.change_journal.epoch_exhausted", "change journal epoch is exhausted"));
    }
    std::unordered_map<GameplayObjectRef, Profile> restored_profiles;
    restored_profiles.reserve(snapshot.profiles.size());
    std::unordered_set<ProgressionModifierId, IdHash> restored_modifier_ids;
    std::uint64_t max_modifier_low = 0;
    const auto modifier_scope = modifier_ids_.Scope().Raw();
    for (auto& stored : snapshot.profiles)
    {
        if (!stored.subject.IsValid() || restored_profiles.contains(stored.subject))
            return foundation::Result<void>::Failure(
                Error("gameplay.progression.restore_invalid", "invalid or duplicate profile snapshot"));
        Profile profile;
        profile.subject = stored.subject;
        profile.revision = stored.revision;
        for (const auto& [id, value] : stored.base_attributes)
        {
            const auto def = attributes_.find(id);
            if (def == attributes_.end() || profile.base_attributes.contains(id) || value < def->second.min_micro || value > def->second.max_micro)
                return foundation::Result<void>::Failure(
                    Error("gameplay.progression.restore_invalid", "invalid base attribute snapshot"));
            profile.base_attributes.emplace(id, value);
        }
        for (const auto& state : stored.tracks)
        {
            const auto def = tracks_.find(state.id);
            if (def == tracks_.end() || profile.tracks.contains(state.id) ||
                state.progress_micro < def->second.policy.min_progress_micro || state.progress_micro > def->second.policy.max_progress_micro ||
                state.rank != RankFor(def->second, state.progress_micro))
                return foundation::Result<void>::Failure(
                    Error("gameplay.progression.restore_invalid", "invalid track snapshot"));
            profile.tracks.emplace(state.id, state);
        }
        for (const auto& modifier : stored.modifiers)
        {
            if (!modifier.id.IsValid() || !attributes_.contains(modifier.target) ||
                !restored_modifier_ids.insert(modifier.id).second)
                return foundation::Result<void>::Failure(
                    Error("gameplay.progression.restore_invalid", "invalid or duplicate modifier snapshot"));
            if (modifier.id.value.High() == modifier_scope)
                max_modifier_low = std::max(max_modifier_low, modifier.id.value.Low());
            profile.modifiers.push_back(modifier);
        }
        for (const auto perk : stored.perks)
            if (!perks_.contains(perk) || !profile.perks.insert(perk).second)
                return foundation::Result<void>::Failure(
                    Error("gameplay.progression.restore_invalid", "invalid perk snapshot"));
        for (const auto& unlock : stored.unlocks)
        {
            if (!unlock.type.IsValid() || !unlock.value.IsValid() ||
                std::find(profile.unlocks.begin(), profile.unlocks.end(), unlock) != profile.unlocks.end())
                return foundation::Result<void>::Failure(
                    Error("gameplay.progression.restore_invalid", "invalid unlock snapshot"));
            profile.unlocks.push_back(unlock);
        }
        for (const auto milestone : stored.achieved_milestones)
            if (!milestones_.contains(milestone) || !profile.achieved_milestones.insert(milestone).second)
                return foundation::Result<void>::Failure(
                    Error("gameplay.progression.restore_invalid", "invalid milestone snapshot"));
        restored_profiles.emplace(profile.subject, std::move(profile));
    }

    std::deque<ProgressionChange> restored_changes;
    if (snapshot.journal.size() > kChangeJournalCapacity)
        return foundation::Result<void>::Failure(
            Error("gameplay.progression.restore_invalid", "change journal exceeds capacity"));
    if (snapshot.next_change_sequence == 0 &&
        (snapshot.journal.empty() || snapshot.journal.back().sequence != std::numeric_limits<std::uint64_t>::max()))
        return foundation::Result<void>::Failure(
            Error("gameplay.progression.restore_invalid", "exhausted progression journal is missing terminal sequence"));

    std::uint64_t previous = 0;
    for (const auto& change : snapshot.journal)
    {
        const bool reaches_next = snapshot.next_change_sequence != 0 && change.sequence >= snapshot.next_change_sequence;
        if (change.sequence == 0 || (previous != 0 && change.sequence <= previous) || reaches_next)
            return foundation::Result<void>::Failure(
                Error("gameplay.progression.restore_invalid", "invalid change journal sequence"));
        restored_changes.push_back(change);
        previous = change.sequence;
    }
    if (!ValidGeneratorSnapshot(snapshot.modifier_ids, modifier_scope, max_modifier_low))
        return foundation::Result<void>::Failure(
            Error("gameplay.progression.restore_invalid", "invalid progression modifier id generator snapshot"));

    profiles_.swap(restored_profiles);
    changes_.swap(restored_changes);
    modifier_ids_.Restore(snapshot.modifier_ids);
    pending_progress_grants_.clear();
    revision_ = snapshot.revision;
    next_change_sequence_ = snapshot.next_change_sequence;
    journal_epoch_ = *next_journal_epoch;
    return foundation::Result<void>::Success();
}

ProgressionDiagnostics ProgressionService::GetDiagnostics() const noexcept
{
    std::uint64_t modifiers = 0;
    for (const auto& [subject, profile] : profiles_)
    {
        (void)subject;
        modifiers += profile.modifiers.size();
    }
    return {profiles_.size(), attribute_reads_, derived_evaluations_, modifiers, track_grants_, rank_changes_};
}
} // namespace epidemic::gameplay::progression
