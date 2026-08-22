#pragma once
#include "Epidemic/GameFramework/Dialogue/dialogue.h"
#include "Epidemic/GameFramework/Economy/economy.h"
#include "Epidemic/GameFramework/Equipment/equipment.h"
#include "Epidemic/GameFramework/ItemsInventory/items_inventory.h"
#include "Epidemic/GameFramework/Knowledge/knowledge.h"
#include "Epidemic/GameFramework/Ownership/ownership.h"
#include "Epidemic/GameFramework/Processes/processes.h"

namespace epidemic::gameplay::integration
{
[[nodiscard]] constexpr GameplayObjectRef ItemPropertyRef(items::ItemInstanceId item) noexcept
{
    return {items::ItemsInventoryService::Domain(), item.value};
}

class EquipmentItemsAdapter final : public equipment::IEquipmentItemProvider
{
public:
    explicit EquipmentItemsAdapter(items::ItemsInventoryService& items) : items_(items) {}
    [[nodiscard]] std::optional<equipment::EquipmentItemDescriptor> Describe(equipment::EquipmentItemId item) const override;
    [[nodiscard]] foundation::Result<void> ReserveForEquipment(equipment::EquipmentItemId item,GameplayObjectRef subject,GameplayContext context) override;
    [[nodiscard]] foundation::Result<void> ReleaseFromEquipment(equipment::EquipmentItemId item,GameplayObjectRef subject,GameplayContext context) override;
    [[nodiscard]] foundation::Result<void> ExchangeEquipmentReservations(
        std::span<const equipment::EquipmentItemId> release_items,
        std::optional<equipment::EquipmentItemId> reserve_item,
        GameplayObjectRef subject,
        GameplayContext context) override;
private: items::ItemsInventoryService& items_;
};

struct ProcessItemInputPayload { items::ItemInstanceId item{}; };
struct ProcessItemOutputPayload { items::ItemDefinitionId definition{}; items::ContainerId container{}; };
struct ProcessItemReservationPayload { items::ItemReservationId reservation{}; };
class ItemProcessInputProvider final : public processes::IProcessInputProvider
{
public:
    explicit ItemProcessInputProvider(items::ItemsInventoryService& items):items_(items){}
    [[nodiscard]] static constexpr TypeId InputPayloadType() noexcept{return TypeId::FromString("framework.process.item.input");}
    [[nodiscard]] static constexpr TypeId ReservationPayloadType() noexcept{return TypeId::FromString("framework.process.item.reservation");}
    [[nodiscard]] foundation::Result<processes::ReservedProcessInput> Reserve(const processes::ProcessInputDefinition&,const processes::StartProcessRequest&,processes::ProcessInstanceId) override;
    [[nodiscard]] foundation::Result<void> Consume(const processes::ReservedProcessInput&,GameplayContext) override;
    [[nodiscard]] foundation::Result<void> Release(const processes::ReservedProcessInput&,GameplayContext) override;
private:items::ItemsInventoryService& items_;
};
class ItemProcessOutputHandler final : public processes::IProcessOutputHandler
{
public:
    explicit ItemProcessOutputHandler(items::ItemsInventoryService& items):items_(items){}
    [[nodiscard]] static constexpr processes::ProcessOutputTypeId OutputType() noexcept{return processes::ProcessOutputTypeId::FromString("framework.output.item");}
    [[nodiscard]] static constexpr TypeId OutputPayloadType() noexcept{return TypeId::FromString("framework.process.item.output");}
    [[nodiscard]] bool Supports(processes::ProcessOutputTypeId type)const noexcept override{return type==OutputType();}
    [[nodiscard]] foundation::Result<void> Produce(const processes::ProcessOutputDefinition&,const processes::ProcessInstance&,GameplayContext) override;
private:items::ItemsInventoryService& items_;
};

struct DialogueKnowledgePayload
{
    std::uint32_t speaker_index=0;
    std::uint32_t listener_index=1;
    knowledge::BeliefTypeId belief{};
    knowledge::KnowledgeTopicId topic{};
    knowledge::KnowledgeConfidence confidence=knowledge::KnowledgeConfidence::Medium;
    GameplayObjectRef subject{};
};
class DialogueKnowledgeConsequenceHandler final : public dialogue::IDialogueConsequenceHandler
{
public:
    explicit DialogueKnowledgeConsequenceHandler(knowledge::KnowledgeService& knowledge):knowledge_(knowledge){}
    [[nodiscard]] static constexpr dialogue::DialogueConsequenceTypeId Type() noexcept{return dialogue::DialogueConsequenceTypeId::FromString("dialogue.knowledge.transfer");}
    [[nodiscard]] dialogue::DialogueConsequenceState Execute(const dialogue::DialogueConsequenceDefinition&,const dialogue::DialogueConsequenceExecution&,const dialogue::ConversationSession&)const override;
private:knowledge::KnowledgeService& knowledge_;
};

struct TradeGoodsLine
{
    items::ItemInstanceId item{};
    items::ContainerId destination{};
    GameplayObjectRef from_owner{};
    GameplayObjectRef to_owner{};
};
struct CoordinatedTradePlan { economy::TradePlan money; std::vector<TradeGoodsLine> goods; };
class TradeCoordinator
{
public:
    TradeCoordinator(economy::EconomyService& economy,items::ItemsInventoryService& items,ownership::OwnershipService& ownership):economy_(economy),items_(items),ownership_(ownership){}
    [[nodiscard]] foundation::Result<economy::TradeTransactionId> Execute(CoordinatedTradePlan plan);
private:economy::EconomyService& economy_;items::ItemsInventoryService& items_;ownership::OwnershipService& ownership_;
};
}
