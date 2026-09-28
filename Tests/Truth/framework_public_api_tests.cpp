#include "truth_test.h"

#include "Epidemic/GameFramework/AI/ai.h"
#include "Epidemic/GameFramework/Abilities/abilities.h"
#include "Epidemic/GameFramework/Combat/combat.h"
#include "Epidemic/GameFramework/Conditions/conditions.h"
#include "Epidemic/GameFramework/Construction/construction.h"
#include "Epidemic/GameFramework/Dialogue/dialogue.h"
#include "Epidemic/GameFramework/Economy/economy.h"
#include "Epidemic/GameFramework/Effects/effects.h"
#include "Epidemic/GameFramework/Entities/entities.h"
#include "Epidemic/GameFramework/Environment/environment.h"
#include "Epidemic/GameFramework/Equipment/equipment.h"
#include "Epidemic/GameFramework/Facts/gameplay_facts.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"
#include "Epidemic/GameFramework/Interaction/interaction.h"
#include "Epidemic/GameFramework/ItemsInventory/items_inventory.h"
#include "Epidemic/GameFramework/Knowledge/knowledge.h"
#include "Epidemic/GameFramework/Loot/loot.h"
#include "Epidemic/GameFramework/Materials/materials.h"
#include "Epidemic/GameFramework/NavigationSemantics/navigation_semantics.h"
#include "Epidemic/GameFramework/Perception/perception.h"
#include "Epidemic/GameFramework/Progression/progression.h"
#include "Epidemic/GameFramework/Queries/gameplay_queries.h"
#include "Epidemic/GameFramework/RuntimeBridge/runtime_bridge.h"
#include "Epidemic/GameFramework/SupportRandom/deterministic_random.h"
#include "Epidemic/GameFramework/Time/gameplay_time.h"
#include "Epidemic/GameFramework/Traversal/traversal.h"
#include "Epidemic/GameFramework/World/world.h"

#include <array>
#include <set>
#include <type_traits>

using namespace epidemic::gameplay;

namespace
{
void PublicHeadersAreSelfContained()
{
    static_assert(std::is_trivially_copyable_v<GameplayTickId>);
    static_assert(std::is_trivially_copyable_v<GameplayTimePoint>);
    static_assert(std::is_destructible_v<entities::EntityService>);
    static_assert(std::is_destructible_v<items::ItemsInventoryService>);
    static_assert(std::is_destructible_v<economy::EconomyService>);
    static_assert(std::is_destructible_v<progression::ProgressionService>);
    static_assert(std::is_destructible_v<perception::PerceptionService>);
    static_assert(std::is_destructible_v<knowledge::KnowledgeService>);
    static_assert(std::is_destructible_v<ai::AIService>);
    TRUTH_REQUIRE(true);
}

void OwnerDomainsRemainDistinct()
{
    const std::array domains{
        entities::EntityService::Domain(),
        items::ItemsInventoryService::Domain(),
        economy::EconomyService::Domain(),
        progression::ProgressionService::Domain(),
        perception::PerceptionService::Domain(),
        knowledge::KnowledgeService::Domain(),
        ai::AIService::Domain(),
    };
    const std::set<GameplayDomainId> unique{domains.begin(), domains.end()};
    TRUTH_REQUIRE(unique.size() == domains.size());
    for (const auto domain : domains)
    {
        TRUTH_REQUIRE(domain.IsValid());
    }
}
} // namespace

int main()
{
    return epidemic::truth_tests::Run("framework-public-api", {
        {"public headers are self-contained", &PublicHeadersAreSelfContained},
        {"state owners have distinct domains", &OwnerDomainsRemainDistinct},
    });
}