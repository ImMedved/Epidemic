#include "Epidemic/Foundation/error.h"
#include "Epidemic/GameFramework/Equipment/equipment.h"

#include <cstdlib>
#include <iostream>
#include <unordered_map>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::equipment;

namespace
{
void Check(bool value, const char* message)
{
    if (!value)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

GameplayObjectRef Ref(const char* domain, const char* id)
{
    return {GameplayDomainId::FromString(domain), GameplayObjectId::FromString(id)};
}

class MockItems final : public IEquipmentItemProvider
{
  public:
    std::unordered_map<EquipmentItemId, EquipmentItemDescriptor, IdHash> descriptors;
    std::unordered_map<EquipmentItemId, GameplayObjectRef, IdHash> reservations;
    bool reject_reconciliation = false;

    std::optional<EquipmentItemDescriptor> Describe(EquipmentItemId id) const override
    {
        const auto it = descriptors.find(id);
        if (it == descriptors.end()) return std::nullopt;
        auto descriptor = it->second;
        descriptor.available = !reservations.contains(id);
        return descriptor;
    }

    epidemic::foundation::Result<void> ReserveForEquipment(EquipmentItemId id, GameplayObjectRef subject, GameplayContext) override
    {
        if (!descriptors.contains(id) || reservations.contains(id))
            return epidemic::foundation::Result<void>::Failure(epidemic::foundation::Error::Create("reserved", "reserved"));
        reservations.emplace(id, subject);
        return epidemic::foundation::Result<void>::Success();
    }

    epidemic::foundation::Result<void> ReleaseFromEquipment(EquipmentItemId id, GameplayObjectRef subject, GameplayContext) override
    {
        const auto it = reservations.find(id);
        if (it != reservations.end() && it->second == subject) reservations.erase(it);
        return epidemic::foundation::Result<void>::Success();
    }

    epidemic::foundation::Result<void> ExchangeEquipmentReservations(std::span<const EquipmentItemId> releases,
                                                            std::optional<EquipmentItemId> reserve,
                                                            GameplayObjectRef subject,
                                                            GameplayContext) override
    {
        for (const auto item : releases)
        {
            const auto it = reservations.find(item);
            if (it == reservations.end() || it->second != subject)
                return epidemic::foundation::Result<void>::Failure(
                    epidemic::foundation::Error::Create("release_missing", "equipment reservation missing"));
        }
        if (reserve.has_value() && reservations.contains(*reserve) &&
            std::find(releases.begin(), releases.end(), *reserve) == releases.end())
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("reserve_conflict", "replacement item is already reserved"));
        for (const auto item : releases) reservations.erase(item);
        if (reserve.has_value()) reservations[*reserve] = subject;
        return epidemic::foundation::Result<void>::Success();
    }

    epidemic::foundation::Result<void> ReconcileEquipmentReservation(EquipmentItemId item,
                                                            GameplayObjectRef subject,
                                                            EquipmentBindingId,
                                                            GameplayContext) override
    {
        if (reject_reconciliation)
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("reconcile_rejected", "reconciliation rejected"));
        const auto it = reservations.find(item);
        if (it == reservations.end() || it->second != subject)
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("reservation_missing", "persisted reservation is missing"));
        return epidemic::foundation::Result<void>::Success();
    }
};

EquipmentItemDescriptor Weapon(EquipmentItemId id)
{
    EquipmentItemDescriptor descriptor;
    descriptor.id = id;
    descriptor.tags.Add(TagId::FromString("item.weapon"));
    descriptor.available = true;
    descriptor.revision = Revision{1};
    return descriptor;
}
} // namespace

