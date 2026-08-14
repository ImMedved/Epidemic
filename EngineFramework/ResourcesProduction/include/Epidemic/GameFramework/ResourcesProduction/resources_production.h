#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::resources
{
using Fixed = std::int64_t;
struct ResourceTypeId { TypeId value{}; static constexpr ResourceTypeId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const ResourceTypeId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const ResourceTypeId&) const noexcept = default; };
struct ResourceUnitId { TypeId value{}; static constexpr ResourceUnitId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const ResourceUnitId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const ResourceUnitId&) const noexcept = default; };
struct ResourceStockpileId { GameplayObjectId value{}; static constexpr ResourceStockpileId FromRaw(std::uint64_t h, std::uint64_t l) noexcept { return {GameplayObjectId::FromRaw(h,l)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const ResourceStockpileId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const ResourceStockpileId&) const noexcept = default; };
struct ResourceNodeId { GameplayObjectId value{}; static constexpr ResourceNodeId FromRaw(std::uint64_t h, std::uint64_t l) noexcept { return {GameplayObjectId::FromRaw(h,l)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const ResourceNodeId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const ResourceNodeId&) const noexcept = default; };
struct ProductionSiteId { GameplayObjectId value{}; static constexpr ProductionSiteId FromRaw(std::uint64_t h, std::uint64_t l) noexcept { return {GameplayObjectId::FromRaw(h,l)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const ProductionSiteId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const ProductionSiteId&) const noexcept = default; };
struct ProductionRecipeId { TypeId value{}; static constexpr ProductionRecipeId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const ProductionRecipeId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const ProductionRecipeId&) const noexcept = default; };
struct ProductionOrderId { GameplayObjectId value{}; static constexpr ProductionOrderId FromRaw(std::uint64_t h, std::uint64_t l) noexcept { return {GameplayObjectId::FromRaw(h,l)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const ProductionOrderId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const ProductionOrderId&) const noexcept = default; };
struct ResourceTransactionId { GameplayObjectId value{}; static constexpr ResourceTransactionId FromRaw(std::uint64_t h, std::uint64_t l) noexcept { return {GameplayObjectId::FromRaw(h,l)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const ResourceTransactionId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const ResourceTransactionId&) const noexcept = default; };
struct ResourceSourceId { GameplayObjectId value{}; static constexpr ResourceSourceId FromRaw(std::uint64_t h, std::uint64_t l) noexcept { return {GameplayObjectId::FromRaw(h,l)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const ResourceSourceId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const ResourceSourceId&) const noexcept = default; };
struct ResourceSinkId { GameplayObjectId value{}; static constexpr ResourceSinkId FromRaw(std::uint64_t h, std::uint64_t l) noexcept { return {GameplayObjectId::FromRaw(h,l)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const ResourceSinkId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const ResourceSinkId&) const noexcept = default; };
struct IdHash { template <class T> [[nodiscard]] std::size_t operator()(const T& id) const noexcept { return std::hash<decltype(id.value)>{}(id.value); } };

enum class ResourceStoragePolicy { Abstract, Stockpile, Node, Virtual };
enum class ResourceDecayPolicy { None, Linear, External };
enum class StockpileState { Active, Locked, Disabled, Destroyed };
enum class ResourceNodeState { Active, Depleted, Disabled, Regenerating };
enum class ProductionSiteState { Active, Paused, Disabled, Destroyed };
enum class ProductionOrderState { Queued, Running, Paused, Completed, Failed, Cancelled, BlockedByResources };
enum class ResourceChangeKind { StockpileCreated, ResourceAdded, ResourceRemoved, ResourceTransferred, NodeDepleted, NodeRegenerated, ProductionSiteCreated, ProductionOrderStarted, ProductionOrderCompleted, ProductionOrderFailed, ShortageDetected, SurplusDetected };

struct ResourceType { ResourceTypeId id{}; std::string canonical_name; GameplayTagSet tags; ResourceUnitId unit{}; ResourceStoragePolicy storage_policy = ResourceStoragePolicy::Stockpile; ResourceDecayPolicy decay_policy = ResourceDecayPolicy::None; Revision revision{}; };
struct ResourceQuantity { ResourceTypeId type{}; Fixed amount = 0; [[nodiscard]] constexpr bool IsValid() const noexcept { return type.IsValid() && amount >= 0; } };
struct ResourceStockpile { ResourceStockpileId id{}; GameplayObjectRef owner{}; GameplayObjectRef location_object{}; GameplayObjectRef area{}; StockpileState state = StockpileState::Active; Revision revision{}; };
struct ResourceNode { ResourceNodeId id{}; ResourceTypeId type{}; GameplayObjectRef area{}; Fixed remaining_amount = 0; Fixed regeneration_rate_per_tick = 0; ResourceNodeState state = ResourceNodeState::Active; Revision revision{}; };
struct ProductionSite { ProductionSiteId id{}; GameplayObjectRef site_object{}; GameplayObjectRef area{}; GameplayTagSet capabilities; ProductionSiteState state = ProductionSiteState::Active; Fixed efficiency_micro = 1'000'000; Revision revision{}; };
struct ProductionRecipe { ProductionRecipeId id{}; std::string canonical_name; std::vector<ResourceQuantity> inputs; std::vector<ResourceQuantity> outputs; GameplayDuration duration{}; Revision revision{}; };
struct ProductionOrder { ProductionOrderId id{}; ProductionSiteId site{}; ProductionRecipeId recipe{}; ResourceStockpileId input_stockpile{}; ResourceStockpileId output_stockpile{}; ProductionOrderState state = ProductionOrderState::Queued; GameplayTimePoint started_at{}; GameplayTimePoint due_at{}; Revision revision{}; };
struct ResourceTransaction { ResourceTransactionId id{}; ResourceStockpileId from{}; ResourceStockpileId to{}; std::vector<ResourceQuantity> quantities; TypeId reason{}; GameplayTimePoint time{}; GameplayContext context{}; Revision revision{}; };
struct ResourceChange { std::uint64_t sequence = 0; ResourceChangeKind kind = ResourceChangeKind::ResourceAdded; ResourceStockpileId stockpile{}; ResourceTypeId resource{}; ProductionOrderId order{}; Fixed amount = 0; GameplayTimePoint time{}; GameplayContext context{}; Revision revision{}; };
struct ResourcesSnapshot { std::vector<ResourceStockpile> stockpiles; std::vector<std::pair<ResourceStockpileId, ResourceQuantity>> amounts; std::vector<ResourceNode> nodes; std::vector<ProductionSite> sites; std::vector<ProductionOrder> orders; MonotonicIdGenerator<GameplayObjectId>::Snapshot stockpile_ids{}; MonotonicIdGenerator<GameplayObjectId>::Snapshot node_ids{}; MonotonicIdGenerator<GameplayObjectId>::Snapshot site_ids{}; MonotonicIdGenerator<GameplayObjectId>::Snapshot order_ids{}; MonotonicIdGenerator<GameplayObjectId>::Snapshot transaction_ids{}; Revision revision{}; };
struct ResourcesDiagnostics { std::uint64_t stockpiles = 0; std::uint64_t resource_records = 0; std::uint64_t nodes = 0; std::uint64_t production_sites = 0; std::uint64_t active_orders = 0; std::uint64_t transactions = 0; std::uint64_t shortages = 0; };

class ResourcesProductionService
{
public:
    ResourcesProductionService();
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept { return GameplayDomainId::FromString("framework.resources_production"); }
    [[nodiscard]] foundation::Result<ResourceTypeId> RegisterResourceType(ResourceType type);
    [[nodiscard]] foundation::Result<ProductionRecipeId> RegisterProductionRecipe(ProductionRecipe recipe);
    void Freeze() noexcept { frozen_ = true; }
    [[nodiscard]] const ResourceType* FindResourceType(ResourceTypeId id) const noexcept;
    [[nodiscard]] const ProductionRecipe* FindProductionRecipe(ProductionRecipeId id) const noexcept;

