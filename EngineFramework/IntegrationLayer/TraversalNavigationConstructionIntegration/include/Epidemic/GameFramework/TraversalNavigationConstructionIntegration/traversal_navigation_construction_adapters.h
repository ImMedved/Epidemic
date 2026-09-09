#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Construction/construction.h"
#include "Epidemic/GameFramework/NavigationSemantics/navigation_semantics.h"
#include "Epidemic/GameFramework/Traversal/traversal.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace epidemic::gameplay::traversal_navigation_construction
{
class TraversalNavigationCapabilityProvider final : public navigation_semantics::INavigationCapabilityProvider
{
  public:
    explicit TraversalNavigationCapabilityProvider(const traversal::TraversalService &traversal) : traversal_(traversal) {}
    [[nodiscard]] bool HasCapability(GameplayObjectRef subject, TypeId capability,
                                     navigation_semantics::Fixed min_parameter_micro) const noexcept override;

  private:
    const traversal::TraversalService &traversal_;
};

class TraversalNavigationAdapter
{
  public:
    // GameplayContext is mandatory: Navigation semantics may depend on gameplay time and context-bound fact providers.
    [[nodiscard]] navigation_semantics::NavigationPermissionResult CanUseLink(
        const traversal::TraversalService &traversal,
        const navigation_semantics::NavigationSemanticsService &navigation,
        GameplayObjectRef subject,
        navigation_semantics::NavigationLinkId link,
        const GameplayContext &context) const;
};

enum class ConstructionNavigationOperation : std::uint8_t
{
    Add = 1,
    Update = 2,
    Remove = 3
};

// Durable payload schema for Construction -> Navigation outbox delivery.
// `source_key` identifies one navigation semantic emitted by a placed construction object.
// The final NavigationLayerId is derived from (placed_record, source_key), so Add/Update/Remove
// operations remain idempotent across retries and save/load without an integration-side mapping table.
struct ConstructionNavigationLayerPayload
{
    ConstructionNavigationOperation operation = ConstructionNavigationOperation::Add;
    TypeId source_key{};
    navigation_semantics::NavigationLayerTypeId layer_type{};
    navigation_semantics::NavigationDecisionKind decision = navigation_semantics::NavigationDecisionKind::AddCost;
    navigation_semantics::NavigationLayerLifetime lifetime = navigation_semantics::NavigationLayerLifetime::Persistent;
    std::int32_t priority = 0;
    navigation_semantics::Fixed additive_cost_micro = 0;
    navigation_semantics::Fixed multiplier_micro = 1'000'000;
    TypeId requirement{};
    navigation_semantics::Fixed required_parameter_micro = 0;
    std::optional<GameplayTimePoint> expires_at{};
};

[[nodiscard]] std::vector<std::byte> EncodeNavigationLayerPayload(const ConstructionNavigationLayerPayload &payload);
[[nodiscard]] navigation_semantics::NavigationLayerId NavigationLayerIdFor(
    construction::PlacedObjectId placed_object, TypeId source_key) noexcept;

class ConstructionNavigationAdapter
{
  public:
    // Queue a durable Add/Update/Remove operation for an existing placed construction object.
    [[nodiscard]] foundation::Result<construction::PlacementOutputId> QueueNavigationOperation(
        construction::ConstructionService &construction,
        construction::PlacedObjectId placed_object,
        GameplayObjectRef area,
        const ConstructionNavigationLayerPayload &payload,
        GameplayContext context = {}) const;

    // Production delivery path. Only navigation outputs are consumed; other outbox entries remain pending
    // for their owning integration adapters. Acknowledge happens only after the requested Navigation mutation
    // is confirmed or an idempotent replay is proven already applied.
    [[nodiscard]] foundation::Result<std::size_t> ProcessPendingOutputs(
        construction::ConstructionService &construction,
        navigation_semantics::NavigationSemanticsService &navigation,
        GameplayContext context = {}) const;

  private:
    [[nodiscard]] foundation::Result<ConstructionNavigationLayerPayload> DecodePayload(
        const construction::PlacementOutputOperation &output) const;
    [[nodiscard]] foundation::Result<navigation_semantics::NavigationSemanticLayer> BuildLayer(
        const construction::PlacementOutputOperation &output,
        const ConstructionNavigationLayerPayload &payload,
        navigation_semantics::NavigationLayerId stable_id) const;
};

class ConstructionTraversalAdapter
{
  public:
    // This helper intentionally cancels exactly the supplied sessions. Traversal routes currently do not
    // store NavigationLinkId, so inferring link membership here would be a misleading contract.
    [[nodiscard]] foundation::Result<void> CancelTraversalSessions(
        traversal::TraversalService &traversal,
        std::span<const traversal::TraversalSessionId> sessions,
        GameplayContext context = {}) const;
};
} // namespace epidemic::gameplay::traversal_navigation_construction
