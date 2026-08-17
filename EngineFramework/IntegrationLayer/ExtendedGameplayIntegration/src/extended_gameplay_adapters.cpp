#include "Epidemic/GameFramework/ExtendedGameplayIntegration/extended_gameplay_adapters.h"
#include "Epidemic/Foundation/error.h"
#include <cstring>
#include <type_traits>
namespace epidemic::gameplay::integration
{
namespace
{
foundation::Error Error(std::string_view c, std::string_view m)
{
    return foundation::Error::Create(c, m);
}
template <class T> std::optional<T> Read(const std::vector<std::byte> &bytes)
{
    static_assert(std::is_trivially_copyable_v<T>);
    if (bytes.size() != sizeof(T))
        return std::nullopt;
    T out{};
    std::memcpy(&out, bytes.data(), sizeof(T));
    return out;
}
} // namespace
std::optional<equipment::EquipmentItemDescriptor> EquipmentItemsAdapter::Describe(equipment::EquipmentItemId id) const
{
    const items::ItemInstanceId item{id.value};
    const auto *i = items_.FindItem(item);
    if (!i)
        return std::nullopt;
    const auto *d = items_.FindDefinition(i->definition);
    if (!d)
        return std::nullopt;
    equipment::EquipmentItemDescriptor out;
    out.id = id;
    out.tags = d->tags;
    out.revision = i->revision;
    out.available = i->quantity == 1 && items_.ReservedQuantity(item) == 0;
    return out;
}
foundation::Result<void> EquipmentItemsAdapter::ReserveForEquipment(equipment::EquipmentItemId id,
                                                                    GameplayObjectRef subject, GameplayContext context)
{
    items::ItemInstanceId item{id.value};
    const auto *i = items_.FindItem(item);
    if (!i || i->quantity != 1)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.equipment_item_invalid", "equipment requires one concrete item instance"));
    auto r = items_.ReserveItem(item, 1, subject, TypeId::FromString("equipment.binding"), context);
    if (!r)
        return foundation::Result<void>::Failure(r.GetError());
    return foundation::Result<void>::Success();
}
foundation::Result<void> EquipmentItemsAdapter::ReleaseFromEquipment(equipment::EquipmentItemId id,
                                                                     GameplayObjectRef subject, GameplayContext context)
{
    items::ItemInstanceId item{id.value};
    for (const auto &r : items_.FindReservations(item))
        if (r.state == items::ReservationState::Active && r.owner == subject &&
            r.reason == TypeId::FromString("equipment.binding"))
            return items_.ReleaseReservation(r.id, context);
    return foundation::Result<void>::Success();
}
foundation::Result<processes::ReservedProcessInput> ItemProcessInputProvider::Reserve(
    const processes::ProcessInputDefinition &input, const processes::StartProcessRequest &request,
    processes::ProcessInstanceId instance)
{
    auto payload = input.payload.AsTrivial<ProcessItemInputPayload>(InputPayloadType());
    if (!payload)
        return foundation::Result<processes::ReservedProcessInput>::Failure(
            Error("gameplay.integration.item_payload_invalid", "process item input payload invalid"));
    auto reserve = items_.ReserveItem(payload->item, input.amount, request.actor, TypeId::FromString("process.input"),
                                      request.context);
    if (!reserve)
        return foundation::Result<processes::ReservedProcessInput>::Failure(reserve.GetError());
    processes::ReservedProcessInput out;
    out.id = processes::ProcessReservationId::FromRaw(instance.value.High(), input.id.value.Raw());
    out.input = input.id;
    out.type = input.type;
    out.amount = input.amount;
    out.provider_token = processes::RegisteredPayload::FromTrivial(ReservationPayloadType(),
                                                                   ProcessItemReservationPayload{reserve.Value()});
    return foundation::Result<processes::ReservedProcessInput>::Success(std::move(out));
}
foundation::Result<void> ItemProcessInputProvider::Consume(const processes::ReservedProcessInput &r, GameplayContext c)
{
    auto p = r.provider_token.AsTrivial<ProcessItemReservationPayload>(ReservationPayloadType());
    if (!p)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.item_payload_invalid", "process item reservation payload invalid"));
    return items_.ConsumeReservation(p->reservation, c);
}
foundation::Result<void> ItemProcessInputProvider::Release(const processes::ReservedProcessInput &r, GameplayContext c)
{
    auto p = r.provider_token.AsTrivial<ProcessItemReservationPayload>(ReservationPayloadType());
    if (!p)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.item_payload_invalid", "process item reservation payload invalid"));
    const auto *reservation = items_.FindReservation(p->reservation);
    if (!reservation || reservation->state != items::ReservationState::Active)
        return foundation::Result<void>::Success();
    return items_.ReleaseReservation(p->reservation, c);
}
foundation::Result<void> ItemProcessOutputHandler::Produce(const processes::ProcessOutputDefinition &o,
                                                           const processes::ProcessInstance &, GameplayContext c)
{
    auto p = o.payload.AsTrivial<ProcessItemOutputPayload>(OutputPayloadType());
    if (!p)
        return foundation::Result<void>::Failure(
            Error("gameplay.integration.item_payload_invalid", "process item output payload invalid"));
    items::ItemInstance item;
    item.definition = p->definition;
    item.quantity = o.amount;
    item.location.kind = items::ItemLocationKind::Container;
    item.location.container = p->container;
    auto result = items_.CreateItem(std::move(item), c);
    if (!result)
        return foundation::Result<void>::Failure(result.GetError());
    return foundation::Result<void>::Success();
}
dialogue::DialogueConsequenceState DialogueKnowledgeConsequenceHandler::Execute(
    const dialogue::DialogueConsequenceDefinition &d, const dialogue::DialogueConsequenceExecution &e,
    const dialogue::ConversationSession &s) const
{
    auto p = Read<DialogueKnowledgePayload>(d.payload);
    if (!p || p->speaker_index >= s.participants.size() || p->listener_index >= s.participants.size())
        return dialogue::DialogueConsequenceState::Failed;
    knowledge::LearnKnowledgeRequest req;
    req.learner = s.participants[p->listener_index];
    req.type = p->belief;
    req.topic.id = p->topic;
    req.topic.primary_subject = p->subject;
    req.source_kind = knowledge::KnowledgeSourceId::FromString("dialogue.report");
    req.source_object = s.participants[p->speaker_index];
    req.confidence = p->confidence;
    req.context = e.context;
    auto r = knowledge_.Learn(std::move(req));
    return r ? dialogue::DialogueConsequenceState::Applied : dialogue::DialogueConsequenceState::Failed;
}
foundation::Result<economy::TradeTransactionId> TradeCoordinator::Execute(CoordinatedTradePlan plan)
{
    struct PreparedGood
    {
        TradeGoodsLine line;
        items::ItemTransferPlan transfer;
        items::ItemLocation source;
    };
    std::vector<PreparedGood> goods;
    goods.reserve(plan.goods.size());
    for (const auto &line : plan.goods)
    {
        const auto *item = items_.FindItem(line.item);
        if (!item || item->quantity <= 0)
            return foundation::Result<economy::TradeTransactionId>::Failure(
                Error("gameplay.trade.item_missing", "trade item missing"));
        items::ItemLocation target;
        target.kind = items::ItemLocationKind::Container;
        target.container = line.destination;
        auto transfer = items_.PrepareTransfer(line.item, target, item->quantity, plan.money.context);
        if (!transfer)
            return foundation::Result<economy::TradeTransactionId>::Failure(transfer.GetError());
        goods.push_back({line, transfer.Value(), item->location});
    }
    auto tx = economy_.PrepareTrade(std::move(plan.money));
    if (!tx)
        return foundation::Result<economy::TradeTransactionId>::Failure(tx.GetError());
    auto reserve = economy_.ReserveTrade(tx.Value());
    if (!reserve)
    {
        (void)economy_.CancelTrade(tx.Value());
        return foundation::Result<economy::TradeTransactionId>::Failure(reserve.GetError());
    }
    std::size_t moved = 0;
    for (; moved < goods.size(); ++moved)
    {
        auto r = items_.CommitTransfer(goods[moved].transfer);
        if (!r)
            break;
        auto property = ItemPropertyRef(goods[moved].line.item);
        auto own = ownership_.TransferOwnership({property, goods[moved].line.from_owner, goods[moved].line.to_owner,
                                                 ownership::TransferReason::Trade, goods[moved].transfer.context});
        if (!own)
            break;
    }
    if (moved != goods.size())
    {
        for (std::size_t i = 0; i < moved; ++i)
        {
            auto back =
                items_.PrepareTransfer(goods[i].line.item, goods[i].source,
                                       items_.FindItem(goods[i].line.item)->quantity, goods[i].transfer.context);
            if (back)
                (void)items_.CommitTransfer(back.Value());
            (void)ownership_.TransferOwnership({ItemPropertyRef(goods[i].line.item), goods[i].line.to_owner,
                                                goods[i].line.from_owner, ownership::TransferReason::Trade,
                                                goods[i].transfer.context});
        }
        (void)economy_.CancelTrade(tx.Value());
        return foundation::Result<economy::TradeTransactionId>::Failure(
            Error("gameplay.trade.goods_commit_failed", "goods or ownership transfer failed"));
    }
    auto commit = economy_.CommitTrade(tx.Value());
    if (!commit)
    {
        for (auto &g : goods)
        {
            auto back = items_.PrepareTransfer(g.line.item, g.source, items_.FindItem(g.line.item)->quantity,
                                               g.transfer.context);
            if (back)
                (void)items_.CommitTransfer(back.Value());
            (void)ownership_.TransferOwnership({ItemPropertyRef(g.line.item), g.line.to_owner, g.line.from_owner,
                                                ownership::TransferReason::Trade, g.transfer.context});
        }
        (void)economy_.CancelTrade(tx.Value());
        return foundation::Result<economy::TradeTransactionId>::Failure(commit.GetError());
    }
    return foundation::Result<economy::TradeTransactionId>::Success(tx.Value());
}
} // namespace epidemic::gameplay::integration
