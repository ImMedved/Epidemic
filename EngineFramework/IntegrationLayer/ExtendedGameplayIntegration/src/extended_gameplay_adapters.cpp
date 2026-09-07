#include "Epidemic/GameFramework/ExtendedGameplayIntegration/extended_gameplay_adapters.h"
#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <type_traits>
#include <unordered_set>

namespace epidemic::gameplay::integration
{
namespace
{
foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}

template <class T> std::optional<T> ReadTrivial(const std::vector<std::byte>& bytes)
{
    static_assert(std::is_trivially_copyable_v<T>);
    if (bytes.size() != sizeof(T)) return std::nullopt;
    T out{};
    std::memcpy(&out, bytes.data(), sizeof(T));
    return out;
}

void AppendU64(std::vector<std::byte>& out, std::uint64_t value)
{
    for (unsigned shift = 0; shift < 64; shift += 8)
        out.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
}
void AppendI64(std::vector<std::byte>& out, std::int64_t value)
{
    AppendU64(out, std::bit_cast<std::uint64_t>(value));
}
bool ReadU64(std::span<const std::byte> bytes, std::size_t& offset, std::uint64_t& value)
{
    if (offset + 8 > bytes.size()) return false;
    value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(bytes[offset++])) << (i * 8);
    return true;
}
bool ReadI64(std::span<const std::byte> bytes, std::size_t& offset, std::int64_t& value)
{
    std::uint64_t encoded = 0;
    if (!ReadU64(bytes, offset, encoded)) return false;
    value = std::bit_cast<std::int64_t>(encoded);
    return true;
}
void AppendObjectId(std::vector<std::byte>& out, GameplayObjectId value)
{
    AppendU64(out, value.High());
    AppendU64(out, value.Low());
}
bool ReadObjectId(std::span<const std::byte> bytes, std::size_t& offset, GameplayObjectId& value)
{
    std::uint64_t high = 0, low = 0;
    if (!ReadU64(bytes, offset, high) || !ReadU64(bytes, offset, low)) return false;
    value = GameplayObjectId::FromRaw(high, low);
    return true;
}

std::optional<ProcessItemInputPayload> DecodeProcessItemInput(const processes::RegisteredPayload& payload)
{
    if (payload.type != ItemProcessInputProvider::InputPayloadType() || !payload.portable || payload.schema_version != 1)
        return std::nullopt;
    std::size_t offset = 0;
    GameplayObjectId id{};
    if (!ReadObjectId(payload.bytes, offset, id) || offset != payload.bytes.size()) return std::nullopt;
    return ProcessItemInputPayload{items::ItemInstanceId{id}};
}
std::optional<ProcessItemOutputPayload> DecodeProcessItemOutput(const processes::RegisteredPayload& payload)
{
    if (payload.type != ItemProcessOutputHandler::OutputPayloadType() || !payload.portable || payload.schema_version != 1)
        return std::nullopt;
    std::size_t offset = 0;
    std::uint64_t definition = 0;
    GameplayObjectId container{};
    if (!ReadU64(payload.bytes, offset, definition) || !ReadObjectId(payload.bytes, offset, container) || offset != payload.bytes.size())
        return std::nullopt;
    return ProcessItemOutputPayload{items::ItemDefinitionId{TypeId::FromRaw(definition)}, items::ContainerId{container}};
}
processes::RegisteredPayload EncodeReservationToken(ProcessItemReservationPayload payload)
{
    std::vector<std::byte> bytes;
    bytes.reserve(16);
    AppendObjectId(bytes, payload.reservation.value);
    return processes::RegisteredPayload::FromVersioned(ItemProcessInputProvider::ReservationPayloadType(), 1, std::move(bytes));
}
std::optional<ProcessItemReservationPayload> DecodeReservationToken(const processes::RegisteredPayload& payload)
{
    if (payload.type != ItemProcessInputProvider::ReservationPayloadType() || !payload.portable || payload.schema_version != 1)
        return std::nullopt;
    std::size_t offset = 0;
    GameplayObjectId reservation{};
    if (!ReadObjectId(payload.bytes, offset, reservation) || offset != payload.bytes.size()) return std::nullopt;
    return ProcessItemReservationPayload{items::ItemReservationId{reservation}};
}
processes::RegisteredPayload EncodePreparedOutputToken(PreparedProcessItemOutputPayload payload)
{
    std::vector<std::byte> bytes;
    bytes.reserve(48);
    AppendU64(bytes, payload.definition.value.Raw());
    AppendObjectId(bytes, payload.container.value);
    AppendObjectId(bytes, payload.item.value);
    AppendI64(bytes, payload.amount);
    return processes::RegisteredPayload::FromVersioned(ItemProcessOutputHandler::PreparedPayloadType(), 1, std::move(bytes));
}
std::optional<PreparedProcessItemOutputPayload> DecodePreparedOutputToken(const processes::RegisteredPayload& payload)
{
    if (payload.type != ItemProcessOutputHandler::PreparedPayloadType() || !payload.portable || payload.schema_version != 1)
        return std::nullopt;
    std::size_t offset = 0;
    std::uint64_t definition = 0;
    GameplayObjectId container{}, item{};
    std::int64_t amount = 0;
    if (!ReadU64(payload.bytes, offset, definition) || !ReadObjectId(payload.bytes, offset, container) ||
        !ReadObjectId(payload.bytes, offset, item) || !ReadI64(payload.bytes, offset, amount) || offset != payload.bytes.size())
        return std::nullopt;
    return PreparedProcessItemOutputPayload{items::ItemDefinitionId{TypeId::FromRaw(definition)}, items::ContainerId{container},
                                            items::ItemInstanceId{item}, amount};
}

