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

foundation::Result<void> EquipmentItemsAdapter::ExchangeEquipmentReservations(
    std::span<const equipment::EquipmentItemId> release_items,
    std::optional<equipment::EquipmentItemId> reserve_item,
    GameplayObjectRef subject,
    GameplayContext context)
{
    if (!reserve_item.has_value())
    {
        // Equipment currently uses this operation for swaps; ordinary unequip
        // continues to use ReleaseFromEquipment(). Refuse an ambiguous release-only
        // batch rather than silently performing a non-atomic sequence.
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
            if (reservation.state != items::ReservationState::Active || reservation.owner != subject ||
                reservation.reason != reason)
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
        items::ItemLocation source;
        items::Fixed quantity = 0;
        bool item_moved = false;
        bool ownership_moved = false;
    };

    std::vector<PreparedGood> goods;
    goods.reserve(plan.goods.size());

    // Cross-major preflight. No owner is mutated in this phase.
    for (const auto &line : plan.goods)
    {
        if (!line.from_owner.IsValid() || !line.to_owner.IsValid() || line.from_owner == line.to_owner)
            return foundation::Result<economy::TradeTransactionId>::Failure(
                Error("gameplay.trade.owner_invalid", "trade goods require distinct valid source and destination owners"));

        const auto item = items_.FindItemCopy(line.item);
        if (!item || item->quantity <= 0)
            return foundation::Result<economy::TradeTransactionId>::Failure(
                Error("gameplay.trade.item_missing", "trade item missing"));

        const auto property = ItemPropertyRef(line.item);
        const auto *owner = ownership_.GetOwner(property);
        if (!owner || owner->owner != line.from_owner)
            return foundation::Result<economy::TradeTransactionId>::Failure(
                Error("gameplay.trade.owner_mismatch", "trade goods are not owned by the declared source owner"));

        items::ItemLocation target;
        target.kind = items::ItemLocationKind::Container;
        target.container = line.destination;
        auto transfer = items_.PrepareTransfer(line.item, target, item->quantity, plan.money.context);
        if (!transfer)
            return foundation::Result<economy::TradeTransactionId>::Failure(transfer.GetError());
        goods.push_back({line, item->location, item->quantity, false, false});
    }

    auto tx = economy_.PrepareTrade(std::move(plan.money));
    if (!tx)
        return foundation::Result<economy::TradeTransactionId>::Failure(tx.GetError());

    auto reserve = economy_.ReserveTrade(tx.Value());
    if (!reserve)
    {
        const auto cancelled = economy_.CancelTrade(tx.Value());
        if (!cancelled)
            return foundation::Result<economy::TradeTransactionId>::Failure(
                Error("gameplay.trade.reconciliation_required", "trade reservation failed and the prepared money transaction could not be cancelled"));
        return foundation::Result<economy::TradeTransactionId>::Failure(reserve.GetError());
    }

    auto compensate = [&](std::size_t committed_goods) -> bool {
        bool consistent = true;
        while (committed_goods > 0)
        {
            --committed_goods;
            auto &g = goods[committed_goods];

            // Reverse the forward order: ownership was committed after the item move.
            if (g.ownership_moved)
            {
                const auto ownership_back = ownership_.TransferOwnership(
                    {ItemPropertyRef(g.line.item), g.line.to_owner, g.line.from_owner,
                     ownership::TransferReason::Trade, plan.money.context});
                if (!ownership_back)
                    consistent = false;
                else
                    g.ownership_moved = false;
            }

            if (g.item_moved)
            {
                const auto item_now = items_.FindItemCopy(g.line.item);
                if (!item_now || item_now->quantity < g.quantity)
                {
                    consistent = false;
                }
                else
                {
                    auto back = items_.PrepareTransfer(g.line.item, g.source, g.quantity, plan.money.context);
                    if (!back)
                    {
                        consistent = false;
                    }
                    else
                    {
                        const auto committed_back = items_.CommitTransfer(back.Value());
                        if (!committed_back)
                            consistent = false;
                        else
                            g.item_moved = false;
                    }
                }
            }
        }

        const auto cancelled = economy_.CancelTrade(tx.Value());
        if (!cancelled)
            consistent = false;
        return consistent;
    };

    std::size_t committed_goods = 0;
    for (std::size_t i = 0; i < goods.size(); ++i)
    {
        auto &g = goods[i];
        items::ItemLocation target;
        target.kind = items::ItemLocationKind::Container;
        target.container = g.line.destination;
        auto prepared_transfer = items_.PrepareTransfer(g.line.item, target, g.quantity, plan.money.context);
        if (!prepared_transfer)
        {
            if (!compensate(committed_goods))
                return foundation::Result<economy::TradeTransactionId>::Failure(
                    Error("gameplay.trade.reconciliation_required", "item transfer failed and trade compensation was incomplete"));
            return foundation::Result<economy::TradeTransactionId>::Failure(prepared_transfer.GetError());
        }
        auto moved_item = items_.CommitTransfer(prepared_transfer.Value());
        if (!moved_item)
        {
            if (!compensate(committed_goods))
                return foundation::Result<economy::TradeTransactionId>::Failure(
                    Error("gameplay.trade.reconciliation_required", "item transfer failed and trade compensation was incomplete"));
            return foundation::Result<economy::TradeTransactionId>::Failure(moved_item.GetError());
        }
        g.item_moved = true;
        committed_goods = i + 1;

        auto moved_ownership = ownership_.TransferOwnership(
            {ItemPropertyRef(g.line.item), g.line.from_owner, g.line.to_owner,
             ownership::TransferReason::Trade, plan.money.context});
        if (!moved_ownership)
        {
            if (!compensate(committed_goods))
                return foundation::Result<economy::TradeTransactionId>::Failure(
                    Error("gameplay.trade.reconciliation_required", "ownership transfer failed and trade compensation was incomplete"));
            return foundation::Result<economy::TradeTransactionId>::Failure(moved_ownership.GetError());
        }
        g.ownership_moved = true;
    }

    auto commit = economy_.CommitTrade(tx.Value());
    if (!commit)
    {
        if (!compensate(goods.size()))
            return foundation::Result<economy::TradeTransactionId>::Failure(
                Error("gameplay.trade.reconciliation_required", "money commit failed and trade compensation was incomplete"));
        return foundation::Result<economy::TradeTransactionId>::Failure(commit.GetError());
    }

    return foundation::Result<economy::TradeTransactionId>::Success(tx.Value());
}
} // namespace epidemic::gameplay::integration

