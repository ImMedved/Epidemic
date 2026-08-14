#pragma once
#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::equipment
{
#define EQ_TYPE(name) struct name{TypeId value{};static constexpr name FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};}[[nodiscard]]constexpr bool IsValid()const noexcept{return value.IsValid();}[[nodiscard]]constexpr bool operator==(const name&)const noexcept=default;[[nodiscard]]constexpr auto operator<=>(const name&)const noexcept=default;}
#define EQ_OBJ(name) struct name{GameplayObjectId value{};static constexpr name FromRaw(std::uint64_t h,std::uint64_t l) noexcept{return {GameplayObjectId::FromRaw(h,l)};}[[nodiscard]]constexpr bool IsValid()const noexcept{return value.IsValid();}[[nodiscard]]constexpr bool operator==(const name&)const noexcept=default;[[nodiscard]]constexpr auto operator<=>(const name&)const noexcept=default;}
EQ_TYPE(EquipmentSlotTypeId); EQ_TYPE(EquipmentGrantTypeId); EQ_TYPE(EquipmentProfileDefinitionId);
EQ_OBJ(EquipmentProfileId); EQ_OBJ(EquipmentSlotId); EQ_OBJ(EquipmentBindingId); EQ_OBJ(EquipOperationId); EQ_OBJ(EquipmentLoadoutId); EQ_OBJ(EquipmentItemId);
#undef EQ_TYPE
#undef EQ_OBJ
struct IdHash{template<class T>[[nodiscard]]std::size_t operator()(const T&id)const noexcept{return std::hash<decltype(id.value)>{}(id.value);}};

enum class EquipmentBindingState{Active,Disabled,Broken,PendingRemoval};
enum class EquipmentChangeKind{ProfileCreated,BindingCreated,BindingRemoved,BindingStateChanged,LoadoutActivated};
struct EquipmentGrantDescriptor{EquipmentGrantTypeId type{};TypeId value_type{};std::vector<std::byte> payload;};
struct EquipmentSlotDefinition{EquipmentSlotId id{};EquipmentSlotTypeId type{};GameplayTagSet accepted_tags;GameplayTagSet blocked_tags;TypeId conflict_group{};Revision revision{};};
struct EquipmentProfile{EquipmentProfileId id{};GameplayObjectRef subject{};std::vector<EquipmentSlotDefinition> slots;Revision revision{};};
struct EquipmentItemDescriptor{EquipmentItemId id{};GameplayTagSet tags;Revision revision{};bool available=false;};
struct EquipmentBinding{EquipmentBindingId id{};GameplayObjectRef subject{};EquipmentItemId item{};std::vector<EquipmentSlotId> slots;std::vector<EquipmentGrantDescriptor> grants;EquipmentBindingState state=EquipmentBindingState::Active;Revision item_revision{};Revision revision{};};
struct EquipmentLoadout{EquipmentLoadoutId id{};GameplayObjectRef subject{};std::vector<std::pair<EquipmentSlotId,EquipmentItemId>> desired;Revision revision{};};
struct EquipPlan{EquipOperationId id{};GameplayObjectRef subject{};EquipmentItemId item{};std::vector<EquipmentSlotId> slots;Revision profile_revision{};Revision item_revision{};std::vector<EquipmentBindingId> conflicting_bindings;GameplayContext context{};};
struct EquipmentChange{std::uint64_t sequence=0;EquipmentChangeKind kind=EquipmentChangeKind::BindingCreated;GameplayObjectRef subject{};EquipmentBindingId binding{};EquipmentItemId item{};GameplayContext context{};Revision revision{};};
struct EquipmentSnapshot{std::vector<EquipmentProfile> profiles;std::vector<EquipmentBinding> bindings;std::vector<EquipmentLoadout> loadouts;MonotonicIdGenerator<GameplayObjectId>::Snapshot profile_ids{};MonotonicIdGenerator<GameplayObjectId>::Snapshot slot_ids{};MonotonicIdGenerator<GameplayObjectId>::Snapshot binding_ids{};MonotonicIdGenerator<GameplayObjectId>::Snapshot operation_ids{};MonotonicIdGenerator<GameplayObjectId>::Snapshot loadout_ids{};Revision revision{};};
struct EquipmentDiagnostics{std::uint64_t profiles=0;std::uint64_t bindings=0;std::uint64_t equip_operations=0;std::uint64_t rejected_operations=0;};

class IEquipmentItemProvider
{
public:
    virtual ~IEquipmentItemProvider()=default;
    [[nodiscard]] virtual std::optional<EquipmentItemDescriptor> Describe(EquipmentItemId item) const=0;
    [[nodiscard]] virtual foundation::Result<void> ReserveForEquipment(EquipmentItemId item,GameplayObjectRef subject,GameplayContext context)=0;
    [[nodiscard]] virtual foundation::Result<void> ReleaseFromEquipment(EquipmentItemId item,GameplayObjectRef subject,GameplayContext context)=0;
};

class EquipmentService
{
public:
    EquipmentService();
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept{return GameplayDomainId::FromString("framework.equipment");}
    void SetItemProvider(IEquipmentItemProvider* provider) noexcept{item_provider_=provider;}
    [[nodiscard]] foundation::Result<EquipmentProfileId> CreateProfile(EquipmentProfile profile);
    [[nodiscard]] foundation::Result<EquipmentSlotId> AddSlot(EquipmentProfileId profile,EquipmentSlotDefinition slot);
    [[nodiscard]] const EquipmentProfile* FindProfile(GameplayObjectRef subject) const noexcept;
    [[nodiscard]] const EquipmentBinding* FindBinding(EquipmentBindingId id) const noexcept;
    [[nodiscard]] foundation::Result<EquipPlan> PrepareEquip(GameplayObjectRef subject,EquipmentItemId item,std::vector<EquipmentSlotId> slots,GameplayContext context={});
    [[nodiscard]] foundation::Result<EquipmentBindingId> CommitEquip(const EquipPlan& plan,std::vector<EquipmentGrantDescriptor> grants={});
    [[nodiscard]] foundation::Result<void> Unequip(EquipmentBindingId binding,GameplayContext context={});
    [[nodiscard]] foundation::Result<void> SetBindingState(EquipmentBindingId binding,EquipmentBindingState state,GameplayContext context={});
    [[nodiscard]] std::vector<EquipmentBinding> FindBindings(GameplayObjectRef subject) const;
    [[nodiscard]] std::optional<EquipmentItemId> GetEquippedItem(GameplayObjectRef subject,EquipmentSlotId slot) const;
    [[nodiscard]] bool CanEquip(GameplayObjectRef subject,EquipmentItemId item,const std::vector<EquipmentSlotId>& slots) const;
    [[nodiscard]] foundation::Result<EquipmentLoadoutId> SaveLoadout(EquipmentLoadout loadout);
    [[nodiscard]] std::vector<EquipmentChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] EquipmentSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(EquipmentSnapshot snapshot);
    [[nodiscard]] EquipmentDiagnostics GetDiagnostics() const noexcept;
private:
    void Bump() noexcept{++revision_.value;}
    void Record(EquipmentChange change);
    [[nodiscard]] EquipmentProfile* MutableProfile(EquipmentProfileId id) noexcept;
    [[nodiscard]] bool SlotAccepts(const EquipmentSlotDefinition& slot,const EquipmentItemDescriptor& item) const noexcept;
    Revision revision_{};
    std::unordered_map<EquipmentProfileId,EquipmentProfile,IdHash> profiles_;
    std::unordered_map<GameplayObjectRef,EquipmentProfileId> profile_by_subject_;
    std::unordered_map<EquipmentBindingId,EquipmentBinding,IdHash> bindings_;
    std::unordered_map<EquipmentLoadoutId,EquipmentLoadout,IdHash> loadouts_;
    MonotonicIdGenerator<GameplayObjectId> profile_ids_{0x3500},slot_ids_{0x3501},binding_ids_{0x3502},operation_ids_{0x3503},loadout_ids_{0x3504};
    IEquipmentItemProvider* item_provider_=nullptr;
    std::vector<EquipmentChange> changes_;std::uint64_t next_change_sequence_=1;EquipmentDiagnostics diagnostics_{};
};
}