items::ItemInstanceId ProcessOutputItemId(processes::ProcessInstanceId instance, processes::ProcessOutputId output) noexcept
{
    const auto salt = output.value.Raw();
    auto high = instance.value.High() ^ salt ^ 0x9e3779b97f4a7c15ull;
    auto low = instance.value.Low() ^ std::rotl(salt, 23) ^ 0xd1b54a32d192ed03ull;
    if (high == 0 && low == 0) low = 1;
    return items::ItemInstanceId::FromRaw(high, low);
}

bool SameTradeParty(GameplayObjectRef a, GameplayObjectRef b) noexcept { return a == b && a.IsValid(); }
} // namespace

processes::RegisteredPayload EncodeProcessItemInputPayload(ProcessItemInputPayload payload)
{
    std::vector<std::byte> bytes;
    bytes.reserve(16);
    AppendObjectId(bytes, payload.item.value);
    return processes::RegisteredPayload::FromVersioned(ItemProcessInputProvider::InputPayloadType(), 1, std::move(bytes));
}
processes::RegisteredPayload EncodeProcessItemOutputPayload(ProcessItemOutputPayload payload)
{
    std::vector<std::byte> bytes;
    bytes.reserve(24);
    AppendU64(bytes, payload.definition.value.Raw());
    AppendObjectId(bytes, payload.container.value);
    return processes::RegisteredPayload::FromVersioned(ItemProcessOutputHandler::OutputPayloadType(), 1, std::move(bytes));
}

std::optional<equipment::EquipmentItemDescriptor> EquipmentItemsAdapter::Describe(equipment::EquipmentItemId id) const
{
    const items::ItemInstanceId item{id.value};
    const auto* instance = items_.FindItem(item);
    if (!instance) return std::nullopt;
    const auto* definition = items_.FindDefinition(instance->definition);
    if (!definition) return std::nullopt;
    equipment::EquipmentItemDescriptor out;
    out.id = id;
    out.tags = definition->tags;
    out.revision = instance->revision;
    out.available = instance->quantity == 1 && items_.ReservedQuantity(item) == 0;
    return out;
}
foundation::Result<void> EquipmentItemsAdapter::ReserveForEquipment(equipment::EquipmentItemId id,
                                                                    GameplayObjectRef subject, GameplayContext context)
{
    const items::ItemInstanceId item{id.value};
    const auto* instance = items_.FindItem(item);
    if (!instance || instance->quantity != 1)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.equipment_item_invalid", "equipment requires one concrete item instance"));
    auto reserved = items_.ReserveItem(item, 1, subject, TypeId::FromString("equipment.binding"), context);
    if (!reserved) return foundation::Result<void>::Failure(reserved.GetError());
    return foundation::Result<void>::Success();
}
foundation::Result<void> EquipmentItemsAdapter::ReleaseFromEquipment(equipment::EquipmentItemId id,
                                                                     GameplayObjectRef subject, GameplayContext context)
{
    const items::ItemInstanceId item{id.value};
    const auto reason = TypeId::FromString("equipment.binding");
    std::optional<items::ItemReservationId> match;
    for (const auto& reservation : items_.FindReservations(item))
    {
        if (reservation.state != items::ReservationState::Active || reservation.owner != subject || reservation.reason != reason)
            continue;
        if (match.has_value())
            return foundation::Result<void>::Failure(
                Error("gameplay.integration.equipment_reservation_ambiguous", "multiple active reservations back one equipment binding"));
        match = reservation.id;
    }
    if (!match.has_value())
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.equipment_reservation_missing", "equipment binding exists without its backing item reservation"));
    return items_.ReleaseReservation(*match, context);
}
foundation::Result<void> EquipmentItemsAdapter::ReconcileEquipmentReservation(
    equipment::EquipmentItemId id, GameplayObjectRef subject, equipment::EquipmentBindingId, GameplayContext)
{
    const items::ItemInstanceId item{id.value};
    const auto* instance = items_.FindItem(item);
    if (!instance || instance->quantity != 1)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.equipment_item_missing", "restored equipment binding references a missing or non-concrete item"));
    const auto reason = TypeId::FromString("equipment.binding");
    std::size_t matches = 0;
    for (const auto& reservation : items_.FindReservations(item))
        if (reservation.state == items::ReservationState::Active && reservation.owner == subject &&
            reservation.reason == reason && reservation.quantity == 1)
            ++matches;
    if (matches != 1)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.equipment_reservation_mismatch", "restored equipment binding must match exactly one persisted item reservation"));
    return foundation::Result<void>::Success();
}
foundation::Result<void> EquipmentItemsAdapter::ExchangeEquipmentReservations(
    std::span<const equipment::EquipmentItemId> release_items, std::optional<equipment::EquipmentItemId> reserve_item,
    GameplayObjectRef subject, GameplayContext context)
{
    if (!reserve_item.has_value())
    {
        if (release_items.empty()) return foundation::Result<void>::Success();
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.equipment_exchange_invalid", "equipment reservation exchange requires a replacement item"));
    }
    const items::ItemInstanceId replacement{reserve_item->value};
    const auto replacement_item = items_.FindItemCopy(replacement);
    if (!replacement_item || replacement_item->quantity != 1)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.equipment_item_invalid", "equipment requires one concrete replacement item instance"));
    const auto reason = TypeId::FromString("equipment.binding");
    std::vector<items::ItemReservationId> releases;
    releases.reserve(release_items.size());
    for (const auto release : release_items)
    {
        const items::ItemInstanceId item{release.value};
        std::optional<items::ItemReservationId> matching;
        for (const auto& reservation : items_.FindReservations(item))
        {
            if (reservation.state != items::ReservationState::Active || reservation.owner != subject || reservation.reason != reason)
                continue;
            if (matching.has_value())
                return foundation::Result<void>::Failure(
                    Error("gameplay.integration.equipment_reservation_ambiguous", "multiple equipment reservations match one binding"));
            matching = reservation.id;
        }
        if (!matching.has_value())
            return foundation::Result<void>::Failure(
                Error("gameplay.integration.equipment_reservation_missing", "equipment binding reservation is missing"));
        releases.push_back(*matching);
    }
    const auto exchanged = items_.ExchangeReservations(releases, replacement, 1, subject, reason, context);
    if (!exchanged) return foundation::Result<void>::Failure(exchanged.GetError());
    return foundation::Result<void>::Success();
}

