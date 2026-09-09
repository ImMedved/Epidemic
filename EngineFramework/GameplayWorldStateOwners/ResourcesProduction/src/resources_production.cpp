#include "Epidemic/GameFramework/ResourcesProduction/resources_production.h"
#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace epidemic::gameplay::resources
{
namespace
{
[[nodiscard]] foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}

[[nodiscard]] bool CheckedAddFixed(Fixed lhs, Fixed rhs, Fixed &out) noexcept
{
    if ((rhs > 0 && lhs > std::numeric_limits<Fixed>::max() - rhs) ||
        (rhs < 0 && lhs < std::numeric_limits<Fixed>::min() - rhs))
    {
        return false;
    }
    out = lhs + rhs;
    return true;
}

[[nodiscard]] bool CheckedMul(std::int64_t lhs, Fixed rhs, Fixed &out) noexcept
{
#if defined(__SIZEOF_INT128__)
    const __int128 value = static_cast<__int128>(lhs) * static_cast<__int128>(rhs);
    if (value > std::numeric_limits<Fixed>::max() || value < std::numeric_limits<Fixed>::min())
    {
        return false;
    }
    out = static_cast<Fixed>(value);
    return true;
#else
    if (lhs == 0 || rhs == 0)
    {
        out = 0;
        return true;
    }
    if (lhs > 0 && rhs > 0 && lhs > std::numeric_limits<Fixed>::max() / rhs)
        return false;
    if (lhs > 0 && rhs < 0 && rhs < std::numeric_limits<Fixed>::min() / lhs)
        return false;
    if (lhs < 0 && rhs > 0 && lhs < std::numeric_limits<Fixed>::min() / rhs)
        return false;
    if (lhs < 0 && rhs < 0 && lhs < std::numeric_limits<Fixed>::max() / rhs)
        return false;
    out = lhs * rhs;
    return true;
#endif
}

[[nodiscard]] bool IsValidStockpileState(StockpileState state) noexcept
{
    switch (state)
    {
    case StockpileState::Active:
    case StockpileState::Locked:
    case StockpileState::Disabled:
    case StockpileState::Destroyed:
        return true;
    }
    return false;
}

[[nodiscard]] bool IsValidNodeState(ResourceNodeState state) noexcept
{
    switch (state)
    {
    case ResourceNodeState::Active:
    case ResourceNodeState::Depleted:
    case ResourceNodeState::Disabled:
    case ResourceNodeState::Regenerating:
        return true;
    }
    return false;
}

[[nodiscard]] bool IsValidSiteState(ProductionSiteState state) noexcept
{
    switch (state)
    {
    case ProductionSiteState::Active:
    case ProductionSiteState::Paused:
    case ProductionSiteState::Disabled:
    case ProductionSiteState::Destroyed:
        return true;
    }
    return false;
}

[[nodiscard]] bool IsValidReservationState(ResourceReservationState state) noexcept
{
    switch (state)
    {
    case ResourceReservationState::Active:
    case ResourceReservationState::Consumed:
    case ResourceReservationState::Released:
        return true;
    }
    return false;
}

[[nodiscard]] bool IsValidPlanState(ProductionPlanState state) noexcept
{
    switch (state)
    {
    case ProductionPlanState::Planned:
    case ProductionPlanState::Active:
    case ProductionPlanState::Satisfied:
    case ProductionPlanState::Paused:
    case ProductionPlanState::Cancelled:
        return true;
    }
    return false;
}

[[nodiscard]] std::uint64_t ScopeOf(GameplayObjectId id) noexcept
{
    return id.High();
}

template <typename TWrappedId>
void AdvanceGeneratorPastAcceptedId(MonotonicIdGenerator<GameplayObjectId> &generator, TWrappedId id) noexcept
{
    if (!id.IsValid())
    {
        return;
    }
    auto snapshot = generator.GetSnapshot();
    if (ScopeOf(id.value) != snapshot.scope || snapshot.next == 0 || id.value.Low() < snapshot.next)
    {
        return;
    }
    snapshot.next = id.value.Low() == std::numeric_limits<std::uint64_t>::max() ? 0 : id.value.Low() + 1;
    generator.Restore(snapshot);
}

[[nodiscard]] foundation::Result<void> ValidateGenerator(
    MonotonicIdGenerator<GameplayObjectId>::Snapshot snapshot,
    IdScopeId expected_scope,
    std::uint64_t max_low,
    std::string_view label)
{
    const auto validation = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(snapshot, expected_scope, max_low);
    if (!validation)
    {
        (void)label;
        return foundation::Result<void>::Failure(
            Error(validation.Code(), "invalid resources generator snapshot"));
    }
    return foundation::Result<void>::Success();
}

[[nodiscard]] bool SameQuantities(std::span<const ResourceQuantity> a, std::span<const ResourceQuantity> b) noexcept
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].type != b[i].type || a[i].amount != b[i].amount)
        {
            return false;
        }
    }
    return true;
}
} // namespace

ResourcesProductionService::ResourcesProductionService()
    : stockpile_ids_(0x30321001), node_ids_(0x30321002), site_ids_(0x30321003), transaction_ids_(0x30321005)
{
}

foundation::Result<ResourceTypeId> ResourcesProductionService::RegisterResourceType(ResourceType type)
{
    if (frozen_)
        return foundation::Result<ResourceTypeId>::Failure(
            Error("gameplay.resources.registry_frozen", "resource registry is frozen"));
    if (type.canonical_name.empty())
        return foundation::Result<ResourceTypeId>::Failure(
            Error("gameplay.resources.invalid_type", "resource type name required"));
    const auto canonical = ResourceTypeId::FromString(type.canonical_name);
    if (!type.id.IsValid())
        type.id = canonical;
    if (type.id != canonical || types_.contains(type.id))
        return foundation::Result<ResourceTypeId>::Failure(
            Error("gameplay.resources.invalid_type", "invalid or duplicate resource type"));
    Bump();
    type.revision = revision_;
    const auto id = type.id;
    types_.emplace(id, std::move(type));
    return foundation::Result<ResourceTypeId>::Success(id);
}

