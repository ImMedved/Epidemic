#include "Epidemic/GameFramework/Equipment/equipment.h"
#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_set>
#include <utility>
namespace epidemic::gameplay::equipment
{
namespace
{
foundation::Error Error(std::string_view c, std::string_view m)
{
    return foundation::Error::Create(c, m);
}

template <class Snapshot>
[[nodiscard]] bool ValidGeneratorSnapshot(Snapshot snapshot, std::uint64_t expected_scope, std::uint64_t max_low) noexcept
{
    if (snapshot.scope != expected_scope) return false;
    if (snapshot.next == 0) return true;
    return snapshot.next > max_low;
}


template <class WrappedId>
void AdvanceGeneratorPast(MonotonicIdGenerator<GameplayObjectId>& generator, WrappedId id) noexcept
{
    if (!id.IsValid() || id.value.High() != generator.Scope().Raw()) return;
    auto snapshot = generator.GetSnapshot();
    if (snapshot.next == 0 || id.value.Low() < snapshot.next) return;
    snapshot.next = id.value.Low() == std::numeric_limits<std::uint64_t>::max() ? 0 : id.value.Low() + 1;
    generator.Restore(snapshot);
}
} // namespace
EquipmentService::EquipmentService(std::size_t change_capacity) : change_capacity_(std::max<std::size_t>(1, change_capacity)) {}

foundation::Result<void> EquipmentService::EnsureMutationReady() const
{
    if (reconciliation_state_ == EquipmentReconciliationState::NeedsReconciliation)
        return foundation::Result<void>::Failure(
            Error("gameplay.equipment.reconciliation_required", "restored equipment bindings must be reconciled before mutation"));
    return foundation::Result<void>::Success();
}

foundation::Result<void> EquipmentService::ReconcileRestoredBindings(GameplayContext context)
{
    if (reconciliation_state_ == EquipmentReconciliationState::Ready)
        return foundation::Result<void>::Success();
    if (!item_provider_)
        return foundation::Result<void>::Failure(
            Error("gameplay.equipment.item_provider_missing", "item provider is required to reconcile restored equipment bindings"));

    std::vector<EquipmentBindingId> ids;
    ids.reserve(bindings_.size());
    for (const auto& [id, binding] : bindings_)
    {
        (void)binding;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    for (const auto id : ids)
    {
        const auto& binding = bindings_.at(id);
        auto result = item_provider_->ReconcileEquipmentReservation(binding.item, binding.subject, binding.id, context);
        if (!result) return result;
    }
    reconciliation_state_ = EquipmentReconciliationState::Ready;
    return foundation::Result<void>::Success();
}
foundation::Result<EquipmentProfileDefinitionId> EquipmentService::RegisterProfileDefinition(EquipmentProfileDefinition d)
{
    if (frozen_) return foundation::Result<EquipmentProfileDefinitionId>::Failure(Error("gameplay.registry_frozen", "equipment registry is frozen"));
    if (d.canonical_name.empty()) return foundation::Result<EquipmentProfileDefinitionId>::Failure(Error("gameplay.equipment.invalid_profile_definition", "profile definition requires canonical name"));
    const auto expected = EquipmentProfileDefinitionId::FromString(d.canonical_name);
    if (!d.id.IsValid()) d.id = expected;
    if (d.id != expected || profile_definitions_.contains(d.id))
        return foundation::Result<EquipmentProfileDefinitionId>::Failure(Error("gameplay.equipment.invalid_profile_definition", "invalid or duplicate profile definition"));
    std::sort(d.slots.begin(), d.slots.end(), [](const auto& a, const auto& b){ return a.id < b.id; });
    if (std::any_of(d.slots.begin(), d.slots.end(), [](const auto& slot){ return !slot.id.IsValid() || !slot.type.IsValid(); }))
        return foundation::Result<EquipmentProfileDefinitionId>::Failure(Error("gameplay.equipment.invalid_profile_definition", "profile definition contains invalid slot"));
    if (std::adjacent_find(d.slots.begin(), d.slots.end(), [](const auto& a, const auto& b){ return a.id == b.id; }) != d.slots.end())
        return foundation::Result<EquipmentProfileDefinitionId>::Failure(Error("gameplay.equipment.invalid_profile_definition", "profile definition contains duplicate slot"));
    const auto id = d.id;
    profile_definitions_.emplace(id, std::move(d));
    return foundation::Result<EquipmentProfileDefinitionId>::Success(id);
}
foundation::Result<void> EquipmentService::RegisterGrantSchema(EquipmentGrantSchema schema)
{
    if (frozen_) return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "equipment registry is frozen"));
    if (!schema.type.IsValid() || !schema.payload_type.IsValid() || schema.schema_version == 0 || schema.max_payload_bytes == 0 ||
        grant_schemas_.contains(schema.type))
        return foundation::Result<void>::Failure(Error("gameplay.equipment.invalid_grant_schema", "invalid or duplicate equipment grant schema"));
    grant_schemas_.emplace(schema.type, std::move(schema));
    return foundation::Result<void>::Success();
}
EquipmentProfile *EquipmentService::MutableProfile(EquipmentProfileId id) noexcept
{
    auto it = profiles_.find(id);
    return it == profiles_.end() ? nullptr : &it->second;
}
foundation::Result<EquipmentProfileId> EquipmentService::CreateProfile(EquipmentProfile p)
{
    if (auto ready = EnsureMutationReady(); !ready) return foundation::Result<EquipmentProfileId>::Failure(ready.GetError());
    if (!frozen_) return foundation::Result<EquipmentProfileId>::Failure(Error("gameplay.registry_not_frozen", "equipment registry must be frozen before runtime mutation"));
    if (!p.subject.IsValid() || profile_by_subject_.contains(p.subject))
        return foundation::Result<EquipmentProfileId>::Failure(
            Error("gameplay.equipment.invalid_profile", "invalid or duplicate equipment profile"));
    if (!p.id.IsValid()) p.id = EquipmentProfileId{profile_ids_.Next()};
    if (!p.id.IsValid() || profiles_.contains(p.id))
        return foundation::Result<EquipmentProfileId>::Failure(Error("gameplay.equipment.invalid_profile", "invalid or duplicate equipment profile id"));
    for (auto &s : p.slots)
    {
        if (!s.id.IsValid()) s.id = EquipmentSlotId{slot_ids_.Next()};
        if (!s.id.IsValid() || !s.type.IsValid())
            return foundation::Result<EquipmentProfileId>::Failure(Error("gameplay.equipment.invalid_slot", "profile contains invalid equipment slot"));
    }
    std::sort(p.slots.begin(), p.slots.end(), [](const auto& a, const auto& b){ return a.id < b.id; });
    if (std::adjacent_find(p.slots.begin(), p.slots.end(), [](const auto& a, const auto& b){ return a.id == b.id; }) != p.slots.end())
        return foundation::Result<EquipmentProfileId>::Failure(Error("gameplay.equipment.duplicate_slot", "profile contains duplicate equipment slot"));
    AdvanceGeneratorPast(profile_ids_, p.id);
    for (const auto& slot : p.slots) AdvanceGeneratorPast(slot_ids_, slot.id);
    Bump();
    p.revision = revision_;
    const auto id = p.id;
    profiles_.emplace(id, p);
    profile_by_subject_[p.subject] = id;
    ++diagnostics_.profiles;
    EquipmentChange created_profile;
    created_profile.kind = EquipmentChangeKind::ProfileCreated;
    created_profile.subject = p.subject;
    created_profile.revision = revision_;
    Record(std::move(created_profile));
    return foundation::Result<EquipmentProfileId>::Success(id);
}
foundation::Result<EquipmentProfileId> EquipmentService::CreateProfileFromDefinition(GameplayObjectRef subject, EquipmentProfileDefinitionId definition)
{
    const auto found = profile_definitions_.find(definition);
    if (found == profile_definitions_.end())
        return foundation::Result<EquipmentProfileId>::Failure(Error("gameplay.equipment.profile_definition_missing", "equipment profile definition is not registered"));
    EquipmentProfile profile;
    profile.subject = subject;
    profile.definition = definition;
    profile.slots = found->second.slots;
    return CreateProfile(std::move(profile));
}
foundation::Result<EquipmentSlotId> EquipmentService::AddSlot(EquipmentProfileId id, EquipmentSlotDefinition s)
{
    if (auto ready = EnsureMutationReady(); !ready) return foundation::Result<EquipmentSlotId>::Failure(ready.GetError());
    auto *p = MutableProfile(id);
    if (!p)
        return foundation::Result<EquipmentSlotId>::Failure(
            Error("gameplay.equipment.profile_missing", "equipment profile missing"));
    if (!s.id.IsValid()) s.id = EquipmentSlotId{slot_ids_.Next()};
    if (!s.id.IsValid() || !s.type.IsValid())
        return foundation::Result<EquipmentSlotId>::Failure(Error("gameplay.equipment.invalid_slot", "invalid equipment slot"));
    for (const auto &existing : p->slots)
        if (existing.id == s.id)
            return foundation::Result<EquipmentSlotId>::Failure(
                Error("gameplay.equipment.duplicate_slot", "duplicate equipment slot"));
    AdvanceGeneratorPast(slot_ids_, s.id);
    Bump();
    s.revision = revision_;
    p->slots.push_back(s);
    std::sort(p->slots.begin(), p->slots.end(), [](auto &a, auto &b) { return a.id < b.id; });
    p->revision = revision_;
    EquipmentChange change;
    change.kind = EquipmentChangeKind::SlotAdded;
    change.subject = p->subject;
    change.revision = revision_;
    change.slots = {s.id};
    Record(std::move(change));
    return foundation::Result<EquipmentSlotId>::Success(s.id);
}
const EquipmentProfile *EquipmentService::FindProfile(GameplayObjectRef subject) const noexcept
{
    const auto pit = profile_by_subject_.find(subject);
    if (pit == profile_by_subject_.end())
        return nullptr;
    const auto it = profiles_.find(pit->second);
    return it == profiles_.end() ? nullptr : &it->second;
}
const EquipmentBinding *EquipmentService::FindBinding(EquipmentBindingId id) const noexcept
{
    const auto it = bindings_.find(id);
    return it == bindings_.end() ? nullptr : &it->second;
}
std::optional<EquipmentProfile> EquipmentService::FindProfileCopy(GameplayObjectRef subject) const noexcept
{
    const auto* value = FindProfile(subject);
    return value ? std::optional<EquipmentProfile>{*value} : std::nullopt;
}
std::optional<EquipmentBinding> EquipmentService::FindBindingCopy(EquipmentBindingId id) const noexcept
{
    const auto* value = FindBinding(id);
    return value ? std::optional<EquipmentBinding>{*value} : std::nullopt;
}
bool EquipmentService::SlotAccepts(const EquipmentSlotDefinition &s, const EquipmentItemDescriptor &i) const noexcept
{
    for (const auto t : s.blocked_tags.Values())
        if (i.tags.HasExact(t))
            return false;
    if (s.accepted_tags.Values().empty())
        return true;
    for (const auto t : s.accepted_tags.Values())
        if (i.tags.HasExact(t))
            return true;
    return false;
}
std::vector<EquipmentBindingId> EquipmentService::ComputeConflicts(const EquipmentProfile& profile, GameplayObjectRef subject,
                                                                    const std::vector<EquipmentSlotId>& slots) const
{
    std::vector<TypeId> groups;
    for (const auto slot_id : slots)
    {
        const auto sit = std::find_if(profile.slots.begin(), profile.slots.end(), [&](const auto& slot){ return slot.id == slot_id; });
        if (sit != profile.slots.end() && sit->conflict_group.IsValid()) groups.push_back(sit->conflict_group);
    }
    std::sort(groups.begin(), groups.end());
    groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
    std::vector<EquipmentBindingId> result;
    for (const auto& [id, binding] : bindings_)
    {
        if (binding.subject != subject) continue;
        bool conflict = false;
        for (const auto bound_slot : binding.slots)
        {
            if (std::binary_search(slots.begin(), slots.end(), bound_slot)) { conflict = true; break; }
            const auto sit = std::find_if(profile.slots.begin(), profile.slots.end(), [&](const auto& slot){ return slot.id == bound_slot; });
            if (sit != profile.slots.end() && sit->conflict_group.IsValid() &&
                std::binary_search(groups.begin(), groups.end(), sit->conflict_group)) { conflict = true; break; }
        }
        if (conflict) result.push_back(id);
    }
    std::sort(result.begin(), result.end());
    return result;
}
bool EquipmentService::ValidateGrants(const std::vector<EquipmentGrantDescriptor>& grants) const
{
    for (const auto& grant : grants)
    {
        const auto schema = grant_schemas_.find(grant.type);
        if (schema == grant_schemas_.end() || grant.value_type != schema->second.payload_type ||
            grant.payload.size() > schema->second.max_payload_bytes) return false;
    }
    return true;
}
bool EquipmentService::CanEquip(GameplayObjectRef subject, EquipmentItemId item,
                                const std::vector<EquipmentSlotId> &slots) const
{
    const auto *p = FindProfile(subject);
    if (!p || !item_provider_ || slots.empty())
        return false;
    if (std::any_of(bindings_.begin(), bindings_.end(), [&](const auto& pair) {
            return pair.second.subject == subject && pair.second.item == item;
        }))
        return false;
    auto canonical = slots;
    std::sort(canonical.begin(), canonical.end());
    canonical.erase(std::unique(canonical.begin(), canonical.end()), canonical.end());
    const auto desc = item_provider_->Describe(item);
    if (!desc || !desc->available)
        return false;
    for (auto slot_id : canonical)
    {
        auto sit = std::find_if(p->slots.begin(), p->slots.end(), [&](const auto &s) { return s.id == slot_id; });
        if (sit == p->slots.end() || !SlotAccepts(*sit, *desc))
            return false;
    }
    return true;
}
foundation::Result<EquipPlan> EquipmentService::PrepareEquip(GameplayObjectRef subject, EquipmentItemId item,
                                                             std::vector<EquipmentSlotId> slots,
                                                             GameplayContext context)
{
    if (auto ready = EnsureMutationReady(); !ready) return foundation::Result<EquipPlan>::Failure(ready.GetError());
    std::sort(slots.begin(), slots.end());
    slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
    const auto *p = FindProfile(subject);
    if (!p || !CanEquip(subject, item, slots))
    {
        ++diagnostics_.rejected_operations;
        return foundation::Result<EquipPlan>::Failure(
            Error("gameplay.equipment.cannot_equip", "item cannot be equipped in requested slots"));
    }
    const auto desc = item_provider_->Describe(item);
    EquipPlan plan;
    plan.id = EquipOperationId{operation_ids_.Next()};
    if (!plan.id.IsValid())
        return foundation::Result<EquipPlan>::Failure(Error("gameplay.equipment.id_exhausted", "equip operation id generator is exhausted"));
    plan.subject = subject;
    plan.item = item;
    plan.slots = std::move(slots);
    plan.profile_revision = p->revision;
    plan.item_revision = desc->revision;
    plan.context = context;
    plan.conflicting_bindings = ComputeConflicts(*p, subject, plan.slots);
    return foundation::Result<EquipPlan>::Success(std::move(plan));
}
foundation::Result<EquipmentBindingId> EquipmentService::CommitEquip(const EquipPlan &p,
                                                                     std::vector<EquipmentGrantDescriptor> grants)
{
    if (auto ready = EnsureMutationReady(); !ready) return foundation::Result<EquipmentBindingId>::Failure(ready.GetError());
    if (!frozen_ || !item_provider_)
        return foundation::Result<EquipmentBindingId>::Failure(Error("gameplay.equipment.not_ready", "equipment service is not frozen or item provider is missing"));
    const auto *profile = FindProfile(p.subject);
    if (!profile || profile->revision != p.profile_revision)
        return foundation::Result<EquipmentBindingId>::Failure(Error("gameplay.equipment.stale_plan", "equip plan is stale"));
    const auto desc = item_provider_->Describe(p.item);
    if (!desc || !desc->available || desc->revision != p.item_revision || !CanEquip(p.subject, p.item, p.slots))
        return foundation::Result<EquipmentBindingId>::Failure(Error("gameplay.equipment.stale_item", "equipment item state changed"));
    const auto current_conflicts = ComputeConflicts(*profile, p.subject, p.slots);
    if (current_conflicts != p.conflicting_bindings)
        return foundation::Result<EquipmentBindingId>::Failure(Error("gameplay.equipment.stale_plan", "equipment occupancy changed since prepare"));
    if (std::any_of(bindings_.begin(), bindings_.end(), [&](const auto& pair) { return pair.second.item == p.item; }))
        return foundation::Result<EquipmentBindingId>::Failure(Error("gameplay.equipment.item_already_bound", "equipment item is already bound"));

    if (grants.empty()) grants = desc->grants;
    if (!ValidateGrants(grants))
        return foundation::Result<EquipmentBindingId>::Failure(Error("gameplay.equipment.invalid_grants", "equipment grants do not match registered schemas"));

    const EquipmentBindingId new_id{binding_ids_.Next()};
    if (!new_id.IsValid())
        return foundation::Result<EquipmentBindingId>::Failure(Error("gameplay.equipment.id_exhausted", "equipment binding id generator is exhausted"));

    std::vector<EquipmentItemId> releases;
    releases.reserve(current_conflicts.size());
    std::vector<EquipmentBinding> removed_bindings;
    removed_bindings.reserve(current_conflicts.size());
    for (const auto conflict : current_conflicts)
    {
        const auto it = bindings_.find(conflict);
        if (it == bindings_.end())
            return foundation::Result<EquipmentBindingId>::Failure(Error("gameplay.equipment.stale_plan", "conflicting binding disappeared"));
        releases.push_back(it->second.item);
        removed_bindings.push_back(it->second);
    }

    auto exchange = item_provider_->ExchangeEquipmentReservations(releases, p.item, p.subject, p.context);
    if (!exchange) return foundation::Result<EquipmentBindingId>::Failure(exchange.GetError());

    EquipmentBinding binding;
    binding.id = new_id;
    binding.subject = p.subject;
    binding.item = p.item;
    binding.slots = p.slots;
    binding.grants = std::move(grants);
    binding.item_revision = desc->revision;
    Bump();
    binding.revision = revision_;

    for (const auto& removed : removed_bindings)
    {
        bindings_.erase(removed.id);
        EquipmentChange change;
        change.kind = EquipmentChangeKind::BindingRemoved;
        change.subject = removed.subject;
        change.binding = removed.id;
        change.item = removed.item;
        change.context = p.context;
        change.revision = revision_;
        change.slots = removed.slots;
        change.grants = removed.grants;
        change.old_state = removed.state;
        change.new_state = removed.state;
        Record(std::move(change));
    }
    bindings_.emplace(new_id, binding);
    diagnostics_.bindings = bindings_.size();
    ++diagnostics_.equip_operations;
    EquipmentChange created;
    created.kind = EquipmentChangeKind::BindingCreated;
    created.subject = p.subject;
    created.binding = new_id;
    created.item = p.item;
    created.context = p.context;
    created.revision = revision_;
    created.slots = binding.slots;
    created.grants = binding.grants;
    created.new_state = binding.state;
    Record(std::move(created));
    return foundation::Result<EquipmentBindingId>::Success(new_id);
}