foundation::Result<void> ItemProcessInputProvider::Validate(const processes::ProcessInputDefinition& input,
                                                            const processes::StartProcessRequest&, processes::ProcessInstanceId)
{
    const auto payload = DecodeProcessItemInput(input.payload);
    if (!payload || !items_.FindItem(payload->item))
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.item_payload_invalid", "process item input payload references a missing item"));
    return foundation::Result<void>::Success();
}
foundation::Result<processes::ReservedProcessInput> ItemProcessInputProvider::Reserve(
    const processes::ProcessInputDefinition& input, const processes::StartProcessRequest& request,
    processes::ProcessInstanceId instance)
{
    const auto payload = DecodeProcessItemInput(input.payload);
    if (!payload)
        return foundation::Result<processes::ReservedProcessInput>::Failure(
            Error("gameplay.integration.item_payload_invalid", "process item input payload is not valid versioned data"));
    auto reserved = items_.ReserveItem(payload->item, input.amount, request.actor, TypeId::FromString("process.input"), request.context);
    if (!reserved) return foundation::Result<processes::ReservedProcessInput>::Failure(reserved.GetError());
    processes::ReservedProcessInput out;
    out.id = processes::ProcessReservationId::FromRaw(instance.value.High(), input.id.value.Raw());
    out.input = input.id;
    out.type = input.type;
    out.amount = input.amount;
    out.provider_token = EncodeReservationToken({reserved.Value()});
    return foundation::Result<processes::ReservedProcessInput>::Success(std::move(out));
}
foundation::Result<void> ItemProcessInputProvider::Consume(const processes::ReservedProcessInput& reservation, GameplayContext context)
{
    const auto payload = DecodeReservationToken(reservation.provider_token);
    if (!payload)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.item_payload_invalid", "process item reservation token is invalid"));
    return items_.ConsumeReservation(payload->reservation, context);
}
foundation::Result<void> ItemProcessInputProvider::Release(const processes::ReservedProcessInput& reservation, GameplayContext context)
{
    const auto payload = DecodeReservationToken(reservation.provider_token);
    if (!payload)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.item_payload_invalid", "process item reservation token is invalid"));
    const auto* current = items_.FindReservation(payload->reservation);
    if (!current || current->state != items::ReservationState::Active) return foundation::Result<void>::Success();
    return items_.ReleaseReservation(payload->reservation, context);
}
foundation::Result<processes::PreparedProcessOutput> ItemProcessOutputHandler::Prepare(
    const processes::ProcessOutputDefinition& output, const processes::ProcessInstance& instance, GameplayContext)
{
    const auto payload = DecodeProcessItemOutput(output.payload);
    items::ItemLocation target;
    if (payload)
    {
        target.kind = items::ItemLocationKind::Container;
        target.container = payload->container;
    }
    if (!payload || !items_.CanCreateItem(payload->definition, output.amount, target))
        return foundation::Result<processes::PreparedProcessOutput>::Failure(
            Error("gameplay.integration.item_payload_invalid", "process item output definition, policy or destination capacity is invalid"));
    processes::PreparedProcessOutput prepared;
    prepared.output = output.id;
    prepared.type = output.type;
    prepared.delivery = output.delivery;
    prepared.provider_token = EncodePreparedOutputToken(
        {payload->definition, payload->container, ProcessOutputItemId(instance.id, output.id), output.amount});
    return foundation::Result<processes::PreparedProcessOutput>::Success(std::move(prepared));
}
foundation::Result<void> ItemProcessOutputHandler::Commit(const processes::PreparedProcessOutput& output,
                                                          const processes::ProcessInstance&, GameplayContext context)
{
    const auto payload = DecodePreparedOutputToken(output.provider_token);
    if (!payload)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.item_payload_invalid", "prepared process item output token is invalid"));
    if (const auto* existing = items_.FindItem(payload->item))
    {
        if (existing->definition == payload->definition && existing->quantity == payload->amount &&
            existing->location.kind == items::ItemLocationKind::Container && existing->location.container == payload->container)
            return foundation::Result<void>::Success();
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.item_output_id_conflict", "stable process output item id already belongs to different state"));
    }
    items::ItemInstance item;
    item.id = payload->item;
    item.definition = payload->definition;
    item.quantity = payload->amount;
    item.location.kind = items::ItemLocationKind::Container;
    item.location.container = payload->container;
    auto created = items_.CreateItem(std::move(item), context);
    if (!created) return foundation::Result<void>::Failure(created.GetError());
    return foundation::Result<void>::Success();
}
foundation::Result<void> ItemProcessOutputHandler::Cancel(const processes::PreparedProcessOutput&,
                                                          const processes::ProcessInstance&, GameplayContext)
{
    return foundation::Result<void>::Success();
}

