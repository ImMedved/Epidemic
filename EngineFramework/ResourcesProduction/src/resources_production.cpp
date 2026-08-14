#include "Epidemic/GameFramework/ResourcesProduction/resources_production.h"
#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <iterator>

namespace epidemic::gameplay::resources
{
namespace
{
[[nodiscard]] foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}
[[nodiscard]] GameplayTimePoint AddTime(GameplayTimePoint p, GameplayDuration d) noexcept { return GameplayTimePoint{p.ticks + d.ticks}; }
}
ResourcesProductionService::ResourcesProductionService()
    : stockpile_ids_(0x30321001),
      node_ids_(0x30321002),
      site_ids_(0x30321003),
      order_ids_(0x30321004),
      transaction_ids_(0x30321005)
{
}
foundation::Result<ResourceTypeId> ResourcesProductionService::RegisterResourceType(ResourceType type)
{
    if (frozen_) return foundation::Result<ResourceTypeId>::Failure(Error("gameplay.resources.registry_frozen", "resource registry is frozen"));
    if (type.canonical_name.empty()) return foundation::Result<ResourceTypeId>::Failure(Error("gameplay.resources.invalid_type", "resource type name required"));
    const auto canonical = ResourceTypeId::FromString(type.canonical_name);
    if (!type.id.IsValid()) type.id = canonical;
    if (type.id != canonical || types_.contains(type.id)) return foundation::Result<ResourceTypeId>::Failure(Error("gameplay.resources.invalid_type", "invalid or duplicate resource type"));
    Bump(); type.revision = revision_;
    const auto id = type.id;
    types_.emplace(id, std::move(type));
    return foundation::Result<ResourceTypeId>::Success(id);
}
foundation::Result<ProductionRecipeId> ResourcesProductionService::RegisterProductionRecipe(ProductionRecipe recipe)
{
    if (frozen_) return foundation::Result<ProductionRecipeId>::Failure(Error("gameplay.resources.registry_frozen", "resource registry is frozen"));
    if (recipe.canonical_name.empty()) return foundation::Result<ProductionRecipeId>::Failure(Error("gameplay.resources.invalid_recipe", "production recipe name required"));
    const auto canonical = ProductionRecipeId::FromString(recipe.canonical_name);
    if (!recipe.id.IsValid()) recipe.id = canonical;
    if (recipe.id != canonical || recipes_.contains(recipe.id)) return foundation::Result<ProductionRecipeId>::Failure(Error("gameplay.resources.invalid_recipe", "invalid or duplicate production recipe"));
    auto valid_inputs = ValidateQuantities(recipe.inputs); if (!valid_inputs) return foundation::Result<ProductionRecipeId>::Failure(valid_inputs.GetError());
    auto valid_outputs = ValidateQuantities(recipe.outputs); if (!valid_outputs) return foundation::Result<ProductionRecipeId>::Failure(valid_outputs.GetError());
    Bump(); recipe.revision = revision_;
    const auto id = recipe.id;
    recipes_.emplace(id, std::move(recipe));
    return foundation::Result<ProductionRecipeId>::Success(id);
}
const ResourceType* ResourcesProductionService::FindResourceType(ResourceTypeId id) const noexcept { const auto it = types_.find(id); return it == types_.end() ? nullptr : &it->second; }
const ProductionRecipe* ResourcesProductionService::FindProductionRecipe(ProductionRecipeId id) const noexcept { const auto it = recipes_.find(id); return it == recipes_.end() ? nullptr : &it->second; }
foundation::Result<void> ResourcesProductionService::ValidateQuantities(std::span<const ResourceQuantity> quantities) const
{
    for (const auto& q : quantities)
    {
        if (!q.IsValid() || !types_.contains(q.type)) return foundation::Result<void>::Failure(Error("gameplay.resources.invalid_quantity", "invalid resource quantity"));
    }
    return foundation::Result<void>::Success();
}
foundation::Result<ResourceStockpileId> ResourcesProductionService::CreateStockpile(ResourceStockpile stockpile)
{
    if (!stockpile.owner.IsValid()) return foundation::Result<ResourceStockpileId>::Failure(Error("gameplay.resources.invalid_stockpile", "stockpile owner is required"));
    if (!stockpile.id.IsValid()) stockpile.id = ResourceStockpileId{stockpile_ids_.Next()};
    if (stockpiles_.contains(stockpile.id)) return foundation::Result<ResourceStockpileId>::Failure(Error("gameplay.resources.invalid_stockpile", "duplicate stockpile"));
    Bump(); stockpile.revision = revision_;
    const auto id = stockpile.id;
    stockpiles_.emplace(id, std::move(stockpile));
    Record({0, ResourceChangeKind::StockpileCreated, id, {}, {}, 0, {}, {}, revision_});
    return foundation::Result<ResourceStockpileId>::Success(id);
}
foundation::Result<ResourceNodeId> ResourcesProductionService::CreateNode(ResourceNode node)
{
    if (!types_.contains(node.type) || node.remaining_amount < 0) return foundation::Result<ResourceNodeId>::Failure(Error("gameplay.resources.invalid_node", "invalid resource node"));
    if (!node.id.IsValid()) node.id = ResourceNodeId{node_ids_.Next()};
    if (nodes_.contains(node.id)) return foundation::Result<ResourceNodeId>::Failure(Error("gameplay.resources.invalid_node", "duplicate resource node"));
    Bump(); node.revision = revision_;
    const auto id = node.id;
    nodes_.emplace(id, std::move(node));
    return foundation::Result<ResourceNodeId>::Success(id);
}
foundation::Result<ProductionSiteId> ResourcesProductionService::CreateProductionSite(ProductionSite site)
{
    if (!site.site_object.IsValid()) return foundation::Result<ProductionSiteId>::Failure(Error("gameplay.resources.invalid_site", "production site object is required"));
    if (!site.id.IsValid()) site.id = ProductionSiteId{site_ids_.Next()};
    if (sites_.contains(site.id)) return foundation::Result<ProductionSiteId>::Failure(Error("gameplay.resources.invalid_site", "duplicate production site"));
    Bump(); site.revision = revision_;
    const auto id = site.id;
    sites_.emplace(id, std::move(site));
    Record({0, ResourceChangeKind::ProductionSiteCreated, {}, {}, {}, 0, {}, {}, revision_});
    return foundation::Result<ProductionSiteId>::Success(id);
}
const ResourceStockpile* ResourcesProductionService::FindStockpile(ResourceStockpileId id) const noexcept { const auto it = stockpiles_.find(id); return it == stockpiles_.end() ? nullptr : &it->second; }
Fixed ResourcesProductionService::GetAmount(ResourceStockpileId stockpile, ResourceTypeId type) const noexcept
{
    const auto it = amounts_.find({stockpile, type});
    return it == amounts_.end() ? 0 : it->second;
}
foundation::Result<void> ResourcesProductionService::Add(ResourceStockpileId stockpile, ResourceQuantity quantity, GameplayContext context)
{
    if (!stockpiles_.contains(stockpile) || !quantity.IsValid() || !types_.contains(quantity.type)) return foundation::Result<void>::Failure(Error("gameplay.resources.invalid_add", "cannot add resource"));
    Bump(); amounts_[{stockpile, quantity.type}] += quantity.amount;
    Record({0, ResourceChangeKind::ResourceAdded, stockpile, quantity.type, {}, quantity.amount, context.time, context, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> ResourcesProductionService::Remove(ResourceStockpileId stockpile, ResourceQuantity quantity, GameplayContext context)
{
    if (!stockpiles_.contains(stockpile) || !quantity.IsValid() || !types_.contains(quantity.type)) return foundation::Result<void>::Failure(Error("gameplay.resources.invalid_remove", "cannot remove resource"));
    auto& amount = amounts_[{stockpile, quantity.type}];
    if (amount < quantity.amount)
    {
        ++diagnostics_.shortages;
        Record({0, ResourceChangeKind::ShortageDetected, stockpile, quantity.type, {}, quantity.amount - amount, context.time, context, revision_});
        return foundation::Result<void>::Failure(Error("gameplay.resources.shortage", "insufficient resource amount"));
    }
    Bump(); amount -= quantity.amount;
    Record({0, ResourceChangeKind::ResourceRemoved, stockpile, quantity.type, {}, quantity.amount, context.time, context, revision_});
    return foundation::Result<void>::Success();
}
bool ResourcesProductionService::CanReserve(ResourceStockpileId stockpile, std::span<const ResourceQuantity> quantities) const noexcept
{
    if (!stockpiles_.contains(stockpile)) return false;
    for (const auto& q : quantities) if (!q.IsValid() || GetAmount(stockpile, q.type) < q.amount) return false;
    return true;
}
foundation::Result<ResourceTransactionId> ResourcesProductionService::Transfer(ResourceStockpileId from, ResourceStockpileId to, std::vector<ResourceQuantity> quantities, TypeId reason, GameplayContext context)
{
    auto valid = ValidateQuantities(quantities); if (!valid) return foundation::Result<ResourceTransactionId>::Failure(valid.GetError());
    if (!stockpiles_.contains(from) || !stockpiles_.contains(to)) return foundation::Result<ResourceTransactionId>::Failure(Error("gameplay.resources.invalid_transfer", "stockpile missing"));
    if (!CanReserve(from, quantities)) return foundation::Result<ResourceTransactionId>::Failure(Error("gameplay.resources.shortage", "insufficient resources for transfer"));
    const auto id = ResourceTransactionId{transaction_ids_.Next()};
    for (const auto& q : quantities)
    {
        auto removed = Remove(from, q, context); if (!removed) return foundation::Result<ResourceTransactionId>::Failure(removed.GetError());
        auto added = Add(to, q, context); if (!added) return foundation::Result<ResourceTransactionId>::Failure(added.GetError());
        Record({0, ResourceChangeKind::ResourceTransferred, to, q.type, {}, q.amount, context.time, context, revision_});
    }
    (void)reason;
    ++diagnostics_.transactions;
    return foundation::Result<ResourceTransactionId>::Success(id);
}
foundation::Result<void> ResourcesProductionService::DepleteNode(ResourceNodeId id, Fixed amount, GameplayContext context)
{
    auto it = nodes_.find(id);
    if (it == nodes_.end() || amount < 0) return foundation::Result<void>::Failure(Error("gameplay.resources.node_missing", "resource node missing"));
    Bump(); it->second.remaining_amount = std::max<Fixed>(0, it->second.remaining_amount - amount); it->second.revision = revision_;
    if (it->second.remaining_amount == 0) { it->second.state = ResourceNodeState::Depleted; Record({0, ResourceChangeKind::NodeDepleted, {}, it->second.type, {}, 0, context.time, context, revision_}); }
    return foundation::Result<void>::Success();
}
foundation::Result<void> ResourcesProductionService::RegenerateNode(ResourceNodeId id, GameplayDuration elapsed, GameplayContext context)
{
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return foundation::Result<void>::Failure(Error("gameplay.resources.node_missing", "resource node missing"));
    if (elapsed.ticks <= 0 || it->second.regeneration_rate_per_tick <= 0) return foundation::Result<void>::Success();
    Bump(); it->second.remaining_amount += elapsed.ticks * it->second.regeneration_rate_per_tick; it->second.state = ResourceNodeState::Active; it->second.revision = revision_;
    Record({0, ResourceChangeKind::NodeRegenerated, {}, it->second.type, {}, it->second.remaining_amount, context.time, context, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<ProductionOrderId> ResourcesProductionService::StartProductionOrder(ProductionSiteId site_id, ProductionRecipeId recipe_id, ResourceStockpileId input, ResourceStockpileId output, GameplayTimePoint now, GameplayContext context)
{
    const auto sit = sites_.find(site_id);
    const auto rec = recipes_.find(recipe_id);
    if (sit == sites_.end() || rec == recipes_.end() || !stockpiles_.contains(input) || !stockpiles_.contains(output)) return foundation::Result<ProductionOrderId>::Failure(Error("gameplay.resources.production_invalid", "invalid production order references"));
    if (sit->second.state != ProductionSiteState::Active) return foundation::Result<ProductionOrderId>::Failure(Error("gameplay.resources.site_unavailable", "production site unavailable"));
    if (!CanReserve(input, rec->second.inputs)) return foundation::Result<ProductionOrderId>::Failure(Error("gameplay.resources.shortage", "production inputs unavailable"));
    const auto id = ProductionOrderId{order_ids_.Next()};
    ProductionOrder order; order.id = id; order.site = site_id; order.recipe = recipe_id; order.input_stockpile = input; order.output_stockpile = output; order.state = ProductionOrderState::Running; order.started_at = now; order.due_at = AddTime(now, rec->second.duration);
    Bump(); order.revision = revision_;
    orders_.emplace(id, order);
    Record({0, ResourceChangeKind::ProductionOrderStarted, input, {}, id, 0, now, context, revision_});
    return foundation::Result<ProductionOrderId>::Success(id);
}
foundation::Result<void> ResourcesProductionService::CompleteProductionOrder(ProductionOrderId id, GameplayTimePoint now, GameplayContext context)
{
    auto it = orders_.find(id);
    if (it == orders_.end()) return foundation::Result<void>::Failure(Error("gameplay.resources.order_missing", "production order missing"));
    if (it->second.state == ProductionOrderState::Completed) return foundation::Result<void>::Success();
    if (it->second.state != ProductionOrderState::Running) return foundation::Result<void>::Failure(Error("gameplay.resources.order_invalid_state", "production order is not running"));
    if (now.ticks < it->second.due_at.ticks) return foundation::Result<void>::Failure(Error("gameplay.resources.order_not_due", "production order not due"));
    const auto* recipe = FindProductionRecipe(it->second.recipe);
    if (!recipe) return foundation::Result<void>::Failure(Error("gameplay.resources.recipe_missing", "production recipe missing"));
    for (const auto& input : recipe->inputs) { auto r = Remove(it->second.input_stockpile, input, context); if (!r) { it->second.state = ProductionOrderState::BlockedByResources; return r; } }
    for (const auto& output : recipe->outputs) { auto r = Add(it->second.output_stockpile, output, context); if (!r) return r; }
    Bump(); it->second.state = ProductionOrderState::Completed; it->second.revision = revision_;
    Record({0, ResourceChangeKind::ProductionOrderCompleted, it->second.output_stockpile, {}, id, 0, now, context, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<std::vector<ProductionOrderId>> ResourcesProductionService::CompleteDueOrders(GameplayTimePoint now)
{
    std::vector<ProductionOrderId> due;
    for (const auto& [id, order] : orders_) if (order.state == ProductionOrderState::Running && order.due_at.ticks <= now.ticks) due.push_back(id);
    std::sort(due.begin(), due.end());
    for (auto id : due) { GameplayContext c; c.time = now; auto r = CompleteProductionOrder(id, now, c); if (!r) return foundation::Result<std::vector<ProductionOrderId>>::Failure(r.GetError()); }
    return foundation::Result<std::vector<ProductionOrderId>>::Success(std::move(due));
}
const ProductionOrder* ResourcesProductionService::FindProductionOrder(ProductionOrderId id) const noexcept { const auto it = orders_.find(id); return it == orders_.end() ? nullptr : &it->second; }
std::vector<ResourceChange> ResourcesProductionService::ChangesSince(std::uint64_t sequence) const
{
    std::vector<ResourceChange> out; std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(out), [&](const auto& c){ return c.sequence > sequence; }); return out;
}
ResourcesSnapshot ResourcesProductionService::CaptureSnapshot() const
{
    ResourcesSnapshot s;
    for (const auto& [id, v] : stockpiles_) { (void)id; s.stockpiles.push_back(v); }
    for (const auto& [key, amount] : amounts_) s.amounts.push_back({key.stockpile, {key.type, amount}});
    for (const auto& [id, v] : nodes_) { (void)id; s.nodes.push_back(v); }
    for (const auto& [id, v] : sites_) { (void)id; s.sites.push_back(v); }
    for (const auto& [id, v] : orders_) { (void)id; if (v.state != ProductionOrderState::Completed) s.orders.push_back(v); }
    std::sort(s.stockpiles.begin(), s.stockpiles.end(), [](auto&a, auto&b){ return a.id < b.id; });
    std::sort(s.amounts.begin(), s.amounts.end(), [](auto&a, auto&b){ return a.first == b.first ? a.second.type < b.second.type : a.first < b.first; });
    std::sort(s.nodes.begin(), s.nodes.end(), [](auto&a, auto&b){ return a.id < b.id; });
    std::sort(s.sites.begin(), s.sites.end(), [](auto&a, auto&b){ return a.id < b.id; });
    std::sort(s.orders.begin(), s.orders.end(), [](auto&a, auto&b){ return a.id < b.id; });
    s.stockpile_ids = stockpile_ids_.GetSnapshot(); s.node_ids = node_ids_.GetSnapshot(); s.site_ids = site_ids_.GetSnapshot(); s.order_ids = order_ids_.GetSnapshot(); s.transaction_ids = transaction_ids_.GetSnapshot(); s.revision = revision_;
    return s;
}
foundation::Result<void> ResourcesProductionService::RestoreSnapshot(ResourcesSnapshot s)
{
    stockpiles_.clear(); amounts_.clear(); nodes_.clear(); sites_.clear(); orders_.clear();
    for (auto& v : s.stockpiles) { if (!v.id.IsValid()) return foundation::Result<void>::Failure(Error("gameplay.resources.restore_invalid", "invalid stockpile")); stockpiles_[v.id] = std::move(v); }
    for (auto& p : s.amounts) { if (!stockpiles_.contains(p.first) || !types_.contains(p.second.type)) return foundation::Result<void>::Failure(Error("gameplay.resources.restore_invalid", "invalid amount")); amounts_[{p.first, p.second.type}] = p.second.amount; }
    for (auto& v : s.nodes) { if (!v.id.IsValid() || !types_.contains(v.type)) return foundation::Result<void>::Failure(Error("gameplay.resources.restore_invalid", "invalid node")); nodes_[v.id] = std::move(v); }
    for (auto& v : s.sites) { if (!v.id.IsValid()) return foundation::Result<void>::Failure(Error("gameplay.resources.restore_invalid", "invalid site")); sites_[v.id] = std::move(v); }
    for (auto& v : s.orders) { if (!v.id.IsValid() || !recipes_.contains(v.recipe)) return foundation::Result<void>::Failure(Error("gameplay.resources.restore_invalid", "invalid order")); orders_[v.id] = std::move(v); }
    stockpile_ids_.Restore(s.stockpile_ids); node_ids_.Restore(s.node_ids); site_ids_.Restore(s.site_ids); order_ids_.Restore(s.order_ids); transaction_ids_.Restore(s.transaction_ids); revision_ = s.revision; changes_.clear(); next_change_sequence_ = 1;
    return foundation::Result<void>::Success();
}
ResourcesDiagnostics ResourcesProductionService::GetDiagnostics() const noexcept
{
    auto d = diagnostics_; d.stockpiles = stockpiles_.size(); d.nodes = nodes_.size(); d.production_sites = sites_.size(); d.resource_records = amounts_.size(); d.active_orders = 0; for (const auto& [id, o] : orders_) { (void)id; if (o.state == ProductionOrderState::Running || o.state == ProductionOrderState::Queued) ++d.active_orders; } return d;
}
void ResourcesProductionService::Record(ResourceChange change)
{
    change.sequence = next_change_sequence_++;
    changes_.push_back(change);
}
}