foundation::Result<void> EquipmentService::Unequip(EquipmentBindingId id, GameplayContext context)
{
    if (auto ready = EnsureMutationReady(); !ready) return ready;
    auto it = bindings_.find(id);
    if (it == bindings_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.equipment.binding_missing", "equipment binding missing"));
    const auto copy = it->second;
    if (item_provider_)
    {
        auto r = item_provider_->ReleaseFromEquipment(copy.item, copy.subject, context);
        if (!r)
            return r;
    }
    bindings_.erase(it);
    Bump();
    if (diagnostics_.bindings > 0)
        --diagnostics_.bindings;
    EquipmentChange change;
    change.kind = EquipmentChangeKind::BindingRemoved;
    change.subject = copy.subject;
    change.binding = id;
    change.item = copy.item;
    change.context = context;
    change.revision = revision_;
    change.slots = copy.slots;
    change.grants = copy.grants;
    change.old_state = copy.state;
    change.new_state = copy.state;
    Record(std::move(change));
    return foundation::Result<void>::Success();
}
foundation::Result<void> EquipmentService::SetBindingState(EquipmentBindingId id, EquipmentBindingState state,
                                                           GameplayContext context)
{
    if (auto ready = EnsureMutationReady(); !ready) return ready;
    auto it = bindings_.find(id);
    if (it == bindings_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.equipment.binding_missing", "equipment binding missing"));
    if (it->second.state == state)
        return foundation::Result<void>::Success();
    const auto old_state = it->second.state;
    Bump();
    it->second.state = state;
    it->second.revision = revision_;
    EquipmentChange change;
    change.kind = EquipmentChangeKind::BindingStateChanged;
    change.subject = it->second.subject;
    change.binding = id;
    change.item = it->second.item;
    change.context = context;
    change.revision = revision_;
    change.slots = it->second.slots;
    change.grants = it->second.grants;
    change.old_state = old_state;
    change.new_state = state;
    Record(std::move(change));
    return foundation::Result<void>::Success();
}
std::vector<EquipmentBinding> EquipmentService::FindBindings(GameplayObjectRef s) const
{
    std::vector<EquipmentBinding> out;
    for (const auto &[id, b] : bindings_)
    {
        (void)id;
        if (b.subject == s)
            out.push_back(b);
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return out;
}
std::optional<EquipmentItemId> EquipmentService::GetEquippedItem(GameplayObjectRef s, EquipmentSlotId slot) const
{
    for (const auto &b : FindBindings(s))
        if (std::find(b.slots.begin(), b.slots.end(), slot) != b.slots.end())
            return b.item;
    return std::nullopt;
}
foundation::Result<void> EquipmentService::ValidateAndCanonicalizeLoadout(EquipmentLoadout& loadout) const
{
    if (!loadout.subject.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.equipment.invalid_loadout", "loadout subject is invalid"));
    const auto* profile = FindProfile(loadout.subject);
    if (!profile)
        return foundation::Result<void>::Failure(Error("gameplay.equipment.invalid_loadout", "loadout subject has no equipment profile"));
    if (!item_provider_ && !loadout.desired.empty())
        return foundation::Result<void>::Failure(Error("gameplay.equipment.item_provider_missing", "item provider is required to validate loadout items"));

    std::sort(loadout.desired.begin(), loadout.desired.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second < b.second;
    });

    std::unordered_set<EquipmentItemId, IdHash> items;
    std::unordered_set<TypeId> conflict_groups;
    EquipmentSlotId previous_slot{};
    for (const auto& [slot_id, item_id] : loadout.desired)
    {
        if (!slot_id.IsValid() || !item_id.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.equipment.invalid_loadout", "loadout contains invalid slot or item id"));
        if (previous_slot.IsValid() && previous_slot == slot_id)
            return foundation::Result<void>::Failure(Error("gameplay.equipment.invalid_loadout", "loadout contains duplicate desired slot"));
        previous_slot = slot_id;
        if (!items.insert(item_id).second)
            return foundation::Result<void>::Failure(Error("gameplay.equipment.invalid_loadout", "loadout uses the same item more than once"));

        const auto slot = std::find_if(profile->slots.begin(), profile->slots.end(), [&](const auto& value) { return value.id == slot_id; });
        if (slot == profile->slots.end())
            return foundation::Result<void>::Failure(Error("gameplay.equipment.invalid_loadout", "loadout references a slot outside the subject profile"));
        const auto descriptor = item_provider_->Describe(item_id);
        if (!descriptor || !SlotAccepts(*slot, *descriptor))
            return foundation::Result<void>::Failure(Error("gameplay.equipment.invalid_loadout", "loadout item is incompatible with the requested slot"));
        if (slot->conflict_group.IsValid() && !conflict_groups.insert(slot->conflict_group).second)
            return foundation::Result<void>::Failure(Error("gameplay.equipment.invalid_loadout", "loadout contains conflicting slots"));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<EquipmentLoadoutId> EquipmentService::SaveLoadout(EquipmentLoadout l)
{
    if (auto ready = EnsureMutationReady(); !ready)
        return foundation::Result<EquipmentLoadoutId>::Failure(ready.GetError());
    auto valid = ValidateAndCanonicalizeLoadout(l);
    if (!valid) return foundation::Result<EquipmentLoadoutId>::Failure(valid.GetError());
    if (!l.id.IsValid()) l.id = EquipmentLoadoutId{loadout_ids_.Next()};
    if (!l.id.IsValid() || loadouts_.contains(l.id))
        return foundation::Result<EquipmentLoadoutId>::Failure(Error("gameplay.equipment.invalid_loadout", "invalid or duplicate loadout id"));
    AdvanceGeneratorPast(loadout_ids_, l.id);
    Bump();
    l.revision = revision_;
    const auto id = l.id;
    const auto subject = l.subject;
    loadouts_.emplace(id, std::move(l));
    EquipmentChange change;
    change.kind = EquipmentChangeKind::LoadoutSaved;
    change.subject = subject;
    change.revision = revision_;
    Record(std::move(change));
    return foundation::Result<EquipmentLoadoutId>::Success(id);
}

EquipmentChangeBatch EquipmentService::ReadChangesSince(std::uint64_t seq) const
{
    EquipmentChangeBatch batch;
    batch.latest_sequence = next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max() : next_change_sequence_ - 1;
    batch.oldest_available_sequence = changes_.empty() ? next_change_sequence_ : changes_.front().sequence;
    if (!changes_.empty() && seq < changes_.front().sequence - 1)
    {
        batch.snapshot_required = true;
        return batch;
    }
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(batch.changes),
                 [seq](const auto& c) { return c.sequence > seq; });
    return batch;
}

std::vector<EquipmentChange> EquipmentService::ChangesSince(std::uint64_t seq) const
{
    return ReadChangesSince(seq).changes;
}
EquipmentSnapshot EquipmentService::CaptureSnapshot() const
{
    EquipmentSnapshot s;
    for (const auto &[id, v] : profiles_)
    {
        (void)id;
        s.profiles.push_back(v);
    }
    for (const auto &[id, v] : bindings_)
    {
        (void)id;
        s.bindings.push_back(v);
    }
    for (const auto &[id, v] : loadouts_)
    {
        (void)id;
        s.loadouts.push_back(v);
    }
    std::sort(s.profiles.begin(), s.profiles.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.bindings.begin(), s.bindings.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.loadouts.begin(), s.loadouts.end(), [](auto &a, auto &b) { return a.id < b.id; });
    s.profile_ids = profile_ids_.GetSnapshot();
    s.slot_ids = slot_ids_.GetSnapshot();
    s.binding_ids = binding_ids_.GetSnapshot();
    s.operation_ids = operation_ids_.GetSnapshot();
    s.loadout_ids = loadout_ids_.GetSnapshot();
    s.revision = revision_;
    return s;
}
foundation::Result<void> EquipmentService::RestoreSnapshot(EquipmentSnapshot s)
{
    std::unordered_map<EquipmentProfileId, EquipmentProfile, IdHash> profiles;
    std::unordered_map<GameplayObjectRef, EquipmentProfileId> by_subject;
    std::uint64_t max_profile_low = 0;
    std::uint64_t max_slot_low = 0;
    std::uint64_t max_binding_low = 0;
    std::uint64_t max_loadout_low = 0;

    for (auto &v : s.profiles)
    {
        if (!v.id.IsValid() || !v.subject.IsValid() || v.revision.value > s.revision.value || profiles.contains(v.id) || by_subject.contains(v.subject))
            return foundation::Result<void>::Failure(Error("gameplay.equipment.restore_invalid", "invalid equipment profile snapshot"));
        if (v.definition.IsValid() && !profile_definitions_.contains(v.definition))
            return foundation::Result<void>::Failure(Error("gameplay.equipment.restore_invalid", "equipment profile references an unregistered definition"));
        std::sort(v.slots.begin(), v.slots.end(), [](const auto& a, const auto& b){ return a.id < b.id; });
        if (std::any_of(v.slots.begin(), v.slots.end(), [&](const auto& slot){
                return !slot.id.IsValid() || !slot.type.IsValid() || slot.revision.value > s.revision.value;
            }) ||
            std::adjacent_find(v.slots.begin(), v.slots.end(), [](const auto& a, const auto& b){ return a.id == b.id; }) != v.slots.end())
            return foundation::Result<void>::Failure(Error("gameplay.equipment.restore_invalid", "invalid equipment slots in snapshot"));
        if (v.id.value.High() == 0x3500) max_profile_low = std::max(max_profile_low, v.id.value.Low());
        for (const auto& slot : v.slots)
            if (slot.id.value.High() == 0x3501) max_slot_low = std::max(max_slot_low, slot.id.value.Low());
        by_subject.emplace(v.subject, v.id);
        profiles.emplace(v.id, std::move(v));
    }

    std::unordered_map<EquipmentBindingId, EquipmentBinding, IdHash> bindings;
    std::unordered_set<EquipmentItemId, IdHash> bound_items;
    for (auto &v : s.bindings)
    {
        std::sort(v.slots.begin(), v.slots.end());
        if (!v.id.IsValid() || !v.item.IsValid() || !by_subject.contains(v.subject) || bindings.contains(v.id) ||
            v.slots.empty() || v.revision.value > s.revision.value ||
            !ValidateGrants(v.grants) ||
            std::adjacent_find(v.slots.begin(), v.slots.end()) != v.slots.end() || !bound_items.insert(v.item).second)
            return foundation::Result<void>::Failure(Error("gameplay.equipment.restore_invalid", "invalid equipment binding snapshot"));

        const auto profile_id = by_subject.at(v.subject);
        const auto& profile = profiles.at(profile_id);
        for (const auto slot_id : v.slots)
            if (std::none_of(profile.slots.begin(), profile.slots.end(), [&](const auto& slot){ return slot.id == slot_id; }))
                return foundation::Result<void>::Failure(Error("gameplay.equipment.restore_invalid", "binding references missing slot"));
        if (v.id.value.High() == 0x3502) max_binding_low = std::max(max_binding_low, v.id.value.Low());
        bindings.emplace(v.id, std::move(v));
    }

    for (auto a = bindings.begin(); a != bindings.end(); ++a)
        for (auto b = std::next(a); b != bindings.end(); ++b)
            if (a->second.subject == b->second.subject)
            {
                const auto& profile = profiles.at(by_subject.at(a->second.subject));
                auto conflicts = [&](const EquipmentBinding& left, const EquipmentBinding& right){
                    for (const auto ls : left.slots)
                        for (const auto rs : right.slots)
                        {
                            if (ls == rs) return true;
                            const auto ldef = std::find_if(profile.slots.begin(), profile.slots.end(), [&](const auto& slot){ return slot.id == ls; });
                            const auto rdef = std::find_if(profile.slots.begin(), profile.slots.end(), [&](const auto& slot){ return slot.id == rs; });
                            if (ldef != profile.slots.end() && rdef != profile.slots.end() && ldef->conflict_group.IsValid() &&
                                ldef->conflict_group == rdef->conflict_group) return true;
                        }
                    return false;
                };
                if (conflicts(a->second, b->second))
                    return foundation::Result<void>::Failure(Error("gameplay.equipment.restore_invalid", "snapshot contains conflicting bindings"));
            }

    std::unordered_map<EquipmentLoadoutId, EquipmentLoadout, IdHash> loadouts;
    for (auto &v : s.loadouts)
    {
        if (!v.id.IsValid() || !v.subject.IsValid() || v.revision.value > s.revision.value || loadouts.contains(v.id) || !by_subject.contains(v.subject))
            return foundation::Result<void>::Failure(Error("gameplay.equipment.restore_invalid", "invalid loadout snapshot"));
        if (!item_provider_ && !v.desired.empty())
            return foundation::Result<void>::Failure(Error("gameplay.equipment.restore_invalid", "item provider is required to validate restored loadouts"));
        std::sort(v.desired.begin(), v.desired.end(), [](const auto& a, const auto& b) {
            if (a.first != b.first) return a.first < b.first;
            return a.second < b.second;
        });
        const auto& profile = profiles.at(by_subject.at(v.subject));
        std::unordered_set<EquipmentItemId, IdHash> desired_items;
        std::unordered_set<TypeId> groups;
        EquipmentSlotId previous{};
        for (const auto& [slot_id, item_id] : v.desired)
        {
            if (!slot_id.IsValid() || !item_id.IsValid() || (previous.IsValid() && previous == slot_id) || !desired_items.insert(item_id).second)
                return foundation::Result<void>::Failure(Error("gameplay.equipment.restore_invalid", "loadout is not a canonical slot/item set"));
            previous = slot_id;
            const auto slot = std::find_if(profile.slots.begin(), profile.slots.end(), [&](const auto& value){ return value.id == slot_id; });
            if (slot == profile.slots.end())
                return foundation::Result<void>::Failure(Error("gameplay.equipment.restore_invalid", "loadout references missing slot"));
            const auto descriptor = item_provider_->Describe(item_id);
            if (!descriptor || !SlotAccepts(*slot, *descriptor))
                return foundation::Result<void>::Failure(Error("gameplay.equipment.restore_invalid", "loadout item is incompatible with its slot"));
            if (slot->conflict_group.IsValid() && !groups.insert(slot->conflict_group).second)
                return foundation::Result<void>::Failure(Error("gameplay.equipment.restore_invalid", "loadout contains conflicting slots"));
        }
        if (v.id.value.High() == 0x3504) max_loadout_low = std::max(max_loadout_low, v.id.value.Low());
        loadouts.emplace(v.id, std::move(v));
    }

    if (!ValidGeneratorSnapshot(s.profile_ids, 0x3500, max_profile_low) ||
        !ValidGeneratorSnapshot(s.slot_ids, 0x3501, max_slot_low) ||
        !ValidGeneratorSnapshot(s.binding_ids, 0x3502, max_binding_low) ||
        !ValidGeneratorSnapshot(s.operation_ids, 0x3503, 0) ||
        !ValidGeneratorSnapshot(s.loadout_ids, 0x3504, max_loadout_low))
        return foundation::Result<void>::Failure(Error("gameplay.equipment.restore_invalid", "invalid equipment id generator snapshot"));

    profiles_ = std::move(profiles);
    profile_by_subject_ = std::move(by_subject);
    bindings_ = std::move(bindings);
    loadouts_ = std::move(loadouts);
    profile_ids_.Restore(s.profile_ids);
    slot_ids_.Restore(s.slot_ids);
    binding_ids_.Restore(s.binding_ids);
    operation_ids_.Restore(s.operation_ids);
    loadout_ids_.Restore(s.loadout_ids);
    revision_ = s.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    reconciliation_state_ = bindings_.empty() ? EquipmentReconciliationState::Ready : EquipmentReconciliationState::NeedsReconciliation;
    diagnostics_ = {};
    diagnostics_.profiles = profiles_.size();
    diagnostics_.bindings = bindings_.size();
    return foundation::Result<void>::Success();
}
EquipmentDiagnostics EquipmentService::GetDiagnostics() const noexcept
{
    auto d = diagnostics_;
    d.profiles = profiles_.size();
    d.bindings = bindings_.size();
    return d;
}
void EquipmentService::Record(EquipmentChange c)
{
    if (next_change_sequence_ == 0) return;
    c.sequence = next_change_sequence_;
    if (next_change_sequence_ == std::numeric_limits<std::uint64_t>::max()) next_change_sequence_ = 0;
    else ++next_change_sequence_;
    changes_.push_back(std::move(c));
    while (changes_.size() > change_capacity_) changes_.pop_front();
}
} // namespace epidemic::gameplay::equipment