dialogue::DialogueConsequenceState DialogueKnowledgeConsequenceHandler::Execute(
    const dialogue::DialogueConsequenceDefinition& definition, const dialogue::DialogueConsequenceExecution& execution,
    const dialogue::ConversationSession& session) const
{
    if (definition.type == ShareType())
    {
        const auto payload = ReadTrivial<DialogueShareKnowledgePayload>(definition.payload);
        if (!payload || payload->speaker_index >= session.participants.size() || payload->listener_index >= session.participants.size())
            return dialogue::DialogueConsequenceState::Failed;
        const auto speaker = session.participants[payload->speaker_index];
        const auto listener = session.participants[payload->listener_index];
        const auto* source = knowledge_.FindKnowledge(payload->record);
        if (!source || source->owner != speaker)
            return dialogue::DialogueConsequenceState::Failed;
        knowledge::ShareKnowledgeRequest request;
        request.speaker = speaker;
        request.listener = listener;
        request.record = payload->record;
        request.mode = payload->mode;
        request.context = execution.context;
        return knowledge_.Share(std::move(request)) ? dialogue::DialogueConsequenceState::Applied
                                                    : dialogue::DialogueConsequenceState::Failed;
    }
    if (definition.type == AssertType())
    {
        const auto payload = ReadTrivial<DialogueAssertKnowledgePayload>(definition.payload);
        if (!payload || payload->speaker_index >= session.participants.size() || payload->listener_index >= session.participants.size())
            return dialogue::DialogueConsequenceState::Failed;
        knowledge::LearnKnowledgeRequest request;
        request.learner = session.participants[payload->listener_index];
        request.type = payload->belief;
        request.topic.id = payload->topic;
        request.topic.primary_subject = payload->subject;
        request.source_kind = knowledge::KnowledgeSourceId::FromString("dialogue.assertion");
        request.source_object = session.participants[payload->speaker_index];
        request.assertion = payload->assertion;
        request.epistemic_state = payload->epistemic_state;
        request.confidence = payload->confidence;
        request.context = execution.context;
        return knowledge_.Learn(std::move(request)) ? dialogue::DialogueConsequenceState::Applied
                                                    : dialogue::DialogueConsequenceState::Failed;
    }
    return dialogue::DialogueConsequenceState::Failed;
}

const CoordinatedTradeExecution* TradeCoordinator::FindExecution(TradeExecutionId id) const noexcept
{
    const auto found = executions_.find(id);
    return found == executions_.end() ? nullptr : &found->second;
}

