#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <type_traits>

using namespace epidemic::gameplay;

int main()
{
    static_assert(!std::is_same_v<GameplayObjectId, EventId>);
    static_assert(!std::is_same_v<QueryTypeId, EventTypeId>);

    const auto object = GameplayObjectId::FromString("object.house.17");
    const auto same = GameplayObjectId::FromString("object.house.17");
    const auto different = GameplayObjectId::FromString("object.house.18");
    if (!object.IsValid() || object != same || object == different)
    {
        return 1;
    }

    MonotonicIdGenerator<ScheduleId> generator(42);
    const auto first = generator.Next();
    const auto snapshot = generator.GetSnapshot();
    const auto second = generator.Next();
    generator.Restore(snapshot);
    if (first.High() != 42 || second != generator.Next())
    {
        return 2;
    }

    StableTypeRegistry<TypeId> registry;
    const auto registered = registry.Register("framework.test.type");
    if (!registered || registry.Register("framework.test.type"))
    {
        return 3;
    }
    registry.Freeze();
    const auto frozen = registry.Register("framework.test.other");
    if (frozen || !frozen.GetError().HasCode("gameplay.registry_frozen"))
    {
        return 4;
    }

    GameplayTagRegistry tags;
    const auto oak = tags.Register("material.wood.oak");
    if (!oak)
    {
        return 5;
    }
    const auto wood = TagId::FromString("material.wood");
    const auto material = TagId::FromString("material");
    if (!tags.Matches(oak.Value(), wood) || !tags.Matches(oak.Value(), material) || tags.Matches(wood, oak.Value()))
    {
        return 6;
    }

    GameplayTagSet set;
    set.Add(oak.Value());
    GameplayTagSet required;
    required.Add(wood);
    if (!set.HasAll(required, tags))
    {
        return 7;
    }

    const GameplayObjectRef ref{GameplayDomainId::FromString("framework.world"), object};
    if (!ref.IsValid())
    {
        return 8;
    }

    return 0;
}
