#include "Epidemic/GameFramework/Progression/progression.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <iterator>
#include <limits>

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
    if (b > 0 && a > INT64_MAX - b)
        return INT64_MAX;
    if (b < 0 && a < INT64_MIN - b)
        return INT64_MIN;
    return a + b;
}
[[nodiscard]] std::int64_t MulSat(std::int64_t a, std::int64_t b) noexcept
{
    if (a == 0 || b == 0)
        return 0;
    if ((a == -1 && b == INT64_MIN) || (b == -1 && a == INT64_MIN))
        return INT64_MAX;
    if (a > 0)
    {
        if (b > 0 && a > INT64_MAX / b)
            return INT64_MAX;
        if (b < 0 && b < INT64_MIN / a)
            return INT64_MIN;
    }
    else
    {
        if (b > 0 && a < INT64_MIN / b)
            return INT64_MIN;
        if (b < 0 && a < INT64_MAX / b)
            return INT64_MAX;
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
} // namespace

ProgressionService::ProgressionService()
    : modifier_ids_(GameplayObjectId::FromString("framework.progression.modifiers").High())
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
    if (definition.id != expected || tracks_.contains(definition.id) ||
        !std::is_sorted(definition.rank_thresholds_micro.begin(), definition.rank_thresholds_micro.end()))
        return foundation::Result<ProgressionTrackId>::Failure(
            Error("gameplay.progression.track_invalid", "invalid or duplicate progression track"));
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

foundation::Result<void> ProgressionService::Freeze()
{
    std::unordered_map<AttributeTypeId, std::uint8_t, IdHash> marks;
    const auto visit = [&](auto &&self, AttributeTypeId id) -> bool {
        auto &mark = marks[id];
        if (mark == 1)
            return false;
        if (mark == 2)
            return true;
        mark = 1;
        const auto &def = attributes_.at(id);
        for (const auto &term : def.derived_terms)
        {
            if (!attributes_.contains(term.source) || !self(self, term.source))
                return false;
        }
        mark = 2;
        return true;
    };
    for (const auto &[id, def] : attributes_)
    {
        (void)def;
        if (!visit(visit, id))
            return foundation::Result<void>::Failure(Error("gameplay.progression.attribute_cycle",
                                                           "derived attribute dependency cycle or unknown dependency"));
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
bool ProgressionService::HasProfile(GameplayObjectRef subject) const noexcept
{
    return profiles_.contains(subject);
}
ProgressionService::Profile *ProgressionService::FindProfile(GameplayObjectRef subject) noexcept
{
    auto it = profiles_.find(subject);
    return it == profiles_.end() ? nullptr : &it->second;
}
const ProgressionService::Profile *ProgressionService::FindProfile(GameplayObjectRef subject) const noexcept
{
    auto it = profiles_.find(subject);
    return it == profiles_.end() ? nullptr : &it->second;
}
void ProgressionService::Bump(Profile &profile) noexcept
{
    ++revision_.value;
    ++profile.revision.value;
}
void ProgressionService::Record(ProgressionChange change)
{
    change.sequence = next_change_sequence_++;
    changes_.push_back(std::move(change));
}

foundation::Result<void> ProgressionService::SetBaseAttribute(GameplayObjectRef subject, AttributeTypeId attribute,
                                                              std::int64_t value, GameplayContext context)
{
    auto *profile = FindProfile(subject);
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
    const Profile &profile, AttributeTypeId attribute, std::unordered_set<AttributeTypeId, IdHash> &visiting) const
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
        for (const auto &term : def->second.derived_terms)
        {
            auto source = EvaluateAttribute(profile, term.source, visiting);
            if (!source)
                return source;
            value = AddSat(value, MulMicro(source.Value(), term.coefficient_micro));
        }
    }
    visiting.erase(attribute);
    std::vector<const ProgressionModifier *> mods;
    for (const auto &m : profile.modifiers)
        if (m.target == attribute)
            mods.push_back(&m);
    std::sort(mods.begin(), mods.end(), [](const auto *a, const auto *b) {
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
    for (const auto *m : mods)
    {
        switch (m->operation)
        {
        case ModifierOperation::BaseAdd:
            value = AddSat(value, m->value_micro);
            break;
        case ModifierOperation::BaseMultiply:
            value = MulMicro(value, m->value_micro);
            break;
        case ModifierOperation::FinalAdd:
            value = AddSat(value, m->value_micro);
            break;
        case ModifierOperation::FinalMultiply:
            value = MulMicro(value, m->value_micro);
            break;
        case ModifierOperation::Override:
            value = m->value_micro;
            break;
        case ModifierOperation::ClampMin:
            value = std::max(value, m->value_micro);
            break;
        case ModifierOperation::ClampMax:
            value = std::min(value, m->value_micro);
            break;
        }
    }
    value = std::clamp(value, def->second.min_micro, def->second.max_micro);
    return foundation::Result<std::int64_t>::Success(value);
}
foundation::Result<std::int64_t> ProgressionService::GetAttribute(GameplayObjectRef subject,
                                                                  AttributeTypeId attribute) const
{
    ++attribute_reads_;
    const auto *profile = FindProfile(subject);
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
    auto *profile = FindProfile(subject);
    if (profile == nullptr || !attributes_.contains(modifier.target))
        return foundation::Result<ProgressionModifierId>::Failure(
            Error("gameplay.progression.modifier_invalid", "profile or target attribute missing"));
    if (!modifier.id.IsValid())
    {
        modifier.id = {modifier_ids_.Next()};
    }
    if (std::any_of(profile->modifiers.begin(), profile->modifiers.end(),
                    [&](const auto &m) { return m.id == modifier.id; }))
    {
        return foundation::Result<ProgressionModifierId>::Failure(
            Error("gameplay.already_registered", "modifier id already exists"));
    }
    const auto id = modifier.id;
    profile->modifiers.push_back(std::move(modifier));
    Bump(*profile);
    Record({0, ProgressionChangeKind::ModifierAdded, subject, {}, {}, {}, id, revision_, context});
    return foundation::Result<ProgressionModifierId>::Success(id);
}
foundation::Result<void> ProgressionService::RemoveModifier(GameplayObjectRef subject, ProgressionModifierId modifier,
                                                            GameplayContext context)
{
    auto *p = FindProfile(subject);
    if (!p)
        return foundation::Result<void>::Failure(Error("gameplay.progression.profile_missing", "profile missing"));
    auto it = std::find_if(p->modifiers.begin(), p->modifiers.end(), [&](const auto &m) { return m.id == modifier; });
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
    auto *p = FindProfile(subject);
    if (!p)
        return 0;
    std::vector<ProgressionModifierId> ids;
    for (const auto &m : p->modifiers)
        if (m.source == source)
            ids.push_back(m.id);
    for (auto id : ids)
    {
        auto r = RemoveModifier(subject, id, context);
        (void)r;
    }
    return ids.size();
}

foundation::Result<ProgressionTrackState> ProgressionService::GrantProgress(GameplayObjectRef subject,
                                                                            ProgressionTrackId track,
                                                                            std::int64_t amount,
                                                                            GameplayContext context)
{
    auto *p = FindProfile(subject);
    auto d = tracks_.find(track);
    if (!p || d == tracks_.end())
        return foundation::Result<ProgressionTrackState>::Failure(
            Error("gameplay.progression.track_missing", "profile or track missing"));
    auto &state = p->tracks[track];
    state.id = track;
    const auto old_rank = state.rank;
    state.progress_micro = AddSat(state.progress_micro, amount);
    state.rank =
        static_cast<std::uint32_t>(std::upper_bound(d->second.rank_thresholds_micro.begin(),
                                                    d->second.rank_thresholds_micro.end(), state.progress_micro) -
                                   d->second.rank_thresholds_micro.begin());
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
foundation::Result<ProgressionTrackState> ProgressionService::GetTrack(GameplayObjectRef subject,
                                                                       ProgressionTrackId track) const
{
    const auto *p = FindProfile(subject);
    if (!p)
        return foundation::Result<ProgressionTrackState>::Failure(
            Error("gameplay.progression.profile_missing", "profile missing"));
    auto it = p->tracks.find(track);
    if (it == p->tracks.end())
        return foundation::Result<ProgressionTrackState>::Success(ProgressionTrackState{track, 0, 0, {}});
    return foundation::Result<ProgressionTrackState>::Success(it->second);
}

foundation::Result<void> ProgressionService::GrantPerk(GameplayObjectRef subject, PerkDefinitionId perk,
                                                       GameplayContext context)
{
    auto *p = FindProfile(subject);
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
    auto *p = FindProfile(subject);
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
    const auto *p = FindProfile(subject);
    return p && p->perks.contains(perk);
}

foundation::Result<void> ProgressionService::GrantUnlock(GameplayObjectRef subject, UnlockRecord unlock,
                                                         GameplayContext context)
{
    auto *p = FindProfile(subject);
    if (!p || !unlock.type.IsValid() || !unlock.value.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.progression.unlock_invalid", "invalid unlock"));
    if (std::any_of(p->unlocks.begin(), p->unlocks.end(),
                    [&](const auto &u) { return u.type == unlock.type && u.value == unlock.value; }))
        return foundation::Result<void>::Success();
    p->unlocks.push_back(unlock);
    Bump(*p);
    Record({0, ProgressionChangeKind::UnlockGranted, subject, {}, {}, {}, {}, revision_, context});
    return foundation::Result<void>::Success();
}
foundation::Result<void> ProgressionService::RevokeUnlock(GameplayObjectRef subject, UnlockTypeId type, TypeId value,
                                                          GameplayContext context)
{
    auto *p = FindProfile(subject);
    if (!p)
        return foundation::Result<void>::Failure(Error("gameplay.progression.profile_missing", "profile missing"));
    auto before = p->unlocks.size();
    std::erase_if(p->unlocks, [&](const auto &u) { return u.type == type && u.value == value; });
    if (before != p->unlocks.size())
    {
        Bump(*p);
        Record({0, ProgressionChangeKind::UnlockRevoked, subject, {}, {}, {}, {}, revision_, context});
    }
    return foundation::Result<void>::Success();
}
bool ProgressionService::HasUnlock(GameplayObjectRef subject, UnlockTypeId type, TypeId value) const noexcept
{
    const auto *p = FindProfile(subject);
    return p && std::any_of(p->unlocks.begin(), p->unlocks.end(),
                            [&](const auto &u) { return u.type == type && u.value == value; });
}

foundation::Result<ProgressionProfileSnapshot> ProgressionService::GetProfileSnapshot(GameplayObjectRef subject) const
{
    const auto *p = FindProfile(subject);
    if (!p)
        return foundation::Result<ProgressionProfileSnapshot>::Failure(
            Error("gameplay.progression.profile_missing", "profile missing"));
    ProgressionProfileSnapshot out;
    out.subject = subject;
    out.revision = p->revision;
    for (const auto &[id, v] : p->base_attributes)
        out.base_attributes.emplace_back(id, v);
    for (const auto &[id, t] : p->tracks)
    {
        (void)id;
        out.tracks.push_back(t);
    }
    out.modifiers = p->modifiers;
    out.perks.assign(p->perks.begin(), p->perks.end());
    out.unlocks = p->unlocks;
    std::sort(out.base_attributes.begin(), out.base_attributes.end(),
              [](auto &a, auto &b) { return a.first < b.first; });
    std::sort(out.tracks.begin(), out.tracks.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(out.modifiers.begin(), out.modifiers.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(out.perks.begin(), out.perks.end());
    std::sort(out.unlocks.begin(), out.unlocks.end(), [](auto &a, auto &b) {
        if (a.type != b.type)
            return a.type < b.type;
        return a.value < b.value;
    });
    return foundation::Result<ProgressionProfileSnapshot>::Success(std::move(out));
}

std::vector<ProgressionChange> ProgressionService::ChangesSince(std::uint64_t sequence) const
{
    std::vector<ProgressionChange> out;
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(out),
                 [&](const auto &c) { return c.sequence > sequence; });
    return out;
}
ProgressionSnapshot ProgressionService::CaptureSnapshot() const
{
    ProgressionSnapshot s;
    s.modifier_ids = modifier_ids_.GetSnapshot();
    s.revision = revision_;
    for (const auto &[subject, p] : profiles_)
    {
        (void)p;
        auto one = GetProfileSnapshot(subject);
        if (one)
            s.profiles.push_back(std::move(one).Value());
    }
    std::sort(s.profiles.begin(), s.profiles.end(), [](auto &a, auto &b) { return a.subject < b.subject; });
    return s;
}
foundation::Result<void> ProgressionService::RestoreSnapshot(ProgressionSnapshot snapshot)
{
    profiles_.clear();
    for (auto &s : snapshot.profiles)
    {
        if (!s.subject.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.progression.restore_invalid", "invalid profile snapshot"));
        Profile p;
        p.subject = s.subject;
        p.revision = s.revision;
        for (auto &[id, v] : s.base_attributes)
        {
            if (!attributes_.contains(id))
                return foundation::Result<void>::Failure(
                    Error("gameplay.progression.restore_invalid", "unknown attribute"));
            p.base_attributes[id] = v;
        }
        for (auto &t : s.tracks)
        {
            if (!tracks_.contains(t.id))
                return foundation::Result<void>::Failure(
                    Error("gameplay.progression.restore_invalid", "unknown track"));
            p.tracks[t.id] = t;
        }
        p.modifiers = std::move(s.modifiers);
        p.perks.insert(s.perks.begin(), s.perks.end());
        p.unlocks = std::move(s.unlocks);
        profiles_.emplace(p.subject, std::move(p));
    }
    modifier_ids_.Restore(snapshot.modifier_ids);
    revision_ = snapshot.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    return foundation::Result<void>::Success();
}
ProgressionDiagnostics ProgressionService::GetDiagnostics() const noexcept
{
    std::uint64_t modifiers = 0;
    for (const auto &[s, p] : profiles_)
    {
        (void)s;
        modifiers += p.modifiers.size();
    }
    return {profiles_.size(), attribute_reads_, derived_evaluations_, modifiers, track_grants_, rank_changes_};
}
} // namespace epidemic::gameplay::progression