foundation::Result<TradeExecutionId> TradeCoordinator::Prepare(CoordinatedTradePlan plan)
{
    if (!plan.money.buyer.IsValid() || !plan.money.seller.IsValid() || plan.money.buyer == plan.money.seller)
        return foundation::Result<TradeExecutionId>::Failure(
            Error("gameplay.trade.parties_invalid", "coordinated trade requires distinct buyer and seller"));

    const TradeExecutionId id{execution_ids_.Next()};
    if (!id.IsValid())
        return foundation::Result<TradeExecutionId>::Failure(Error("gameplay.trade.id_exhausted", "trade execution id exhausted"));

    CoordinatedTradeExecution execution;
    execution.id = id;
    execution.buyer = plan.money.buyer;
    execution.seller = plan.money.seller;
    execution.context = plan.money.context;
    execution.goods.reserve(plan.goods.size());

    std::unordered_set<items::ItemInstanceId, items::IdHash> seen_items;
    for (const auto& line : plan.goods)
    {
        if (!SameTradeParty(line.from_owner, plan.money.seller) || !SameTradeParty(line.to_owner, plan.money.buyer))
            return foundation::Result<TradeExecutionId>::Failure(
                Error("gameplay.trade.party_mismatch", "goods owner must be seller and destination owner must be buyer"));
        if (!line.item.IsValid() || !line.destination.IsValid() || !seen_items.insert(line.item).second)
            return foundation::Result<TradeExecutionId>::Failure(
                Error("gameplay.trade.goods_invalid", "trade goods must contain unique valid item instances and destinations"));
        const auto item = items_.FindItemCopy(line.item);
        if (!item || item->quantity <= 0)
            return foundation::Result<TradeExecutionId>::Failure(Error("gameplay.trade.item_missing", "trade item missing"));
        items::ItemLocation target;
        target.kind = items::ItemLocationKind::Container;
        target.container = line.destination;
        if (!items_.CanTransfer(line.item, target, item->quantity))
            return foundation::Result<TradeExecutionId>::Failure(
                Error("gameplay.trade.item_unavailable", "trade item cannot be transferred to destination"));
        const auto* owner = ownership_.GetOwner(ItemPropertyRef(line.item));
        if (!owner || owner->owner != plan.money.seller)
            return foundation::Result<TradeExecutionId>::Failure(
                Error("gameplay.trade.owner_mismatch", "trade item is not owned by seller"));
        CoordinatedTradeGoodsExecution prepared;
        prepared.line = line;
        prepared.quantity = item->quantity;
        prepared.source = item->location;
        prepared.ownership_revision = owner->revision;
        prepared.ownership_domain = owner->domain;
        execution.goods.push_back(std::move(prepared));
    }

    std::size_t reserved_goods = 0;
    for (auto& good : execution.goods)
    {
        auto reservation = items_.ReserveItem(good.line.item, good.quantity, execution.seller,
                                              TypeId::FromString("trade.goods"), execution.context);
        if (!reservation)
        {
            bool rollback_ok = true;
            for (std::size_t i = 0; i < reserved_goods; ++i)
            {
                if (items_.ReleaseReservation(execution.goods[i].reservation, execution.context))
                    execution.goods[i].state = TradeGoodsLegState::Released;
                else
                    rollback_ok = false;
            }
            if (!rollback_ok)
            {
                execution.state = CoordinatedTradeState::ReconciliationRequired;
                executions_.emplace(id, std::move(execution));
                return foundation::Result<TradeExecutionId>::Success(id);
            }
            return foundation::Result<TradeExecutionId>::Failure(reservation.GetError());
        }
        good.reservation = reservation.Value();
        good.state = TradeGoodsLegState::Reserved;
        ++reserved_goods;
    }

    plan.money.id = economy::TradeTransactionId{id.value};
    auto transaction = economy_.PrepareTrade(std::move(plan.money));
    if (!transaction)
    {
        bool rollback_ok = true;
        for (auto& good : execution.goods)
        {
            if (good.state != TradeGoodsLegState::Reserved) continue;
            if (items_.ReleaseReservation(good.reservation, execution.context))
                good.state = TradeGoodsLegState::Released;
            else
                rollback_ok = false;
        }
        if (!rollback_ok)
        {
            execution.state = CoordinatedTradeState::ReconciliationRequired;
            executions_.emplace(id, std::move(execution));
            return foundation::Result<TradeExecutionId>::Success(id);
        }
        return foundation::Result<TradeExecutionId>::Failure(transaction.GetError());
    }
    execution.money_transaction = transaction.Value();
    execution.money_state = TradeMoneyLegState::Prepared;
    auto money_reserved = economy_.ReserveTrade(execution.money_transaction);
    if (!money_reserved)
    {
        const auto money_cancelled = economy_.CancelTrade(execution.money_transaction);
        bool rollback_ok = static_cast<bool>(money_cancelled);
        execution.money_state = money_cancelled ? TradeMoneyLegState::Cancelled
                                                : TradeMoneyLegState::ReconciliationRequired;
        for (auto& good : execution.goods)
        {
            if (good.state != TradeGoodsLegState::Reserved) continue;
            if (items_.ReleaseReservation(good.reservation, execution.context))
                good.state = TradeGoodsLegState::Released;
            else
                rollback_ok = false;
        }
        if (!rollback_ok)
        {
            execution.state = CoordinatedTradeState::ReconciliationRequired;
            executions_.emplace(id, std::move(execution));
            return foundation::Result<TradeExecutionId>::Success(id);
        }
        return foundation::Result<TradeExecutionId>::Failure(money_reserved.GetError());
    }

    execution.money_state = TradeMoneyLegState::Reserved;
    execution.state = CoordinatedTradeState::Prepared;
    executions_.emplace(id, std::move(execution));
    return foundation::Result<TradeExecutionId>::Success(id);
}

