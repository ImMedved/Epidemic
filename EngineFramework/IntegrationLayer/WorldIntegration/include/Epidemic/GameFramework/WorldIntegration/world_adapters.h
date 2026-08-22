#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Entities/entities.h"
#include "Epidemic/GameFramework/Environment/environment.h"
#include "Epidemic/GameFramework/Facts/gameplay_facts.h"
#include "Epidemic/GameFramework/Interaction/interaction.h"
#include "Epidemic/GameFramework/Queries/gameplay_queries.h"
#include "Epidemic/GameFramework/World/world.h"

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

  private:
    world::WorldService &world_;
    environment::EnvironmentService &environment_;
    interaction::InteractionService &interaction_;
    facts::GameplayFactsService &facts_;
    EventTypeId world_event_{}, environment_event_{}, interaction_event_{};
    std::uint64_t wc_ = 0, ec_ = 0, ic_ = 0;
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
