#pragma once
#include "Epidemic/GameFramework/Dialogue/dialogue.h"
#include "Epidemic/GameFramework/Economy/economy.h"
#include "Epidemic/GameFramework/Equipment/equipment.h"
#include "Epidemic/GameFramework/ItemsInventory/items_inventory.h"
#include "Epidemic/GameFramework/Knowledge/knowledge.h"
#include "Epidemic/GameFramework/Ownership/ownership.h"
#include "Epidemic/GameFramework/Processes/processes.h"

#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

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
    [[nodiscard]] foundation::Result<void> ReserveForEquipment(equipment::EquipmentItemId item, GameplayObjectRef subject, GameplayContext context) override;
    [[nodiscard]] foundation::Result<void> ReleaseFromEquipment(equipment::EquipmentItemId item, GameplayObjectRef subject, GameplayContext context) override;
    [[nodiscard]] foundation::Result<void> ReconcileEquipmentReservation(
        equipment::EquipmentItemId item, GameplayObjectRef subject, equipment::EquipmentBindingId binding,
        GameplayContext context) override;
    [[nodiscard]] foundation::Result<void> ExchangeEquipmentReservations(
        std::span<const equipment::EquipmentItemId> release_items,
        std::optional<equipment::EquipmentItemId> reserve_item,
        GameplayObjectRef subject,
        GameplayContext context) override;
private:
    items::ItemsInventoryService& items_;
};

struct ProcessItemInputPayload { items::ItemInstanceId item{}; };
struct ProcessItemOutputPayload { items::ItemDefinitionId definition{}; items::ContainerId container{}; };
struct ProcessItemReservationPayload { items::ItemReservationId reservation{}; };
struct PreparedProcessItemOutputPayload
{
    items::ItemDefinitionId definition{};
    items::ContainerId container{};
    items::ItemInstanceId item{};
    processes::Fixed amount = 0;
};

[[nodiscard]] processes::RegisteredPayload EncodeProcessItemInputPayload(ProcessItemInputPayload payload);
[[nodiscard]] processes::RegisteredPayload EncodeProcessItemOutputPayload(ProcessItemOutputPayload payload);

class ItemProcessInputProvider final : public processes::IProcessInputProvider
{
public:
    explicit ItemProcessInputProvider(items::ItemsInventoryService& items) : items_(items) {}
    [[nodiscard]] static constexpr TypeId InputPayloadType() noexcept { return TypeId::FromString("framework.process.item.input"); }
    [[nodiscard]] static constexpr TypeId ReservationPayloadType() noexcept { return TypeId::FromString("framework.process.item.reservation"); }
    [[nodiscard]] foundation::Result<void> Validate(const processes::ProcessInputDefinition&, const processes::StartProcessRequest&, processes::ProcessInstanceId) override;
    [[nodiscard]] foundation::Result<processes::ReservedProcessInput> Reserve(const processes::ProcessInputDefinition&, const processes::StartProcessRequest&, processes::ProcessInstanceId) override;
    [[nodiscard]] foundation::Result<void> Consume(const processes::ReservedProcessInput&, GameplayContext) override;
    [[nodiscard]] foundation::Result<void> Release(const processes::ReservedProcessInput&, GameplayContext) override;
private:
    items::ItemsInventoryService& items_;
};

class ItemProcessOutputHandler final : public processes::IProcessOutputHandler
{
public:
    explicit ItemProcessOutputHandler(items::ItemsInventoryService& items) : items_(items) {}
    [[nodiscard]] static constexpr processes::ProcessOutputTypeId OutputType() noexcept { return processes::ProcessOutputTypeId::FromString("framework.output.item"); }
    [[nodiscard]] static constexpr TypeId OutputPayloadType() noexcept { return TypeId::FromString("framework.process.item.output"); }
    [[nodiscard]] static constexpr TypeId PreparedPayloadType() noexcept { return TypeId::FromString("framework.process.item.output.prepared"); }
    [[nodiscard]] bool Supports(processes::ProcessOutputTypeId type) const noexcept override { return type == OutputType(); }
    [[nodiscard]] foundation::Result<processes::PreparedProcessOutput> Prepare(const processes::ProcessOutputDefinition&, const processes::ProcessInstance&, GameplayContext) override;
    [[nodiscard]] foundation::Result<void> Commit(const processes::PreparedProcessOutput&, const processes::ProcessInstance&, GameplayContext) override;
    [[nodiscard]] foundation::Result<void> Cancel(const processes::PreparedProcessOutput&, const processes::ProcessInstance&, GameplayContext) override;
private:
    items::ItemsInventoryService& items_;
};