foundation::Result<economy::TradeTransactionId> TradeCoordinator::Continue(TradeExecutionId id)
{
    auto found = executions_.find(id);
    if (found == executions_.end())
        return foundation::Result<economy::TradeTransactionId>::Failure(Error("gameplay.trade.execution_missing", "trade execution missing"));
    auto& execution = found->second;
    if (execution.state == CoordinatedTradeState::Completed)
        return foundation::Result<economy::TradeTransactionId>::Success(execution.money_transaction);
    if (!execution.money_transaction.IsValid() || execution.money_state != TradeMoneyLegState::Reserved)
    {
        execution.state = CoordinatedTradeState::ReconciliationRequired;
        return foundation::Result<economy::TradeTransactionId>::Failure(
            Error("gameplay.trade.reconciliation_required", "trade preparation is incomplete and requires reconciliation"));
    }

    if (std::any_of(execution.goods.begin(), execution.goods.end(),
                    [](const auto& good) { return good.state != TradeGoodsLegState::Reserved; }))
    {
        execution.state = CoordinatedTradeState::ReconciliationRequired;
        return foundation::Result<economy::TradeTransactionId>::Failure(
            Error("gameplay.trade.reconciliation_required", "trade goods preparation is incomplete"));
    }
    execution.state = CoordinatedTradeState::Committing;
    for (auto& good : execution.goods)
    {
        if (good.state == TradeGoodsLegState::Reserved)
        {
            const auto* owner = ownership_.GetOwner(ItemPropertyRef(good.line.item), good.ownership_domain);
            if (!owner || owner->owner != execution.seller || owner->revision != good.ownership_revision)
            {
                execution.state = CoordinatedTradeState::ReconciliationRequired;
                return foundation::Result<economy::TradeTransactionId>::Failure(
                    Error("gameplay.trade.reconciliation_required", "trade item ownership changed after preparation"));
            }
            items::ItemLocation target;
            target.kind = items::ItemLocationKind::Container;
            target.container = good.line.destination;
            auto moved = items_.CommitReservedTransfer(good.reservation, target, execution.context);
            if (!moved)
            {
                execution.state = CoordinatedTradeState::ReconciliationRequired;
                return foundation::Result<economy::TradeTransactionId>::Failure(moved.GetError());
            }
            good.state = TradeGoodsLegState::ItemTransferred;
        }

        if (good.state == TradeGoodsLegState::ItemTransferred)
        {
            const auto* owner = ownership_.GetOwner(ItemPropertyRef(good.line.item), good.ownership_domain);
            if (owner && owner->owner == execution.buyer)
            {
                good.state = TradeGoodsLegState::OwnershipTransferred;
                continue;
            }
            ownership::TransferOwnershipRequest request;
            request.property = ItemPropertyRef(good.line.item);
            request.from_owner = execution.seller;
            request.to_owner = execution.buyer;
            request.reason = ownership::TransferReason::Trade;
            request.context = execution.context;
            request.domain = good.ownership_domain;
            request.expected_revision = good.ownership_revision;
            auto transferred = ownership_.TransferOwnership(std::move(request));
            if (!transferred)
            {
                execution.state = CoordinatedTradeState::ReconciliationRequired;
                return foundation::Result<economy::TradeTransactionId>::Failure(transferred.GetError());
            }
            good.state = TradeGoodsLegState::OwnershipTransferred;
        }
    }

    auto money = economy_.CommitTrade(execution.money_transaction);
    if (!money)
    {
        execution.money_state = TradeMoneyLegState::ReconciliationRequired;
        execution.state = CoordinatedTradeState::ReconciliationRequired;
        return foundation::Result<economy::TradeTransactionId>::Failure(money.GetError());
    }
    execution.money_state = TradeMoneyLegState::Committed;
    execution.state = CoordinatedTradeState::Completed;
    return foundation::Result<economy::TradeTransactionId>::Success(execution.money_transaction);
}

foundation::Result<economy::TradeTransactionId> TradeCoordinator::Execute(CoordinatedTradePlan plan)
{
    auto prepared = Prepare(std::move(plan));
    if (!prepared) return foundation::Result<economy::TradeTransactionId>::Failure(prepared.GetError());
    return Continue(prepared.Value());
}