foundation::Result<ProductionRecipeId> ResourcesProductionService::RegisterProductionRecipe(ProductionRecipe recipe)
{
    if (frozen_)
        return foundation::Result<ProductionRecipeId>::Failure(
            Error("gameplay.resources.registry_frozen", "resource registry is frozen"));
    if (recipe.canonical_name.empty())
        return foundation::Result<ProductionRecipeId>::Failure(
            Error("gameplay.resources.invalid_recipe", "production recipe name required"));
    const auto canonical = ProductionRecipeId::FromString(recipe.canonical_name);
    if (!recipe.id.IsValid())
        recipe.id = canonical;
    if (recipe.id != canonical || recipes_.contains(recipe.id))
        return foundation::Result<ProductionRecipeId>::Failure(
            Error("gameplay.resources.invalid_recipe", "invalid or duplicate production recipe"));
    if (recipe.duration.ticks < 0)
        return foundation::Result<ProductionRecipeId>::Failure(
            Error("gameplay.resources.invalid_recipe", "production recipe duration cannot be negative"));

    auto canonical_inputs = CanonicalizeQuantities(recipe.inputs);
    if (!canonical_inputs)
        return foundation::Result<ProductionRecipeId>::Failure(canonical_inputs.GetError());
    auto canonical_outputs = CanonicalizeQuantities(recipe.outputs);
    if (!canonical_outputs)
        return foundation::Result<ProductionRecipeId>::Failure(canonical_outputs.GetError());
    recipe.inputs = std::move(canonical_inputs.Value());
    recipe.outputs = std::move(canonical_outputs.Value());

    Bump();
    recipe.revision = revision_;
    const auto id = recipe.id;
    recipes_.emplace(id, std::move(recipe));
    return foundation::Result<ProductionRecipeId>::Success(id);
}

const ResourceType *ResourcesProductionService::FindResourceType(ResourceTypeId id) const noexcept
{
    const auto it = types_.find(id);
    return it == types_.end() ? nullptr : &it->second;
}

const ProductionRecipe *ResourcesProductionService::FindProductionRecipe(ProductionRecipeId id) const noexcept
{
    const auto it = recipes_.find(id);
    return it == recipes_.end() ? nullptr : &it->second;
}

foundation::Result<void> ResourcesProductionService::ValidateQuantities(
    std::span<const ResourceQuantity> quantities) const
{
    auto canonical = CanonicalizeQuantities(quantities);
    if (!canonical)
        return foundation::Result<void>::Failure(canonical.GetError());
    return foundation::Result<void>::Success();
}

foundation::Result<std::vector<ResourceQuantity>> ResourcesProductionService::CanonicalizeQuantities(
    std::span<const ResourceQuantity> quantities) const
{
    std::vector<ResourceQuantity> out;
    out.reserve(quantities.size());
    for (const auto &q : quantities)
    {
        if (!q.type.IsValid() || !types_.contains(q.type) || q.amount <= 0)
        {
            return foundation::Result<std::vector<ResourceQuantity>>::Failure(
                Error("gameplay.resources.invalid_quantity", "invalid resource quantity"));
        }
        out.push_back(q);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.type < b.type; });

    std::vector<ResourceQuantity> canonical;
    canonical.reserve(out.size());
    for (const auto &q : out)
    {
        if (!canonical.empty() && canonical.back().type == q.type)
        {
            Fixed sum = 0;
            if (!CheckedAddFixed(canonical.back().amount, q.amount, sum))
            {
                return foundation::Result<std::vector<ResourceQuantity>>::Failure(
                    Error("gameplay.resources.quantity_overflow", "resource quantity overflow"));
            }
            canonical.back().amount = sum;
        }
        else
        {
            canonical.push_back(q);
        }
    }
    return foundation::Result<std::vector<ResourceQuantity>>::Success(std::move(canonical));
}

foundation::Result<void> ResourcesProductionService::ValidateStockpileOperation(ResourceStockpileId stockpile,
                                                                                 StockpileOperation operation) const
{
    const auto it = stockpiles_.find(stockpile);
    if (it == stockpiles_.end())
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.resources.stockpile_missing", "stockpile missing"));
    }
    if (it->second.state != StockpileState::Active)
    {
        (void)operation;
        return foundation::Result<void>::Failure(
            Error("gameplay.resources.stockpile_unavailable", "stockpile is not operational"));
    }
    return foundation::Result<void>::Success();
}

bool ResourcesProductionService::CanAddToStockpile(ResourceStockpileId stockpile) const noexcept
{
    const auto it = stockpiles_.find(stockpile);
    return it != stockpiles_.end() && it->second.state == StockpileState::Active;
}

bool ResourcesProductionService::CanRemoveFromStockpile(ResourceStockpileId stockpile) const noexcept
{
    const auto it = stockpiles_.find(stockpile);
    return it != stockpiles_.end() && it->second.state == StockpileState::Active;
}

std::uint64_t ResourcesProductionService::LowPart(GameplayObjectId id) noexcept
{
    return id.IsValid() ? id.Low() : 0;
}

void ResourcesProductionService::AddReservedIndex(ResourceStockpileId stockpile, ResourceTypeId type, Fixed amount) noexcept
{
    auto &reserved = reserved_amounts_[{stockpile, type}];
    Fixed next = 0;
    if (CheckedAddFixed(reserved, amount, next))
    {
        reserved = next;
    }
    else
    {
        reserved = std::numeric_limits<Fixed>::max();
    }
}

void ResourcesProductionService::RemoveReservedIndex(ResourceStockpileId stockpile, ResourceTypeId type, Fixed amount) noexcept
{
    auto key = AmountKey{stockpile, type};
    auto it = reserved_amounts_.find(key);
    if (it == reserved_amounts_.end())
    {
        return;
    }
    if (it->second <= amount)
    {
        reserved_amounts_.erase(it);
        return;
    }
    it->second -= amount;
}

foundation::Result<ResourceStockpileId> ResourcesProductionService::CreateStockpile(ResourceStockpile stockpile)
{
    if (!stockpile.owner.IsValid() || !IsValidStockpileState(stockpile.state))
        return foundation::Result<ResourceStockpileId>::Failure(
            Error("gameplay.resources.invalid_stockpile", "invalid stockpile"));
    if (!stockpile.id.IsValid())
    {
        stockpile.id = ResourceStockpileId{stockpile_ids_.Next()};
        if (!stockpile.id.IsValid())
        {
            return foundation::Result<ResourceStockpileId>::Failure(
                Error("gameplay.resources.id_exhausted", "stockpile id generator exhausted"));
        }
    }
    if (stockpiles_.contains(stockpile.id))
        return foundation::Result<ResourceStockpileId>::Failure(
            Error("gameplay.resources.invalid_stockpile", "duplicate stockpile"));
    AdvanceGeneratorPastAcceptedId(stockpile_ids_, stockpile.id);
    Bump();
    stockpile.revision = revision_;
    const auto id = stockpile.id;
    stockpiles_.emplace(id, std::move(stockpile));
    Record({0, ResourceChangeKind::StockpileCreated, id, {}, {}, 0, {}, {}, revision_});
    return foundation::Result<ResourceStockpileId>::Success(id);
}

