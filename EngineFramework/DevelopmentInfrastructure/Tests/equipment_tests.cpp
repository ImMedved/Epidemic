#include "Epidemic/Foundation/error.h"
#include "Epidemic/GameFramework/Equipment/equipment.h"
#include <cstdlib>
#include <iostream>
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::equipment;
namespace
{
void Check(bool v, const char *m)
{
    if (!v)
    {
        std::cerr << m << '\n';
        std::exit(1);
    }
}
GameplayObjectRef Ref(const char *d, const char *i)
{
    return {GameplayDomainId::FromString(d), GameplayObjectId::FromString(i)};
}
class MockItems final : public IEquipmentItemProvider
{
  public:
    EquipmentItemDescriptor desc{};
    bool reserved = false;
    std::optional<EquipmentItemDescriptor> Describe(EquipmentItemId id) const override
    {
        if (id != desc.id)
            return std::nullopt;
        auto d = desc;
        d.available = !reserved;
        return d;
    }
    epidemic::foundation::Result<void> ReserveForEquipment(EquipmentItemId, GameplayObjectRef, GameplayContext) override
    {
        if (reserved)
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("reserved", "reserved"));
        reserved = true;
        return epidemic::foundation::Result<void>::Success();
    }
    epidemic::foundation::Result<void> ReleaseFromEquipment(EquipmentItemId, GameplayObjectRef,
                                                            GameplayContext) override
    {
        reserved = false;
        return epidemic::foundation::Result<void>::Success();
    }
};
} // namespace
int main()
{
    EquipmentService s;
    MockItems items;
    items.desc.id = EquipmentItemId::FromRaw(1, 42);
    items.desc.tags.Add(TagId::FromString("item.weapon"));
    items.desc.available = true;
    items.desc.revision = Revision{1};
    s.SetItemProvider(&items);
    EquipmentProfile p;
    p.subject = Ref("actor", "hero");
    auto pid = s.CreateProfile(p);
    Check(static_cast<bool>(pid), "profile");
    EquipmentSlotDefinition main;
    main.type = EquipmentSlotTypeId::FromString("main_hand");
    main.accepted_tags.Add(TagId::FromString("item.weapon"));
    auto mainid = s.AddSlot(pid.Value(), main);
    Check(static_cast<bool>(mainid), "main slot");
    EquipmentSlotDefinition off;
    off.type = EquipmentSlotTypeId::FromString("off_hand");
    off.accepted_tags.Add(TagId::FromString("item.weapon"));
    auto offid = s.AddSlot(pid.Value(), off);
    Check(static_cast<bool>(offid), "off slot");
    auto plan = s.PrepareEquip(p.subject, items.desc.id, {mainid.Value(), offid.Value()});
    Check(static_cast<bool>(plan), "prepare two slot");
    auto binding = s.CommitEquip(plan.Value());
    Check(static_cast<bool>(binding), "commit equip");
    Check(s.FindBindings(p.subject).size() == 1, "binding active");
    Check(s.GetEquippedItem(p.subject, mainid.Value()).has_value(), "main occupied");
    Check(static_cast<bool>(s.Unequip(binding.Value())), "unequip");
    Check(!items.reserved, "item released");
    auto snap = s.CaptureSnapshot();
    EquipmentService restored;
    restored.SetItemProvider(&items);
    Check(static_cast<bool>(restored.RestoreSnapshot(std::move(snap))), "restore equipment");
    Check(restored.FindBindings(p.subject).empty(), "unequipped state restored");
    return 0;
}