foundation::Result<void> TradeCoordinator::Cancel(TradeExecutionId id)
{
    auto found = executions_.find(id);
    if (found == executions_.end())
        return foundation::Result<void>::Failure(Error("gameplay.trade.execution_missing", "trade execution missing"));
    auto& execution = found->second;
    if (execution.state == CoordinatedTradeState::Cancelled) return foundation::Result<void>::Success();
    if (execution.state == CoordinatedTradeState::Completed)
        return foundation::Result<void>::Failure(Error("gameplay.trade.already_completed", "completed trade cannot be cancelled"));
    if (std::any_of(execution.goods.begin(), execution.goods.end(),
                    [](const auto& good) { return good.state == TradeGoodsLegState::ItemTransferred ||
                                                   good.state == TradeGoodsLegState::OwnershipTransferred; }))
    {
        execution.state = CoordinatedTradeState::ReconciliationRequired;
        return foundation::Result<void>::Failure(
            Error("gameplay.trade.reconciliation_required", "partially committed trade requires explicit reconciliation"));
    }

    bool ok = true;
    for (auto& good : execution.goods)
    {
        const auto* reservation = items_.FindReservation(good.reservation);
        if (good.state == TradeGoodsLegState::Reserved && reservation && reservation->state == items::ReservationState::Active)
        {
            if (items_.ReleaseReservation(good.reservation, execution.context))
                good.state = TradeGoodsLegState::Released;
            else
                ok = false;
        }
    }
    if (execution.money_transaction.IsValid())
    {
        const auto* transaction = economy_.FindTransaction(execution.money_transaction);
        if (transaction)
        {
            if (economy_.CancelTrade(execution.money_transaction))
                execution.money_state = TradeMoneyLegState::Cancelled;
            else
                ok = false;
        }
    }
    if (!ok)
    {
        execution.state = CoordinatedTradeState::ReconciliationRequired;
        return foundation::Result<void>::Failure(
            Error("gameplay.trade.reconciliation_required", "trade cancellation was only partially applied"));
    }
    execution.state = CoordinatedTradeState::Cancelled;
    return foundation::Result<void>::Success();
}