foundation::Result<ResourceNodeId> ResourcesProductionService::CreateNode(ResourceNode node)
{
    if (!types_.contains(node.type) || node.remaining_amount < 0 || node.regeneration_rate_per_tick < 0 ||
        !IsValidNodeState(node.state))
        return foundation::Result<ResourceNodeId>::Failure(
            Error("gameplay.resources.invalid_node", "invalid resource node"));
    if (node.maximum_amount <= 0)
    {
        node.maximum_amount = node.remaining_amount;
    }
    if (node.remaining_amount > node.maximum_amount)
    {
        return foundation::Result<ResourceNodeId>::Failure(
            Error("gameplay.resources.invalid_node", "resource node exceeds capacity"));
    }
    if (node.remaining_amount == 0 && node.state == ResourceNodeState::Active)
    {
        node.state = ResourceNodeState::Depleted;
    }
    if (!node.id.IsValid())
    {
        node.id = ResourceNodeId{node_ids_.Next()};
        if (!node.id.IsValid())
        {
            return foundation::Result<ResourceNodeId>::Failure(
                Error("gameplay.resources.id_exhausted", "node id generator exhausted"));
        }
    }
    if (nodes_.contains(node.id))
        return foundation::Result<ResourceNodeId>::Failure(
            Error("gameplay.resources.invalid_node", "duplicate resource node"));
    AdvanceGeneratorPastAcceptedId(node_ids_, node.id);
    Bump();
    node.revision = revision_;
    const auto id = node.id;
    nodes_.emplace(id, std::move(node));
    ResourceChange change{};
    change.kind = ResourceChangeKind::NodeCreated;
    change.node = id;
    change.revision = revision_;
    Record(std::move(change));
    return foundation::Result<ResourceNodeId>::Success(id);
}

foundation::Result<ProductionSiteId> ResourcesProductionService::CreateProductionSite(ProductionSite site)
{
    if (!site.site_object.IsValid() || site.efficiency_micro < 0 || !IsValidSiteState(site.state))
        return foundation::Result<ProductionSiteId>::Failure(
            Error("gameplay.resources.invalid_site", "invalid production site"));
    if (!site.id.IsValid())
    {
        site.id = ProductionSiteId{site_ids_.Next()};
        if (!site.id.IsValid())
        {
            return foundation::Result<ProductionSiteId>::Failure(
                Error("gameplay.resources.id_exhausted", "site id generator exhausted"));
        }
    }
    if (sites_.contains(site.id))
        return foundation::Result<ProductionSiteId>::Failure(
            Error("gameplay.resources.invalid_site", "duplicate production site"));
    AdvanceGeneratorPastAcceptedId(site_ids_, site.id);
    Bump();
    site.revision = revision_;
    const auto id = site.id;
    sites_.emplace(id, std::move(site));
    Record({0, ResourceChangeKind::ProductionSiteCreated, {}, {}, {}, 0, {}, {}, revision_});
    return foundation::Result<ProductionSiteId>::Success(id);
}

const ResourceStockpile *ResourcesProductionService::FindStockpile(ResourceStockpileId id) const noexcept
{
    const auto it = stockpiles_.find(id);
    return it == stockpiles_.end() ? nullptr : &it->second;
}

foundation::Result<void> ResourcesProductionService::SetStockpileState(ResourceStockpileId id, StockpileState state,
                                                                        GameplayContext context)
{
    auto it = stockpiles_.find(id);
    if (it == stockpiles_.end() || !IsValidStockpileState(state))
        return foundation::Result<void>::Failure(Error("gameplay.resources.stockpile_missing", "stockpile missing"));
    if (it->second.state == state)
        return foundation::Result<void>::Success();
    if (it->second.state == StockpileState::Destroyed)
        return foundation::Result<void>::Failure(
            Error("gameplay.resources.stockpile_terminal", "destroyed stockpile cannot transition"));
    if (state == StockpileState::Destroyed)
    {
        for (const auto &[rid, reservation] : reservations_)
        {
            (void)rid;
            if (reservation.stockpile == id && reservation.state == ResourceReservationState::Active)
            {
                return foundation::Result<void>::Failure(
                    Error("gameplay.resources.active_reservations", "cannot destroy stockpile with active reservations"));
            }
        }
    }
    Bump();
    it->second.state = state;
    it->second.revision = revision_;
    Record({0, ResourceChangeKind::StockpileStateChanged, id, {}, {}, 0, context.time, context, revision_});
    return foundation::Result<void>::Success();
}

Fixed ResourcesProductionService::GetAmount(ResourceStockpileId stockpile, ResourceTypeId type) const noexcept
{
    const auto it = amounts_.find({stockpile, type});
    return it == amounts_.end() ? 0 : it->second;
}

Fixed ResourcesProductionService::GetReservedAmount(ResourceStockpileId stockpile, ResourceTypeId type) const noexcept
{
    const auto it = reserved_amounts_.find({stockpile, type});
    return it == reserved_amounts_.end() ? 0 : it->second;
}

Fixed ResourcesProductionService::GetAvailableAmount(ResourceStockpileId stockpile, ResourceTypeId type) const noexcept
{
    const auto amount = GetAmount(stockpile, type);
    const auto reserved = GetReservedAmount(stockpile, type);
    return reserved >= amount ? 0 : amount - reserved;
}