struct DialogueShareKnowledgePayload
{
    std::uint32_t speaker_index = 0;
    std::uint32_t listener_index = 1;
    knowledge::KnowledgeRecordId record{};
    knowledge::KnowledgeShareMode mode = knowledge::KnowledgeShareMode::Tell;
};
struct DialogueAssertKnowledgePayload
{
    std::uint32_t speaker_index = 0;
    std::uint32_t listener_index = 1;
    knowledge::BeliefTypeId belief{};
    knowledge::KnowledgeTopicId topic{};
    knowledge::KnowledgeAssertionValue assertion = knowledge::KnowledgeAssertionValue::Affirmed;
    knowledge::KnowledgeEpistemicState epistemic_state = knowledge::KnowledgeEpistemicState::Suspected;
    knowledge::KnowledgeConfidence confidence = knowledge::KnowledgeConfidence::Medium;
    GameplayObjectRef subject{};
};
class DialogueKnowledgeConsequenceHandler final : public dialogue::IDialogueConsequenceHandler
{
public:
    explicit DialogueKnowledgeConsequenceHandler(knowledge::KnowledgeService& knowledge) : knowledge_(knowledge) {}
    [[nodiscard]] static constexpr dialogue::DialogueConsequenceTypeId ShareType() noexcept { return dialogue::DialogueConsequenceTypeId::FromString("dialogue.knowledge.share"); }
    [[nodiscard]] static constexpr dialogue::DialogueConsequenceTypeId AssertType() noexcept { return dialogue::DialogueConsequenceTypeId::FromString("dialogue.knowledge.assert"); }
    [[nodiscard]] dialogue::DialogueConsequenceState Execute(const dialogue::DialogueConsequenceDefinition&, const dialogue::DialogueConsequenceExecution&, const dialogue::ConversationSession&) const override;
private:
    knowledge::KnowledgeService& knowledge_;
};

struct TradeExecutionId
{
    GameplayObjectId value{};
    static constexpr TradeExecutionId FromRaw(std::uint64_t high, std::uint64_t low) noexcept { return {GameplayObjectId::FromRaw(high, low)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const TradeExecutionId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const TradeExecutionId&) const noexcept = default;
};
struct TradeExecutionIdHash
{
    [[nodiscard]] std::size_t operator()(TradeExecutionId id) const noexcept { return std::hash<GameplayObjectId>{}(id.value); }
};

struct TradeGoodsLine
{
    items::ItemInstanceId item{};
    items::ContainerId destination{};
    GameplayObjectRef from_owner{};
    GameplayObjectRef to_owner{};
};
struct CoordinatedTradePlan
{
    economy::TradePlan money;
    std::vector<TradeGoodsLine> goods;
};
enum class TradeGoodsLegState { NotReserved, Reserved, Released, ItemTransferred, OwnershipTransferred };
enum class TradeMoneyLegState { NotPrepared, Prepared, Reserved, Committed, Cancelled, ReconciliationRequired };
enum class CoordinatedTradeState { Prepared, Committing, ReconciliationRequired, Completed, Cancelled };
struct CoordinatedTradeGoodsExecution
{
    TradeGoodsLine line;
    items::Fixed quantity = 0;
    items::ItemLocation source{};
    items::ItemReservationId reservation{};
    Revision ownership_revision{};
    ownership::PropertyDomainId ownership_domain{};
    TradeGoodsLegState state = TradeGoodsLegState::NotReserved;
};
struct CoordinatedTradeExecution
{
    TradeExecutionId id{};
    economy::TradeTransactionId money_transaction{};
    TradeMoneyLegState money_state = TradeMoneyLegState::NotPrepared;
    GameplayObjectRef buyer{};
    GameplayObjectRef seller{};
    GameplayContext context{};
    std::vector<CoordinatedTradeGoodsExecution> goods;
    CoordinatedTradeState state = CoordinatedTradeState::Prepared;
};
struct TradeCoordinatorSnapshot
{
    std::vector<CoordinatedTradeExecution> executions;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot execution_ids{};
};

class TradeCoordinator
{
public:
    TradeCoordinator(economy::EconomyService& economy, items::ItemsInventoryService& items, ownership::OwnershipService& ownership)
        : economy_(economy), items_(items), ownership_(ownership) {}

    [[nodiscard]] foundation::Result<TradeExecutionId> Prepare(CoordinatedTradePlan plan);
    [[nodiscard]] foundation::Result<economy::TradeTransactionId> Continue(TradeExecutionId execution);
    [[nodiscard]] foundation::Result<economy::TradeTransactionId> Execute(CoordinatedTradePlan plan);
    [[nodiscard]] foundation::Result<void> Cancel(TradeExecutionId execution);
    [[nodiscard]] const CoordinatedTradeExecution* FindExecution(TradeExecutionId id) const noexcept;
    [[nodiscard]] TradeCoordinatorSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(TradeCoordinatorSnapshot snapshot);
    void PruneCompleted(std::size_t keep_recent);

private:
    economy::EconomyService& economy_;
    items::ItemsInventoryService& items_;
    ownership::OwnershipService& ownership_;
    MonotonicIdGenerator<GameplayObjectId> execution_ids_{0x4700};
    std::unordered_map<TradeExecutionId, CoordinatedTradeExecution, TradeExecutionIdHash> executions_;
};
}
