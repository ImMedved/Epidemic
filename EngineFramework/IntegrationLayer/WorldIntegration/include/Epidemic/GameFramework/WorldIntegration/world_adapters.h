#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Entities/entities.h"
#include "Epidemic/GameFramework/Environment/environment.h"
#include "Epidemic/GameFramework/Facts/gameplay_facts.h"
#include "Epidemic/GameFramework/Interaction/interaction.h"
#include "Epidemic/GameFramework/Queries/gameplay_queries.h"
#include "Epidemic/GameFramework/World/world.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::world_integration
{
struct AreasAtPositionQuery
{
    using ResultType = std::vector<world::WorldAreaDefinition>;
    world::WorldPosition position{};
    [[nodiscard]] static constexpr QueryTypeId Type() noexcept
    {
        return QueryTypeId::FromString("framework.query.world.areas_at");
    }
};
struct AlterationsInAreaQuery
{
    using ResultType = std::vector<world::WorldAlterationRecord>;
    world::WorldAabb area{};
    std::optional<world::WorldAlterationTypeId> type{};
    [[nodiscard]] static constexpr QueryTypeId Type() noexcept
    {
        return QueryTypeId::FromString("framework.query.world.alterations");
    }
};
struct EnvironmentSampleQuery
{
    using ResultType = environment::EnvironmentSample;
    environment::EnvironmentPosition position{};
    GameplayTimePoint time{};
    std::vector<GameplayObjectRef> scopes;
    [[nodiscard]] static constexpr QueryTypeId Type() noexcept
    {
        return QueryTypeId::FromString("framework.query.environment.sample");
    }
};
struct ActiveInteractionsQuery
{
    using ResultType = std::vector<interaction::InteractionSession>;
    GameplayObjectRef actor{};
    [[nodiscard]] static constexpr QueryTypeId Type() noexcept
    {
        return QueryTypeId::FromString("framework.query.interaction.active");
    }
};

struct WorldFactsCheckpoint
{
    static constexpr std::uint32_t kSchemaVersion = 2;

    std::uint32_t schema_version = kSchemaVersion;
    std::uint64_t world_sequence = 0;
    std::uint64_t environment_sequence = 0;
    std::uint64_t interaction_sequence = 0;
    std::uint64_t world_epoch = 0;
    std::uint64_t environment_epoch = 0;
    std::uint64_t interaction_epoch = 0;
    Revision world_revision{};
    Revision environment_revision{};
    Revision interaction_revision{};
};

[[nodiscard]] constexpr GameplayObjectRef GlobalWorldScope() noexcept
{
    return {world::WorldService::Domain(), GameplayObjectId::FromString("framework.world.scope.global")};
}

// Current-state providers are intended to run inside the composition gameplay read phase.
// World/Environment/Interaction mutations must not execute concurrently with these reads.
class WorldQueryAdapter
{
  public:
    WorldQueryAdapter(world::WorldService &w, environment::EnvironmentService &e, interaction::InteractionService &i,
                      queries::GameplayQueryService &q)
        : world_(w), environment_(e), interaction_(i), queries_(q)
    {
    }
    [[nodiscard]] foundation::Result<void> RegisterProviders();

  private:
    world::WorldService &world_;
    environment::EnvironmentService &environment_;
    interaction::InteractionService &interaction_;
    queries::GameplayQueryService &queries_;
};

class WorldFactsAdapter
{
  public:
    WorldFactsAdapter(world::WorldService &w, environment::EnvironmentService &e, interaction::InteractionService &i,
                      facts::GameplayFactsService &f)
        : world_(w), environment_(e), interaction_(i), facts_(f)
    {
    }
    [[nodiscard]] foundation::Result<void> RegisterContracts();
    [[nodiscard]] foundation::Result<std::uint64_t> PublishPending(GameplayContext context = {});
    [[nodiscard]] WorldFactsCheckpoint CaptureCheckpoint() const noexcept;
    [[nodiscard]] foundation::Result<void> RestoreCheckpoint(WorldFactsCheckpoint checkpoint);
    // Event history cannot be reconstructed from current owner snapshots. Call this only after the
    // composition layer has explicitly reconciled/accepted unavailable historical events.
    void ResetCursorsToLatest() noexcept;

  private:
    world::WorldService &world_;
    environment::EnvironmentService &environment_;
    interaction::InteractionService &interaction_;
    facts::GameplayFactsService &facts_;
    EventTypeId world_event_{}, environment_event_{}, interaction_event_{};
    ChangeCursor wc_{}, ec_{}, ic_{};
};

class EntityInteractionStateProvider final : public interaction::IInteractionStateProvider
{
  public:
    explicit EntityInteractionStateProvider(const entities::EntityService &e) : entities_(e)
    {
    }
    [[nodiscard]] bool IsMaterialized(GameplayObjectRef object) const override;
    [[nodiscard]] Revision RevisionOf(GameplayObjectRef object) const override;

  private:
    const entities::EntityService &entities_;
};

} // namespace epidemic::gameplay::world_integration