foundation::Result<void> ResourcesProductionService::Add(ResourceStockpileId stockpile, ResourceQuantity quantity,
                                                         GameplayContext context)
{
    auto operation = ValidateStockpileOperation(stockpile, StockpileOperation::Add);
    if (!operation)
        return operation;
    auto canonical = CanonicalizeQuantities(std::span<const ResourceQuantity>(&quantity, 1));
    if (!canonical)
        return foundation::Result<void>::Failure(canonical.GetError());

    const auto &q = canonical.Value().front();
    auto key = AmountKey{stockpile, q.type};
    Fixed next = 0;
    if (!CheckedAddFixed(GetAmount(stockpile, q.type), q.amount, next))
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.resources.amount_overflow", "resource amount overflow"));
    }
    Bump();
    amounts_[key] = next;
    Record({0, ResourceChangeKind::ResourceAdded, stockpile, q.type, {}, q.amount, context.time, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ResourcesProductionService::Remove(ResourceStockpileId stockpile, ResourceQuantity quantity,
                                                            GameplayContext context)
{
    auto operation = ValidateStockpileOperation(stockpile, StockpileOperation::Remove);
    if (!operation)
        return operation;
    auto canonical = CanonicalizeQuantities(std::span<const ResourceQuantity>(&quantity, 1));
    if (!canonical)
        return foundation::Result<void>::Failure(canonical.GetError());

    const auto &q = canonical.Value().front();
    const auto available = GetAvailableAmount(stockpile, q.type);
    if (available < q.amount)
    {
        ++diagnostics_.shortages;
        Record({0, ResourceChangeKind::ShortageDetected, stockpile, q.type, {}, q.amount - available, context.time,
                context, revision_});
        return foundation::Result<void>::Failure(
            Error("gameplay.resources.shortage", "insufficient unreserved resource amount"));
    }
    Bump();
    auto key = AmountKey{stockpile, q.type};
    auto &amount = amounts_[key];
    amount -= q.amount;
    if (amount == 0)
    {
        amounts_.erase(key);
    }
    Record({0, ResourceChangeKind::ResourceRemoved, stockpile, q.type, {}, q.amount, context.time, context, revision_});
    return foundation::Result<void>::Success();
}

bool ResourcesProductionService::CanReserve(ResourceStockpileId stockpile,
                                            std::span<const ResourceQuantity> quantities) const
{
    if (ValidateStockpileOperation(stockpile, StockpileOperation::Reserve))
    {
        auto canonical = CanonicalizeQuantities(quantities);
        if (!canonical)
            return false;
        for (const auto &q : canonical.Value())
        {
            if (GetAvailableAmount(stockpile, q.type) < q.amount)
                return false;
        }
        return true;
    }
    return false;
}

foundation::Result<ResourceReservationId> ResourcesProductionService::Reserve(ResourceStockpileId stockpile,
                                                                              std::vector<ResourceQuantity> quantities,
                                                                              GameplayObjectRef owner, TypeId reason,
                                                                              GameplayContext context)
{
    auto operation = ValidateStockpileOperation(stockpile, StockpileOperation::Reserve);
    if (!operation)
        return foundation::Result<ResourceReservationId>::Failure(operation.GetError());
    auto canonical = CanonicalizeQuantities(quantities);
    if (!canonical)
        return foundation::Result<ResourceReservationId>::Failure(canonical.GetError());
    for (const auto &q : canonical.Value())
    {
        if (GetAvailableAmount(stockpile, q.type) < q.amount)
        {
            ++diagnostics_.shortages;
            Record({0, ResourceChangeKind::ShortageDetected, stockpile, q.type, {},
                    q.amount - GetAvailableAmount(stockpile, q.type), context.time, context, revision_});
            return foundation::Result<ResourceReservationId>::Failure(
                Error("gameplay.resources.shortage", "resources unavailable for reservation"));
        }
    }

    ResourceReservation reservation;
    reservation.id = ResourceReservationId{reservation_ids_.Next()};
    if (!reservation.id.IsValid())
        return foundation::Result<ResourceReservationId>::Failure(
            Error("gameplay.resources.id_exhausted", "reservation id generator exhausted"));
    reservation.stockpile = stockpile;
    reservation.quantities = std::move(canonical.Value());
    reservation.owner = owner;
    reservation.reason = reason;
    Bump();
    reservation.revision = revision_;
    const auto id = reservation.id;
    reservations_.emplace(id, reservation);
    for (const auto &q : reservation.quantities)
    {
        AddReservedIndex(stockpile, q.type, q.amount);
        Record({0, ResourceChangeKind::ResourceReserved, stockpile, q.type, {}, q.amount, context.time, context,
                revision_});
    }
    return foundation::Result<ResourceReservationId>::Success(id);
}

const ResourceReservation *ResourcesProductionService::FindReservation(ResourceReservationId id) const noexcept
{
    const auto it = reservations_.find(id);
    return it == reservations_.end() ? nullptr : &it->second;
}

foundation::Result<void> ResourcesProductionService::ReleaseReservation(ResourceReservationId id,
                                                                        GameplayContext context)
{
    auto it = reservations_.find(id);
    if (it == reservations_.end() || it->second.state != ResourceReservationState::Active)
        return foundation::Result<void>::Failure(
            Error("gameplay.resources.reservation_missing", "active resource reservation missing"));
    const auto reservation = it->second;
    Bump();
    for (const auto &q : reservation.quantities)
    {
        RemoveReservedIndex(reservation.stockpile, q.type, q.amount);
        Record({0, ResourceChangeKind::ResourceReservationReleased, reservation.stockpile, q.type, {}, q.amount,
                context.time, context, revision_});
    }
    reservations_.erase(it);
    return foundation::Result<void>::Success();
}

foundation::Result<void> ResourcesProductionService::ConsumeReservation(ResourceReservationId id,
                                                                        GameplayContext context)
{
    auto it = reservations_.find(id);
    if (it == reservations_.end() || it->second.state != ResourceReservationState::Active)
        return foundation::Result<void>::Failure(
            Error("gameplay.resources.reservation_missing", "active resource reservation missing"));
    auto operation = ValidateStockpileOperation(it->second.stockpile, StockpileOperation::Remove);
    if (!operation)
        return operation;
    const auto reservation = it->second;
    for (const auto &q : reservation.quantities)
    {
        if (GetAmount(reservation.stockpile, q.type) < q.amount)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.resources.reservation_invalid", "reserved resource amount is no longer available"));
        }
    }

    Bump();
    for (const auto &q : reservation.quantities)
    {
        auto key = AmountKey{reservation.stockpile, q.type};
        auto &amount = amounts_[key];
        amount -= q.amount;
        if (amount == 0)
        {
            amounts_.erase(key);
        }
        RemoveReservedIndex(reservation.stockpile, q.type, q.amount);
        Record({0, ResourceChangeKind::ResourceReservationConsumed, reservation.stockpile, q.type, {}, q.amount,
                context.time, context, revision_});
    }
    reservations_.erase(it);
    return foundation::Result<void>::Success();
}

foundation::Result<ResourceTransactionId> ResourcesProductionService::Transfer(ResourceStockpileId from,
                                                                               ResourceStockpileId to,
                                                                               std::vector<ResourceQuantity> quantities,
                                                                               TypeId reason, GameplayContext context)
{
    if (from == to)
    {
        return foundation::Result<ResourceTransactionId>::Failure(
            Error("gameplay.resources.invalid_transfer", "source and destination stockpiles must differ"));
    }
    auto source_operation = ValidateStockpileOperation(from, StockpileOperation::TransferSource);
    if (!source_operation)
        return foundation::Result<ResourceTransactionId>::Failure(source_operation.GetError());
    auto destination_operation = ValidateStockpileOperation(to, StockpileOperation::TransferDestination);
    if (!destination_operation)
        return foundation::Result<ResourceTransactionId>::Failure(destination_operation.GetError());
    auto canonical = CanonicalizeQuantities(quantities);
    if (!canonical)
        return foundation::Result<ResourceTransactionId>::Failure(canonical.GetError());

    for (const auto &q : canonical.Value())
    {
        const auto available = GetAvailableAmount(from, q.type);
        if (available < q.amount)
        {
            ++diagnostics_.shortages;
            Record({0, ResourceChangeKind::ShortageDetected, from, q.type, {}, q.amount - available, context.time,
                    context, revision_});
            return foundation::Result<ResourceTransactionId>::Failure(
                Error("gameplay.resources.shortage", "insufficient resources for transfer"));
        }
        Fixed destination_next = 0;
        if (!CheckedAddFixed(GetAmount(to, q.type), q.amount, destination_next))
        {
            return foundation::Result<ResourceTransactionId>::Failure(
                Error("gameplay.resources.amount_overflow", "destination resource amount overflow"));
        }
    }

    const auto id = ResourceTransactionId{transaction_ids_.Next()};
    if (!id.IsValid())
        return foundation::Result<ResourceTransactionId>::Failure(
            Error("gameplay.resources.id_exhausted", "transaction id generator exhausted"));

    Bump();
    for (const auto &q : canonical.Value())
    {
        auto from_key = AmountKey{from, q.type};
        auto to_key = AmountKey{to, q.type};
        auto &from_amount = amounts_[from_key];
        from_amount -= q.amount;
        if (from_amount == 0)
        {
            amounts_.erase(from_key);
        }
        amounts_[to_key] += q.amount;
        Record({0, ResourceChangeKind::ResourceTransferred, to, q.type, {}, q.amount, context.time, context,
                revision_});
    }
    ResourceTransaction transaction;
    transaction.id = id;
    transaction.from = from;
    transaction.to = to;
    transaction.quantities = std::move(canonical.Value());
    transaction.reason = reason;
    transaction.time = context.time;
    transaction.context = context;
    transaction.revision = revision_;
    transactions_.emplace(id, std::move(transaction));
    while (transactions_.size() > change_retention_)
    {
        auto oldest = transactions_.begin();
        for (auto it = transactions_.begin(); it != transactions_.end(); ++it)
        {
            if (it->first < oldest->first)
                oldest = it;
        }
        transactions_.erase(oldest);
    }
    ++diagnostics_.transactions;
    return foundation::Result<ResourceTransactionId>::Success(id);
}

foundation::Result<void> ResourcesProductionService::DepleteNode(ResourceNodeId id, Fixed amount,
                                                                 GameplayContext context)
{
    auto it = nodes_.find(id);
    if (it == nodes_.end() || amount < 0)
        return foundation::Result<void>::Failure(Error("gameplay.resources.node_missing", "resource node missing"));
    if (it->second.state == ResourceNodeState::Disabled)
        return foundation::Result<void>::Failure(Error("gameplay.resources.node_disabled", "resource node disabled"));
    if (amount == 0)
        return foundation::Result<void>::Success();
    Bump();
    it->second.remaining_amount = amount >= it->second.remaining_amount ? 0 : it->second.remaining_amount - amount;
    it->second.state = it->second.remaining_amount == 0 ? ResourceNodeState::Depleted : ResourceNodeState::Active;
    it->second.revision = revision_;
    if (it->second.remaining_amount == 0)
    {
        Record({0, ResourceChangeKind::NodeDepleted, {}, it->second.type, {}, 0, context.time, context, revision_});
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> ResourcesProductionService::RegenerateNode(ResourceNodeId id, GameplayDuration elapsed,
                                                                    GameplayContext context)
{
    auto it = nodes_.find(id);
    if (it == nodes_.end())
        return foundation::Result<void>::Failure(Error("gameplay.resources.node_missing", "resource node missing"));
    if (it->second.state == ResourceNodeState::Disabled)
        return foundation::Result<void>::Failure(Error("gameplay.resources.node_disabled", "resource node disabled"));
    if (elapsed.ticks <= 0 || it->second.regeneration_rate_per_tick <= 0 || it->second.maximum_amount <= 0)
        return foundation::Result<void>::Success();
    Fixed produced = 0;
    if (!CheckedMul(elapsed.ticks, it->second.regeneration_rate_per_tick, produced))
    {
        produced = std::numeric_limits<Fixed>::max();
    }
    const auto before = it->second.remaining_amount;
    if (produced >= it->second.maximum_amount - std::min(it->second.remaining_amount, it->second.maximum_amount))
    {
        it->second.remaining_amount = it->second.maximum_amount;
    }
    else
    {
        it->second.remaining_amount += produced;
    }
    if (it->second.remaining_amount == before)
        return foundation::Result<void>::Success();
    Bump();
    it->second.state = it->second.remaining_amount == 0 ? ResourceNodeState::Depleted : ResourceNodeState::Active;
    it->second.revision = revision_;
    Record({0, ResourceChangeKind::NodeRegenerated, {}, it->second.type, {}, it->second.remaining_amount, context.time,
            context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ResourcesProductionService::RegenerateNode(ResourceNodeId id, GameplayTimePoint to,
                                                                    GameplayContext context)
{
    auto it = nodes_.find(id);
    if (it == nodes_.end())
        return foundation::Result<void>::Failure(Error("gameplay.resources.node_missing", "resource node missing"));
    if (it->second.state == ResourceNodeState::Disabled)
        return foundation::Result<void>::Failure(Error("gameplay.resources.node_disabled", "resource node disabled"));
    if (to.ticks < it->second.last_regenerated_at.ticks)
        return foundation::Result<void>::Failure(
            Error("gameplay.resources.regeneration_interval_conflict", "node regeneration interval moved backward"));
    if (to.ticks == it->second.last_regenerated_at.ticks)
        return foundation::Result<void>::Success();

    const auto elapsed = CheckedDifference(to, it->second.last_regenerated_at);
    if (!elapsed.has_value())
        return foundation::Result<void>::Failure(
            Error("gameplay.time_overflow", "resource node regeneration interval overflows gameplay time"));
    Fixed produced = 0;
    if (elapsed->ticks > 0 && it->second.regeneration_rate_per_tick > 0 && it->second.maximum_amount > 0)
    {
        if (!CheckedMul(elapsed->ticks, it->second.regeneration_rate_per_tick, produced))
        {
            produced = std::numeric_limits<Fixed>::max();
        }
    }
    Bump();
    if (produced > 0)
    {
        if (produced >= it->second.maximum_amount - std::min(it->second.remaining_amount, it->second.maximum_amount))
        {
            it->second.remaining_amount = it->second.maximum_amount;
        }
        else
        {
            it->second.remaining_amount += produced;
        }
        it->second.state = it->second.remaining_amount == 0 ? ResourceNodeState::Depleted : ResourceNodeState::Active;
        Record({0, ResourceChangeKind::NodeRegenerated, {}, it->second.type, {}, it->second.remaining_amount,
                context.time, context, revision_});
    }
    it->second.last_regenerated_at = to;
    it->second.revision = revision_;
    return foundation::Result<void>::Success();
}

foundation::Result<ProductionCapabilityId> ResourcesProductionService::CreateProductionCapability(
    ProductionCapability capability)
{
    if (!capability.site.IsValid() || capability.capacity < 0)
        return foundation::Result<ProductionCapabilityId>::Failure(
            Error("gameplay.resources.invalid_capability", "invalid production capability"));
    if (!capability.id.IsValid())
    {
        capability.id = ProductionCapabilityId{capability_ids_.Next()};
        if (!capability.id.IsValid())
        {
            return foundation::Result<ProductionCapabilityId>::Failure(
                Error("gameplay.resources.id_exhausted", "capability id generator exhausted"));
        }
    }
    if (capabilities_.contains(capability.id))
        return foundation::Result<ProductionCapabilityId>::Failure(
            Error("gameplay.resources.duplicate_capability", "duplicate production capability"));
    AdvanceGeneratorPastAcceptedId(capability_ids_, capability.id);
    Bump();
    capability.revision = revision_;
    const auto id = capability.id;
    capabilities_.emplace(id, std::move(capability));
    Record({0, ResourceChangeKind::ProductionCapabilityCreated, {}, {}, {}, 0, {}, {}, revision_});
    return foundation::Result<ProductionCapabilityId>::Success(id);
}

const ProductionCapability *ResourcesProductionService::FindProductionCapability(
    ProductionCapabilityId id) const noexcept
{
    const auto it = capabilities_.find(id);
    return it == capabilities_.end() ? nullptr : &it->second;
}

foundation::Result<ProductionPlanId> ResourcesProductionService::CreateProductionPlan(ProductionPlan plan)
{
    if (!plan.owner.IsValid() || !types_.contains(plan.desired_output) || plan.target_quantity <= 0 ||
        !IsValidPlanState(plan.state))
        return foundation::Result<ProductionPlanId>::Failure(
            Error("gameplay.resources.invalid_plan", "invalid production plan"));
    if (!plan.id.IsValid())
    {
        plan.id = ProductionPlanId{plan_ids_.Next()};
        if (!plan.id.IsValid())
        {
            return foundation::Result<ProductionPlanId>::Failure(
                Error("gameplay.resources.id_exhausted", "plan id generator exhausted"));
        }
    }
    if (plans_.contains(plan.id))
        return foundation::Result<ProductionPlanId>::Failure(
            Error("gameplay.resources.duplicate_plan", "duplicate production plan"));
    AdvanceGeneratorPastAcceptedId(plan_ids_, plan.id);
    Bump();
    plan.revision = revision_;
    const auto id = plan.id;
    plans_.emplace(id, std::move(plan));
    Record({0, ResourceChangeKind::ProductionPlanCreated, {}, {}, {}, 0, {}, {}, revision_});
    return foundation::Result<ProductionPlanId>::Success(id);
}

foundation::Result<void> ResourcesProductionService::SetProductionPlanState(ProductionPlanId id,
                                                                            ProductionPlanState state,
                                                                            GameplayContext context)
{
    auto it = plans_.find(id);
    if (it == plans_.end() || !IsValidPlanState(state))
        return foundation::Result<void>::Failure(Error("gameplay.resources.plan_missing", "production plan missing"));
    if (it->second.state == state)
        return foundation::Result<void>::Success();
    Bump();
    it->second.state = state;
    it->second.revision = revision_;
    Record({0, ResourceChangeKind::ProductionPlanChanged, {}, it->second.desired_output, {},
            it->second.target_quantity, context.time, context, revision_});
    return foundation::Result<void>::Success();
}

const ProductionPlan *ResourcesProductionService::FindProductionPlan(ProductionPlanId id) const noexcept
{
    const auto it = plans_.find(id);
    return it == plans_.end() ? nullptr : &it->second;
}

std::vector<ProductionPlan> ResourcesProductionService::FindProductionPlans(GameplayObjectRef owner) const
{
    std::vector<ProductionPlan> out;
    for (const auto &[id, plan] : plans_)
    {
        (void)id;
        if (!owner.IsValid() || plan.owner == owner)
            out.push_back(plan);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        return a.id < b.id;
    });
    return out;
}

std::vector<ResourceChange> ResourcesProductionService::ChangesSinceSequence(std::uint64_t sequence) const
{
    return ReadChangesSinceSequence(sequence).changes;
}

ResourceChangeBatch ResourcesProductionService::ReadChangesSinceSequence(std::uint64_t sequence) const
{
    ResourceChangeBatch batch;
    batch.oldest_available_sequence = OldestChangeSequence();
    batch.latest_sequence = LatestChangeCursor().sequence;
    if (next_change_sequence_ == 0 || sequence > batch.latest_sequence)
    {
        batch.snapshot_required = true;
        return batch;
    }
    if (changes_.empty())
    {
        batch.snapshot_required = sequence < batch.latest_sequence;
        return batch;
    }
    if (sequence < batch.oldest_available_sequence && batch.oldest_available_sequence - sequence > 1)
    {
        batch.snapshot_required = true;
        return batch;
    }
    for (const auto &change : changes_)
    {
        if (change.sequence > sequence)
            batch.changes.push_back(change);
    }
    return batch;
}

std::uint64_t ResourcesProductionService::OldestChangeSequence() const noexcept
{
    return changes_.empty() ? next_change_sequence_ : changes_.front().sequence;
}


void ResourcesProductionService::PruneChangesBefore(std::uint64_t sequence) noexcept
{
    while (!changes_.empty() && changes_.front().sequence < sequence)
    {
        changes_.pop_front();
    }
}

ResourcesSnapshot ResourcesProductionService::CaptureSnapshot() const
{
    ResourcesSnapshot s;
    for (const auto &[id, v] : stockpiles_)
    {
        (void)id;
        s.stockpiles.push_back(v);
    }
    for (const auto &[key, amount] : amounts_)
    {
        if (amount > 0)
            s.amounts.push_back({key.stockpile, {key.type, amount}});
    }
    for (const auto &[id, v] : nodes_)
    {
        (void)id;
        s.nodes.push_back(v);
    }
    for (const auto &[id, v] : sites_)
    {
        (void)id;
        s.sites.push_back(v);
    }
    for (const auto &[id, v] : reservations_)
    {
        (void)id;
        if (v.state == ResourceReservationState::Active)
            s.reservations.push_back(v);
    }
    for (const auto &[id, v] : capabilities_)
    {
        (void)id;
        s.capabilities.push_back(v);
    }
    for (const auto &[id, v] : plans_)
    {
        (void)id;
        s.plans.push_back(v);
    }
    for (const auto &[id, v] : transactions_)
    {
        (void)id;
        s.transactions.push_back(v);
    }
    std::sort(s.stockpiles.begin(), s.stockpiles.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.amounts.begin(), s.amounts.end(), [](const auto &a, const auto &b) {
        return a.first == b.first ? a.second.type < b.second.type : a.first < b.first;
    });
    std::sort(s.nodes.begin(), s.nodes.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.sites.begin(), s.sites.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.reservations.begin(), s.reservations.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.capabilities.begin(), s.capabilities.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.plans.begin(), s.plans.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.transactions.begin(), s.transactions.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    s.stockpile_ids = stockpile_ids_.GetSnapshot();
    s.node_ids = node_ids_.GetSnapshot();
    s.site_ids = site_ids_.GetSnapshot();
    s.reservation_ids = reservation_ids_.GetSnapshot();
    s.capability_ids = capability_ids_.GetSnapshot();
    s.plan_ids = plan_ids_.GetSnapshot();
    s.transaction_ids = transaction_ids_.GetSnapshot();
    s.revision = revision_;
    s.change_epoch = journal_epoch_;
    return s;
}

foundation::Result<void> ResourcesProductionService::RestoreSnapshot(ResourcesSnapshot s)
{
    const auto next_journal_epoch = CheckedNextChangeEpoch(s.change_epoch > journal_epoch_ ? s.change_epoch : journal_epoch_);
    if (!next_journal_epoch)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.change_journal.epoch_exhausted", "change journal epoch is exhausted"));
    }
    std::unordered_map<ResourceStockpileId, ResourceStockpile, IdHash> stockpiles;
    std::unordered_map<AmountKey, Fixed, AmountKeyHash> amounts;
    std::unordered_map<ResourceNodeId, ResourceNode, IdHash> nodes;
    std::unordered_map<ProductionSiteId, ProductionSite, IdHash> sites;
    std::unordered_map<ResourceReservationId, ResourceReservation, IdHash> reservations;
    std::unordered_map<ProductionCapabilityId, ProductionCapability, IdHash> capabilities;
    std::unordered_map<ProductionPlanId, ProductionPlan, IdHash> plans;
    std::unordered_map<ResourceTransactionId, ResourceTransaction, IdHash> transactions;
    std::unordered_map<AmountKey, Fixed, AmountKeyHash> reserved;

    std::uint64_t max_stockpile = 0;
    std::uint64_t max_node = 0;
    std::uint64_t max_site = 0;
    std::uint64_t max_reservation = 0;
    std::uint64_t max_capability = 0;
    std::uint64_t max_plan = 0;
    std::uint64_t max_transaction = 0;

    for (auto &v : s.stockpiles)
    {
        if (!v.id.IsValid() || !v.owner.IsValid() || !IsValidStockpileState(v.state) || stockpiles.contains(v.id))
            return foundation::Result<void>::Failure(Error("gameplay.resources.restore_invalid", "invalid stockpile"));
        max_stockpile = std::max(max_stockpile, LowPart(v.id.value));
        stockpiles.emplace(v.id, std::move(v));
    }
    for (const auto &p : s.amounts)
    {
        if (!stockpiles.contains(p.first) || !types_.contains(p.second.type) || p.second.amount < 0)
            return foundation::Result<void>::Failure(Error("gameplay.resources.restore_invalid", "invalid amount"));
        auto key = AmountKey{p.first, p.second.type};
        if (amounts.contains(key))
            return foundation::Result<void>::Failure(
                Error("gameplay.resources.restore_invalid", "duplicate amount record"));
        if (p.second.amount > 0)
            amounts.emplace(key, p.second.amount);
    }
    for (auto &v : s.nodes)
    {
        if (!v.id.IsValid() || !types_.contains(v.type) || v.remaining_amount < 0 || v.regeneration_rate_per_tick < 0 ||
            !IsValidNodeState(v.state) || nodes.contains(v.id))
            return foundation::Result<void>::Failure(Error("gameplay.resources.restore_invalid", "invalid node"));
        if (v.maximum_amount <= 0)
            v.maximum_amount = v.remaining_amount;
        if (v.remaining_amount > v.maximum_amount)
            return foundation::Result<void>::Failure(
                Error("gameplay.resources.restore_invalid", "node exceeds capacity"));
        max_node = std::max(max_node, LowPart(v.id.value));
        nodes.emplace(v.id, std::move(v));
    }
    for (auto &v : s.sites)
    {
        if (!v.id.IsValid() || !v.site_object.IsValid() || v.efficiency_micro < 0 || !IsValidSiteState(v.state) ||
            sites.contains(v.id))
            return foundation::Result<void>::Failure(Error("gameplay.resources.restore_invalid", "invalid site"));
        max_site = std::max(max_site, LowPart(v.id.value));
        sites.emplace(v.id, std::move(v));
    }
    for (auto &v : s.reservations)
    {
        auto canonical = CanonicalizeQuantities(v.quantities);
        if (!canonical)
            return foundation::Result<void>::Failure(canonical.GetError());
        if (!v.id.IsValid() || !stockpiles.contains(v.stockpile) || !IsValidReservationState(v.state) ||
            v.state != ResourceReservationState::Active || reservations.contains(v.id))
            return foundation::Result<void>::Failure(
                Error("gameplay.resources.restore_invalid", "invalid reservation"));
        if (stockpiles.at(v.stockpile).state != StockpileState::Active)
            return foundation::Result<void>::Failure(
                Error("gameplay.resources.restore_invalid", "active reservation on unavailable stockpile"));
        v.quantities = std::move(canonical.Value());
        for (const auto &q : v.quantities)
        {
            auto key = AmountKey{v.stockpile, q.type};
            Fixed sum = q.amount;
            const auto existing = reserved.find(key);
            if (existing != reserved.end() && !CheckedAddFixed(existing->second, q.amount, sum))
                return foundation::Result<void>::Failure(
                    Error("gameplay.resources.restore_invalid", "reserved amount overflow"));
            if (existing == reserved.end())
                reserved.emplace(key, q.amount);
            else
                existing->second = sum;
        }
        max_reservation = std::max(max_reservation, LowPart(v.id.value));
        reservations.emplace(v.id, std::move(v));
    }
    for (const auto &[key, amount] : reserved)
    {
        auto available = amounts.find(key);
        if (available == amounts.end() || amount > available->second)
            return foundation::Result<void>::Failure(
                Error("gameplay.resources.restore_invalid", "reserved amount exceeds stockpile amount"));
    }
    for (auto &v : s.capabilities)
    {
        if (!v.id.IsValid() || !v.site.IsValid() || v.capacity < 0 || capabilities.contains(v.id))
            return foundation::Result<void>::Failure(Error("gameplay.resources.restore_invalid", "invalid capability"));
        max_capability = std::max(max_capability, LowPart(v.id.value));
        capabilities.emplace(v.id, std::move(v));
    }
    for (auto &v : s.plans)
    {
        if (!v.id.IsValid() || !v.owner.IsValid() || !types_.contains(v.desired_output) || v.target_quantity <= 0 ||
            !IsValidPlanState(v.state) || plans.contains(v.id))
            return foundation::Result<void>::Failure(
                Error("gameplay.resources.restore_invalid", "invalid production plan"));
        max_plan = std::max(max_plan, LowPart(v.id.value));
        plans.emplace(v.id, std::move(v));
    }
    for (auto &v : s.transactions)
    {
        auto canonical = CanonicalizeQuantities(v.quantities);
        if (!canonical)
            return foundation::Result<void>::Failure(canonical.GetError());
        if (!v.id.IsValid() || !stockpiles.contains(v.from) || !stockpiles.contains(v.to) || v.from == v.to ||
            transactions.contains(v.id))
            return foundation::Result<void>::Failure(
                Error("gameplay.resources.restore_invalid", "invalid transaction"));
        v.quantities = std::move(canonical.Value());
        max_transaction = std::max(max_transaction, LowPart(v.id.value));
        transactions.emplace(v.id, std::move(v));
    }

    auto g = ValidateGenerator(s.stockpile_ids, stockpile_ids_.Scope(), max_stockpile, "stockpile");
    if (!g)
        return g;
    g = ValidateGenerator(s.node_ids, node_ids_.Scope(), max_node, "node");
    if (!g)
        return g;
    g = ValidateGenerator(s.site_ids, site_ids_.Scope(), max_site, "site");
    if (!g)
        return g;
    g = ValidateGenerator(s.reservation_ids, reservation_ids_.Scope(), max_reservation, "reservation");
    if (!g)
        return g;
    g = ValidateGenerator(s.capability_ids, capability_ids_.Scope(), max_capability, "capability");
    if (!g)
        return g;
    g = ValidateGenerator(s.plan_ids, plan_ids_.Scope(), max_plan, "plan");
    if (!g)
        return g;
    g = ValidateGenerator(s.transaction_ids, transaction_ids_.Scope(), max_transaction, "transaction");
    if (!g)
        return g;

    stockpiles_ = std::move(stockpiles);
    amounts_ = std::move(amounts);
    reserved_amounts_ = std::move(reserved);
    nodes_ = std::move(nodes);
    sites_ = std::move(sites);
    reservations_ = std::move(reservations);
    capabilities_ = std::move(capabilities);
    plans_ = std::move(plans);
    transactions_ = std::move(transactions);
    stockpile_ids_.Restore(s.stockpile_ids);
    node_ids_.Restore(s.node_ids);
    site_ids_.Restore(s.site_ids);
    reservation_ids_.Restore(s.reservation_ids);
    capability_ids_.Restore(s.capability_ids);
    plan_ids_.Restore(s.plan_ids);
    transaction_ids_.Restore(s.transaction_ids);
    revision_ = s.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    diagnostics_ = {};
    diagnostics_.transactions = transactions_.size();
    journal_epoch_ = *next_journal_epoch;
    return foundation::Result<void>::Success();
}

ResourcesDiagnostics ResourcesProductionService::GetDiagnostics() const noexcept
{
    auto d = diagnostics_;
    d.stockpiles = stockpiles_.size();
    d.nodes = nodes_.size();
    d.production_sites = sites_.size();
    d.resource_records = amounts_.size();
    d.production_capabilities = capabilities_.size();
    d.production_plans = plans_.size();
    d.active_reservations = reservations_.size();
    d.transactions = transactions_.size();
    return d;
}

void ResourcesProductionService::Record(ResourceChange change)
{
    if (next_change_sequence_ == 0)
        return;
    change.sequence = next_change_sequence_;
    if (next_change_sequence_ == std::numeric_limits<std::uint64_t>::max())
        next_change_sequence_ = 0;
    else
        ++next_change_sequence_;
    changes_.push_back(change);
    while (changes_.size() > change_retention_)
        changes_.pop_front();
}

void ResourcesProductionService::RebuildDerivedState() noexcept
{
    reserved_amounts_.clear();
    diagnostics_ = {};
    diagnostics_.transactions = transactions_.size();
    for (const auto &[id, reservation] : reservations_)
    {
        (void)id;
        if (reservation.state != ResourceReservationState::Active)
            continue;
        for (const auto &q : reservation.quantities)
            AddReservedIndex(reservation.stockpile, q.type, q.amount);
    }
}
} // namespace epidemic::gameplay::resources