TradeCoordinatorSnapshot TradeCoordinator::CaptureSnapshot() const
{
    TradeCoordinatorSnapshot snapshot;
    snapshot.executions.reserve(executions_.size());
    for (const auto& [id, execution] : executions_)
    {
        (void)id;
        snapshot.executions.push_back(execution);
    }
    std::sort(snapshot.executions.begin(), snapshot.executions.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    snapshot.execution_ids = execution_ids_.GetSnapshot();
    return snapshot;
}

foundation::Result<void> TradeCoordinator::RestoreSnapshot(TradeCoordinatorSnapshot snapshot)
{
    std::unordered_map<TradeExecutionId, CoordinatedTradeExecution, TradeExecutionIdHash> restored;
    std::uint64_t max_low = 0;
    for (auto& execution : snapshot.executions)
    {
        if (!execution.id.IsValid() || !execution.buyer.IsValid() || !execution.seller.IsValid() ||
            execution.buyer == execution.seller || restored.contains(execution.id))
            return foundation::Result<void>::Failure(Error("gameplay.trade.restore_invalid", "invalid trade execution snapshot"));
        if (execution.id.value.High() == execution_ids_.Scope().Raw())
            max_low = std::max(max_low, execution.id.value.Low());

        const auto* transaction = execution.money_transaction.IsValid()
                                      ? economy_.FindTransaction(execution.money_transaction)
                                      : nullptr;
        switch (execution.money_state)
        {
        case TradeMoneyLegState::NotPrepared:
            if (execution.money_transaction.IsValid())
                return foundation::Result<void>::Failure(
                    Error("gameplay.trade.restore_invalid", "unprepared money leg has a transaction"));
            break;
        case TradeMoneyLegState::Prepared:
            if (!transaction || transaction->state != economy::TradeTransactionState::Prepared)
                return foundation::Result<void>::Failure(
                    Error("gameplay.trade.restore_invalid", "prepared money leg is missing"));
            break;
        case TradeMoneyLegState::Reserved:
            if (!transaction || transaction->state != economy::TradeTransactionState::Reserved)
                return foundation::Result<void>::Failure(
                    Error("gameplay.trade.restore_invalid", "reserved money leg is missing"));
            break;
        case TradeMoneyLegState::Committed:
            if (!transaction || transaction->state != economy::TradeTransactionState::Committed)
                return foundation::Result<void>::Failure(
                    Error("gameplay.trade.restore_invalid", "committed money leg is inconsistent"));
            break;
        case TradeMoneyLegState::Cancelled:
            if (execution.money_transaction.IsValid() &&
                (!transaction || transaction->state != economy::TradeTransactionState::Cancelled))
                return foundation::Result<void>::Failure(
                    Error("gameplay.trade.restore_invalid", "cancelled money leg is inconsistent"));
            break;
        case TradeMoneyLegState::ReconciliationRequired:
            if (execution.money_transaction.IsValid() && transaction == nullptr)
                return foundation::Result<void>::Failure(
                    Error("gameplay.trade.restore_invalid", "reconciliation money transaction is missing"));
            break;
        }

        std::unordered_set<items::ItemInstanceId, items::IdHash> seen;
        for (const auto& good : execution.goods)
        {
            if (!good.line.item.IsValid() || !good.line.destination.IsValid() || !seen.insert(good.line.item).second ||
                !SameTradeParty(good.line.from_owner, execution.seller) ||
                !SameTradeParty(good.line.to_owner, execution.buyer) || good.quantity <= 0)
                return foundation::Result<void>::Failure(
                    Error("gameplay.trade.restore_invalid", "invalid trade goods execution"));

            const auto* reservation = good.reservation.IsValid() ? items_.FindReservation(good.reservation) : nullptr;
            const auto item = items_.FindItemCopy(good.line.item);
            if (!item)
                return foundation::Result<void>::Failure(
                    Error("gameplay.trade.restore_invalid", "trade item is missing"));
            switch (good.state)
            {
            case TradeGoodsLegState::NotReserved:
                if (good.reservation.IsValid())
                    return foundation::Result<void>::Failure(
                        Error("gameplay.trade.restore_invalid", "unreserved goods leg has a reservation"));
                break;
            case TradeGoodsLegState::Reserved:
                if (!reservation || reservation->state != items::ReservationState::Active ||
                    reservation->item != good.line.item || reservation->quantity != good.quantity ||
                    reservation->owner != execution.seller || reservation->reason != TypeId::FromString("trade.goods"))
                    return foundation::Result<void>::Failure(
                        Error("gameplay.trade.restore_invalid", "trade goods reservation is missing or mismatched"));
                break;
            case TradeGoodsLegState::Released:
                if (reservation && reservation->state != items::ReservationState::Released)
                    return foundation::Result<void>::Failure(
                        Error("gameplay.trade.restore_invalid", "released goods leg still has an active reservation"));
                break;
            case TradeGoodsLegState::ItemTransferred:
            case TradeGoodsLegState::OwnershipTransferred:
                if (item->location.kind != items::ItemLocationKind::Container ||
                    item->location.container != good.line.destination)
                    return foundation::Result<void>::Failure(
                        Error("gameplay.trade.restore_invalid", "committed trade item is not at its destination"));
                break;
            }

            if (good.state == TradeGoodsLegState::OwnershipTransferred)
            {
                const auto* owner = ownership_.GetOwner(ItemPropertyRef(good.line.item), good.ownership_domain);
                if (!owner || owner->owner != execution.buyer)
                    return foundation::Result<void>::Failure(
                        Error("gameplay.trade.restore_invalid", "committed trade ownership leg is inconsistent"));
            }
        }

        if (execution.state == CoordinatedTradeState::Prepared &&
            (execution.money_state != TradeMoneyLegState::Reserved ||
             std::any_of(execution.goods.begin(), execution.goods.end(),
                         [](const auto& good) { return good.state != TradeGoodsLegState::Reserved; })))
            return foundation::Result<void>::Failure(
                Error("gameplay.trade.restore_invalid", "prepared trade does not have fully reserved legs"));
        if (execution.state == CoordinatedTradeState::Completed &&
            (execution.money_state != TradeMoneyLegState::Committed ||
             std::any_of(execution.goods.begin(), execution.goods.end(),
                         [](const auto& good) { return good.state != TradeGoodsLegState::OwnershipTransferred; })))
            return foundation::Result<void>::Failure(
                Error("gameplay.trade.restore_invalid", "completed trade has incomplete legs"));
        if (execution.state == CoordinatedTradeState::Cancelled &&
            std::any_of(execution.goods.begin(), execution.goods.end(), [](const auto& good) {
                return good.state != TradeGoodsLegState::NotReserved && good.state != TradeGoodsLegState::Released;
            }))
            return foundation::Result<void>::Failure(
                Error("gameplay.trade.restore_invalid", "cancelled trade has live goods legs"));

        restored.emplace(execution.id, std::move(execution));
    }

    const auto generator = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        snapshot.execution_ids, execution_ids_.Scope(), max_low);
    if (!generator)
        return foundation::Result<void>::Failure(
            Error("gameplay.trade.restore_invalid", "trade execution id generator snapshot is invalid"));
    executions_ = std::move(restored);
    execution_ids_.Restore(snapshot.execution_ids);
    return foundation::Result<void>::Success();
}

void TradeCoordinator::PruneCompleted(std::size_t keep_recent)
{
    std::vector<TradeExecutionId> completed;
    for (const auto& [id, execution] : executions_)
        if (execution.state == CoordinatedTradeState::Completed || execution.state == CoordinatedTradeState::Cancelled) completed.push_back(id);
    std::sort(completed.begin(), completed.end());
    if (completed.size() <= keep_recent) return;
    for (std::size_t i = 0; i < completed.size() - keep_recent; ++i) executions_.erase(completed[i]);
}
} // namespace epidemic::gameplay::integration