    [[nodiscard]] foundation::Result<ResourceStockpileId> CreateStockpile(ResourceStockpile stockpile);
    [[nodiscard]] foundation::Result<ResourceNodeId> CreateNode(ResourceNode node);
    [[nodiscard]] foundation::Result<ProductionSiteId> CreateProductionSite(ProductionSite site);
    [[nodiscard]] const ResourceStockpile* FindStockpile(ResourceStockpileId id) const noexcept;
    [[nodiscard]] Fixed GetAmount(ResourceStockpileId stockpile, ResourceTypeId type) const noexcept;
    [[nodiscard]] foundation::Result<void> Add(ResourceStockpileId stockpile, ResourceQuantity quantity, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> Remove(ResourceStockpileId stockpile, ResourceQuantity quantity, GameplayContext context = {});
    [[nodiscard]] foundation::Result<ResourceTransactionId> Transfer(ResourceStockpileId from, ResourceStockpileId to, std::vector<ResourceQuantity> quantities, TypeId reason = {}, GameplayContext context = {});
    [[nodiscard]] bool CanReserve(ResourceStockpileId stockpile, std::span<const ResourceQuantity> quantities) const noexcept;

    [[nodiscard]] foundation::Result<void> DepleteNode(ResourceNodeId node, Fixed amount, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RegenerateNode(ResourceNodeId node, GameplayDuration elapsed, GameplayContext context = {});

    [[nodiscard]] foundation::Result<ProductionOrderId> StartProductionOrder(ProductionSiteId site, ProductionRecipeId recipe, ResourceStockpileId input, ResourceStockpileId output, GameplayTimePoint now, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> CompleteProductionOrder(ProductionOrderId order, GameplayTimePoint now, GameplayContext context = {});
    [[nodiscard]] foundation::Result<std::vector<ProductionOrderId>> CompleteDueOrders(GameplayTimePoint now);
    [[nodiscard]] const ProductionOrder* FindProductionOrder(ProductionOrderId id) const noexcept;

    [[nodiscard]] std::vector<ResourceChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] ResourcesSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(ResourcesSnapshot snapshot);
    [[nodiscard]] ResourcesDiagnostics GetDiagnostics() const noexcept;
private:
    struct AmountKey { ResourceStockpileId stockpile{}; ResourceTypeId type{}; [[nodiscard]] constexpr bool operator==(const AmountKey&) const noexcept = default; };
    struct AmountKeyHash { [[nodiscard]] std::size_t operator()(const AmountKey& k) const noexcept { const auto a = std::hash<GameplayObjectId>{}(k.stockpile.value); const auto b = std::hash<TypeId>{}(k.type.value); return a ^ (b + 0x9E3779B97F4A7C15ull + (a << 6u) + (a >> 2u)); } };
    void Bump() noexcept { ++revision_.value; }
    void Record(ResourceChange change);
    [[nodiscard]] foundation::Result<void> ValidateQuantities(std::span<const ResourceQuantity> quantities) const;

    bool frozen_ = false;
    Revision revision_{};
    std::unordered_map<ResourceTypeId, ResourceType, IdHash> types_;
    std::unordered_map<ResourceStockpileId, ResourceStockpile, IdHash> stockpiles_;
    std::unordered_map<AmountKey, Fixed, AmountKeyHash> amounts_;
    std::unordered_map<ResourceNodeId, ResourceNode, IdHash> nodes_;
    std::unordered_map<ProductionSiteId, ProductionSite, IdHash> sites_;
    std::unordered_map<ProductionRecipeId, ProductionRecipe, IdHash> recipes_;
    std::unordered_map<ProductionOrderId, ProductionOrder, IdHash> orders_;
    MonotonicIdGenerator<GameplayObjectId> stockpile_ids_;
    MonotonicIdGenerator<GameplayObjectId> node_ids_;
    MonotonicIdGenerator<GameplayObjectId> site_ids_;
    MonotonicIdGenerator<GameplayObjectId> order_ids_;
    MonotonicIdGenerator<GameplayObjectId> transaction_ids_;
    std::vector<ResourceChange> changes_;
    std::uint64_t next_change_sequence_ = 1;
    ResourcesDiagnostics diagnostics_{};
};
}
