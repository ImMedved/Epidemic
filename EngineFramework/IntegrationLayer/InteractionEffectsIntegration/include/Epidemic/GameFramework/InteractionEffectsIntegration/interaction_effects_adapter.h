#pragma once

#include "Epidemic/GameFramework/Effects/effects.h"
#include "Epidemic/GameFramework/Interaction/interaction.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::interaction_effects_integration
{
enum class InteractionEffectDeliveryState
{
    Pending,
    Applied,
    Rejected,
    ReconciliationRequired,
};

struct InteractionEffectDeliveryRecord
{
    interaction::InteractionExecutionId execution{};
    interaction::InteractionTypeId interaction_type{};
    std::uint32_t effect_index = 0;
    effects::EffectDefinitionId definition{};
    effects::EffectExecutionId effect_execution{};
    InteractionEffectDeliveryState state = InteractionEffectDeliveryState::Pending;
};

struct InteractionEffectsSnapshot
{
    std::vector<InteractionEffectDeliveryRecord> deliveries;
};

// Maps one Interaction type to an ordered list of required Effects definitions.
// Mapping configuration is bootstrap-only. Freeze the mapping after both
// definition registries are populated, then register this executor with
// InteractionService before freezing that service. Delivery records are technical
// idempotency state and should participate in save/checkpoint orchestration when
// an Interaction execution can be retried across restore.
class InteractionEffectExecutor final : public interaction::IInteractionExecutor
{
  public:
    InteractionEffectExecutor(effects::EffectService& effects, interaction::InteractionTypeId interaction_type)
        : effects_(effects), interaction_type_(interaction_type)
    {
    }

    [[nodiscard]] foundation::Result<void> RegisterEffect(effects::EffectDefinitionId definition);
    [[nodiscard]] foundation::Result<void> Freeze(const interaction::InteractionService& interactions);
    [[nodiscard]] bool IsFrozen() const noexcept { return frozen_; }

    [[nodiscard]] foundation::Result<void> Validate(const interaction::InteractionPlan& plan) const override;
    [[nodiscard]] foundation::Result<void> Commit(
        const interaction::InteractionPlan& plan,
        interaction::InteractionExecutionId execution) noexcept override;

    [[nodiscard]] InteractionEffectsSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(InteractionEffectsSnapshot snapshot);
    [[nodiscard]] const InteractionEffectDeliveryRecord* FindDelivery(
        interaction::InteractionExecutionId execution,
        std::uint32_t effect_index) const noexcept;

    // Safe only after the source Interaction execution is terminal and can no
    // longer be retried. This keeps retention policy outside the adapter.
    void PruneTerminalExecution(interaction::InteractionExecutionId execution) noexcept;

  private:
    struct DeliveryKey
    {
        interaction::InteractionExecutionId execution{};
        std::uint32_t effect_index = 0;

        [[nodiscard]] bool operator==(const DeliveryKey&) const noexcept = default;
    };

    struct DeliveryKeyHash
    {
        [[nodiscard]] std::size_t operator()(const DeliveryKey& key) const noexcept;
    };

    [[nodiscard]] static OperationId DeliveryOperation(
        interaction::InteractionExecutionId execution,
        std::uint32_t effect_index,
        effects::EffectDefinitionId definition) noexcept;
    [[nodiscard]] static CorrelationId DeliveryCorrelation(interaction::InteractionExecutionId execution) noexcept;
    [[nodiscard]] foundation::Result<void> ValidateMapping() const;

    effects::EffectService& effects_;
    interaction::InteractionTypeId interaction_type_{};
    std::vector<effects::EffectDefinitionId> definitions_;
    std::unordered_map<DeliveryKey, InteractionEffectDeliveryRecord, DeliveryKeyHash> deliveries_;
    bool frozen_ = false;
};
} // namespace epidemic::gameplay::interaction_effects_integration
