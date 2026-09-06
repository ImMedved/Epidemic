#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <cstdlib>
#include <limits>
#include <type_traits>

using namespace epidemic::gameplay;

namespace
{
void Check(bool condition, int code)
{
    if (!condition)
    {
        std::exit(code);
    }
}

GameplayObjectRef MakeRef(std::string_view domain, std::string_view id)
{
    return GameplayObjectRef{GameplayDomainId::FromString(domain), GameplayObjectId::FromString(id)};
}
} // namespace

int main()
{
    static_assert(!std::is_same_v<GameplayObjectId, EventId>);
    static_assert(!std::is_same_v<QueryTypeId, EventTypeId>);
    static_assert(!std::is_same_v<IdScopeId, TypeId>);

    const auto object = GameplayObjectId::FromString("object.house.17");
    const auto same = GameplayObjectId::FromString("object.house.17");
    const auto different = GameplayObjectId::FromString("object.house.18");
    Check(object.IsValid() && object == same && object != different, 1);
    Check(!GameplayObjectId::FromString("").IsValid(), 2);
    Check(TypeId::FromString("non_empty_type").IsValid(), 3);
    Check(!TypeId::FromString("").IsValid(), 4);
    Check(object < different || different < object, 5);
    Check(std::hash<GameplayObjectId>{}(object) == std::hash<GameplayObjectId>{}(same), 6);

    MonotonicIdGenerator<ScheduleId> generator(42);
    const auto first = generator.Next();
    const auto snapshot = generator.GetSnapshot();
    const auto second = generator.Next();
    generator.Restore(snapshot);
    Check(first.High() == 42 && second == generator.Next(), 7);

    MonotonicIdGenerator<ScheduleId> named = MonotonicIdGenerator<ScheduleId>::FromScopeName("framework.time.schedule_ids");
    Check(named.Scope() == IdScopeId::FromString("framework.time.schedule_ids"), 8);
    Check(MonotonicIdGenerator<ScheduleId>::IsValidSnapshot(named.GetSnapshot()), 9);

    const auto generator_restore_ok = ValidateMonotonicIdGeneratorSnapshot<ScheduleId>(
        MonotonicIdGenerator<ScheduleId>::Snapshot{42, second.Low() + 1}, IdScopeId::FromRaw(42), second.Low());
    Check(generator_restore_ok.IsValid(), 901);
    Check(generator_restore_ok.Code() == "gameplay.id_generator_snapshot_valid", 902);
    Check(!ValidateMonotonicIdGeneratorSnapshot<ScheduleId>(
               MonotonicIdGenerator<ScheduleId>::Snapshot{0, 1}, IdScopeId::FromRaw(42), 0)
               .IsValid(),
          903);
    Check(!ValidateMonotonicIdGeneratorSnapshot<ScheduleId>(
               MonotonicIdGenerator<ScheduleId>::Snapshot{42, 1}, {}, 0)
               .IsValid(),
          904);
    Check(!ValidateMonotonicIdGeneratorSnapshot<ScheduleId>(
               MonotonicIdGenerator<ScheduleId>::Snapshot{43, 1}, IdScopeId::FromRaw(42), 0)
               .IsValid(),
          905);
    Check(!ValidateMonotonicIdGeneratorSnapshot<ScheduleId>(
               MonotonicIdGenerator<ScheduleId>::Snapshot{42, second.Low()}, IdScopeId::FromRaw(42), second.Low())
               .IsValid(),
          906);
    Check(!ValidateMonotonicIdGeneratorSnapshot<ScheduleId>(
               MonotonicIdGenerator<ScheduleId>::Snapshot{42, second.Low() - 1}, IdScopeId::FromRaw(42), second.Low())
               .IsValid(),
          907);
    Check(ValidateMonotonicIdGeneratorSnapshot<ScheduleId>(
              MonotonicIdGenerator<ScheduleId>::Snapshot{42, 0},
              IdScopeId::FromRaw(42),
              std::numeric_limits<std::uint64_t>::max())
              .IsValid(),
          908);
    Check(!ValidateMonotonicIdGeneratorSnapshot<ScheduleId>(
               MonotonicIdGenerator<ScheduleId>::Snapshot{42, std::numeric_limits<std::uint64_t>::max()},
               IdScopeId::FromRaw(42),
               std::numeric_limits<std::uint64_t>::max())
               .IsValid(),
          909);

    MonotonicIdGenerator<ScheduleId> exhausted(7);
    exhausted.Restore(MonotonicIdGenerator<ScheduleId>::Snapshot{7, std::numeric_limits<std::uint64_t>::max()});
    const auto last = exhausted.Next();
    Check(last.IsValid() && last.High() == 7 && last.Low() == std::numeric_limits<std::uint64_t>::max(), 10);
    const auto exhausted_snapshot = exhausted.GetSnapshot();
    Check(exhausted.IsExhausted() && exhausted_snapshot.next == 0, 11);
    exhausted.Restore(exhausted_snapshot);
    Check(!exhausted.Next().IsValid(), 12);

    StableTypeRegistry<TypeId> registry;
    const auto registered = registry.Register("framework.test.type");
    Check(static_cast<bool>(registered), 13);
    Check(!registry.Register("framework.test.type"), 14);
    Check(!registry.Register("Framework.Test.Type"), 15);
    Check(!registry.Register("framework. test.type"), 16);
    Check(!registry.Register("framework..test"), 17);
    Check(registry.Find("framework.test.type") != nullptr, 18);
    Check(registry.Find("framework..test") == nullptr, 19);
    registry.Freeze();
    const auto frozen = registry.Register("framework.test.other");
    Check(!frozen && frozen.GetError().HasCode("gameplay.registry_frozen"), 20);

    GameplayTagRegistry tags;
    const auto oak = tags.Register("material.wood.oak");
    Check(static_cast<bool>(oak), 21);
    const auto size_before_invalid = tags.Size();
    const auto malformed = tags.Register("alpha.beta..gamma");
    Check(!malformed && tags.Size() == size_before_invalid, 22);
    Check(!tags.Register("alpha."), 23);
    Check(!tags.Register(".alpha"), 24);
    Check(!tags.Register("Alpha.beta"), 25);
    const auto wood = TagId::FromString("material.wood");
    const auto material = TagId::FromString("material");
    Check(tags.Matches(oak.Value(), wood), 26);
    Check(tags.Matches(oak.Value(), material), 27);
    Check(!tags.Matches(wood, oak.Value()), 28);
    Check(!tags.Matches(TagId::FromString("unknown.child"), material), 29);
    tags.Freeze();
    const auto frozen_tag = tags.Register("material.stone");
    Check(!frozen_tag && frozen_tag.GetError().HasCode("gameplay.registry_frozen"), 30);

    GameplayTagSet set;
    set.Add(oak.Value());
    set.Add(oak.Value());
    set.Add({});
    Check(set.Values().size() == 1 && set.HasExact(oak.Value()), 31);
    GameplayTagSet required;
    required.Add(wood);
    Check(set.HasAll(required, tags) && set.HasAny(required, tags), 32);
    set.Remove(oak.Value());
    Check(set.Values().empty(), 33);

    const auto max_time = GameplayTimePoint{std::numeric_limits<std::int64_t>::max()};
    const auto min_time = GameplayTimePoint{std::numeric_limits<std::int64_t>::min()};
    Check(!CheckedAdd(max_time, GameplayDuration{1}).has_value(), 34);
    Check(!CheckedSubtract(min_time, GameplayDuration{1}).has_value(), 35);
    Check(!CheckedDifference(max_time, min_time).has_value(), 36);
    Check(SaturatingAdd(max_time, GameplayDuration{1}).ticks == std::numeric_limits<std::int64_t>::max(), 37);
    Check(SaturatingSubtract(min_time, GameplayDuration{1}).ticks == std::numeric_limits<std::int64_t>::min(), 38);
    Check(SaturatingDifference(max_time, min_time).ticks == std::numeric_limits<std::int64_t>::max(), 39);
    Check((GameplayTimePoint{10} + GameplayDuration{-3}).ticks == 7, 40);
    Check((GameplayTimePoint{10} - GameplayDuration{3}).ticks == 7, 41);
    Check((GameplayTimePoint{10} - GameplayTimePoint{3}).ticks == 7, 42);
    static_assert(kGameplayTimeTicksPerSecond == 1);
    static_assert(kGameplaySecondsPerHour == 3600);
    Check(GameplaySeconds(12).ticks == 12, 421);
    Check(GameplayTimeSeconds(20).ticks == 20, 422);
    Check(ToGameplaySeconds(GameplayDuration{33}) == 33, 423);
    Check(ToGameplaySeconds(GameplayTimePoint{44}) == 44, 424);

    Check(Revision{}.Raw() == 0, 43);
    const auto next_revision = CheckedNext(Revision{41});
    Check(next_revision && next_revision->Raw() == 42, 44);
    Check(!CheckedNext(Revision{std::numeric_limits<std::uint64_t>::max()}), 45);
    Check(CheckedNextSequence(41).value_or(0) == 42, 451);
    Check(!CheckedNextSequence(std::numeric_limits<std::uint64_t>::max()), 452);

    const auto actor = MakeRef("framework.entities", "actor");
    const GameplayObjectPartRef actor_part{actor, TypeId::FromString("entity.part.hand")};
    Check(actor_part.IsValid() && actor_part.object == actor, 453);
    const auto source = MakeRef("framework.effects", "source");
    GameplayContext context;
    context.tick = GameplayTickId{1};
    context.time = GameplayTimePoint{100};
    context.operation = OperationId::FromString("operation.child");
    context.parent_operation = OperationId::FromString("operation.parent");
    context.cause_event = EventId::FromString("event.parent_completed");
    context.correlation = CorrelationId::FromString("correlation.root");
    context.actor = actor;
    context.instigator = actor;
    context.source = source;
    Check(context.parent_operation.IsValid() && context.cause_event.IsValid(), 46);
    Check(context.actor == actor && context.instigator == actor && context.source == source, 47);

    const GameplayObjectRef valid_ref{GameplayDomainId::FromString("framework.world"), object};
    const GameplayObjectRef invalid_ref{GameplayDomainId::FromString("framework.world"), {}};
    Check(valid_ref.IsValid() && !invalid_ref.IsValid(), 48);

    return 0;
}