int main()
{
    MockItems items;
    const auto item_a = EquipmentItemId::FromRaw(1, 42);
    const auto item_b = EquipmentItemId::FromRaw(1, 43);
    items.descriptors.emplace(item_a, Weapon(item_a));
    items.descriptors.emplace(item_b, Weapon(item_b));

    EquipmentService service;
    service.SetItemProvider(&items);
    service.Freeze();

    EquipmentProfile profile;
    profile.subject = Ref("actor", "hero");
    const auto profile_id = service.CreateProfile(profile);
    Check(static_cast<bool>(profile_id), "profile creation failed");

    EquipmentSlotDefinition main;
    main.type = EquipmentSlotTypeId::FromString("main_hand");
    main.accepted_tags.Add(TagId::FromString("item.weapon"));
    const auto main_id = service.AddSlot(profile_id.Value(), main);
    Check(static_cast<bool>(main_id), "main slot creation failed");

    EquipmentSlotDefinition off;
    off.type = EquipmentSlotTypeId::FromString("off_hand");
    off.accepted_tags.Add(TagId::FromString("item.weapon"));
    const auto off_id = service.AddSlot(profile_id.Value(), off);
    Check(static_cast<bool>(off_id), "off slot creation failed");

    const auto slot_changes = service.ReadChangesSince(ChangeCursor{}).changes;
    Check(std::count_if(slot_changes.begin(), slot_changes.end(), [](const auto& change) {
              return change.kind == EquipmentChangeKind::SlotAdded;
          }) == 2,
          "dynamic slots must be observable in the change journal");

    auto plan = service.PrepareEquip(profile.subject, item_a, {main_id.Value(), off_id.Value()});
    Check(static_cast<bool>(plan), "prepare two-slot equipment failed");
    const auto binding = service.CommitEquip(plan.Value());
    Check(static_cast<bool>(binding), "commit equip failed");
    Check(items.reservations.contains(item_a), "equipment reservation was not created");

    const auto before_invalid_state = service.CaptureSnapshot();
    Check(!service.SetBindingState(binding.Value(), static_cast<EquipmentBindingState>(999)),
          "invalid binding state must be rejected");
    const auto after_invalid_state = service.CaptureSnapshot();
    Check(after_invalid_state.revision == before_invalid_state.revision, "invalid binding state changed revision");
    Check(after_invalid_state.bindings.size() == before_invalid_state.bindings.size() &&
              after_invalid_state.bindings.front().state == before_invalid_state.bindings.front().state,
          "invalid binding state changed authoritative binding");

    const auto bound_snapshot = service.CaptureSnapshot();
    EquipmentService restored;
    restored.SetItemProvider(&items);
    restored.Freeze();
    Check(static_cast<bool>(restored.RestoreSnapshot(bound_snapshot)), "restore bound equipment failed");
    Check(restored.NeedsReconciliation(), "restored bindings must require reconciliation");
    Check(!restored.Unequip(binding.Value()), "mutation must be blocked before restored reservations are reconciled");
    Check(static_cast<bool>(restored.ReconcileRestoredBindings()), "restored reservation reconciliation failed");
    Check(!restored.NeedsReconciliation(), "service must become ready after successful reconciliation");
    Check(static_cast<bool>(restored.Unequip(binding.Value())), "unequip after reconciliation failed");
    Check(!items.reservations.contains(item_a), "unequip did not release reservation");

    EquipmentLoadout valid_loadout;
    valid_loadout.subject = profile.subject;
    valid_loadout.desired = {{main_id.Value(), item_a}, {off_id.Value(), item_b}};
    const auto loadout_id = restored.SaveLoadout(valid_loadout);
    Check(static_cast<bool>(loadout_id), "valid loadout was rejected");

    EquipmentLoadout duplicate_item;
    duplicate_item.subject = profile.subject;
    duplicate_item.desired = {{main_id.Value(), item_a}, {off_id.Value(), item_a}};
    Check(!restored.SaveLoadout(duplicate_item), "loadout must reject duplicate item use");

    EquipmentLoadout duplicate_slot;
    duplicate_slot.subject = profile.subject;
    duplicate_slot.desired = {{main_id.Value(), item_a}, {main_id.Value(), item_b}};
    Check(!restored.SaveLoadout(duplicate_slot), "loadout must reject duplicate desired slots");

    auto invalid_generators = restored.CaptureSnapshot();
    invalid_generators.profile_ids.scope = 0x9999;
    Check(!restored.RestoreSnapshot(std::move(invalid_generators)), "restore must reject a foreign profile generator scope");
    Check(restored.FindProfile(profile.subject) != nullptr, "failed restore must not replace current equipment state");

    EquipmentService bounded(2);
    bounded.SetItemProvider(&items);
    bounded.Freeze();
    EquipmentProfile journal_profile;
    journal_profile.subject = Ref("actor", "journal");
    const auto journal_profile_id = bounded.CreateProfile(journal_profile);
    Check(static_cast<bool>(journal_profile_id), "journal profile failed");
    EquipmentSlotDefinition first;
    first.type = EquipmentSlotTypeId::FromString("first");
    Check(static_cast<bool>(bounded.AddSlot(journal_profile_id.Value(), first)), "journal first slot failed");
    EquipmentSlotDefinition second;
    second.type = EquipmentSlotTypeId::FromString("second");
    Check(static_cast<bool>(bounded.AddSlot(journal_profile_id.Value(), second)), "journal second slot failed");
    const auto batch = bounded.ReadChangesSince(ChangeCursor{});
    Check(batch.snapshot_required, "bounded journal must require snapshot when the consumer fell behind retention");
    Check(batch.changes.empty(), "snapshot-required batch must not expose an incomplete change suffix as complete history");

    return 0;
}
