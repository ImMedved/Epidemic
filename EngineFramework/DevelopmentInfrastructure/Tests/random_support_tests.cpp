#include "Epidemic/GameFramework/SupportRandom/deterministic_random.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <vector>

using namespace epidemic::gameplay::random;

namespace
{
void Check(bool condition, int code)
{
    if (!condition)
    {
        std::exit(code);
    }
}

void CheckClose(double actual, double expected, double epsilon, int code)
{
    Check(std::fabs(actual - expected) <= epsilon, code);
}
} // namespace

int main()
{
    Check(RandomSeed{0} == RandomSeed{0}, 1);
    Check(RandomSeed{1} != RandomSeed{2}, 2);

    const auto combat = RandomStream::FromString("combat.hit");
    const auto combat_again = RandomStream::FromString("combat.hit");
    const auto loot = RandomStream::FromString("loot.table");
    Check(combat.IsValid() && combat == combat_again && combat != loot, 3);
    Check(!RandomStream::FromString("").IsValid(), 4);

    Check(StableMix(0) == 16294208416658607535ull, 5);
    Check(StableMix(1) == 10451216379200822465ull, 6);
    Check(StableMix(42) == 13679457532755275413ull, 7);
    Check(StableMix(std::numeric_limits<std::uint64_t>::max()) == 16490336266968443936ull, 8);

    Check(DeriveSeed({42}, combat).value == 5979799565065418595ull, 9);
    Check(DeriveSeed({42}, combat, 9).value == 222481107986405518ull, 10);
    Check(DeriveSeed({42}, combat, 9) != DeriveSeed({42}, loot, 9), 11);

    RandomSequence sequence({42}, combat);
    Check(sequence.Sequence() == 0, 12);
    Check(sequence.NextU64() == 5979799565065418595ull, 13);
    Check(sequence.NextU64() == 284422869324198993ull, 14);
    Check(sequence.NextU64() == 217384426050393463ull, 15);
    Check(sequence.Sequence() == 3, 16);

    RandomSequence replay({42}, combat, 3);
    Check(replay.NextU64() == 2907130181002816261ull, 17);
    Check(replay.NextU64() == 4593431065408583085ull, 18);

    RandomSequence same_a({42}, combat);
    RandomSequence same_b({42}, combat);
    for (int index = 0; index < 1000; ++index)
    {
        Check(same_a.NextU64() == same_b.NextU64(), 19);
    }

    RandomSequence different_seed({43}, combat);
    RandomSequence different_stream({42}, loot);
    Check(RandomSequence({42}, combat).NextU64() != different_seed.NextU64(), 20);
    Check(RandomSequence({42}, combat).NextU64() != different_stream.NextU64(), 21);

    RandomSequence uniform({42}, combat);
    Check(uniform.Uniform(1) == 0, 22);
    Check(uniform.Uniform(10) == 5, 23);
    for (int index = 0; index < 128; ++index)
    {
        Check(uniform.Uniform(17) < 17, 24);
    }
    Check(RandomSequence({42}, combat).Uniform(10) == RandomSequence({42}, combat).Uniform(10), 25);
    Check(RandomSequence({42}, combat).Uniform(std::numeric_limits<std::uint64_t>::max()) <
              std::numeric_limits<std::uint64_t>::max(),
          26);

    RandomSequence range({42}, RandomStream::FromString("range"));
    Check(range.UniformRange(5, 13) == 12, 27);
    for (int index = 0; index < 128; ++index)
    {
        const auto value = range.UniformRange(100, 200);
        Check(value >= 100 && value < 200, 28);
    }

    RandomSequence real({42}, RandomStream::FromString("real"));
    CheckClose(real.Uniform01(), 0.77932763623837287, 0.0000000000000002, 29);
    CheckClose(RandomSequence({42}, RandomStream::FromString("real")).UniformReal(10.0, 20.0), 17.79327636238373,
               0.00000000000001, 30);
    for (int index = 0; index < 128; ++index)
    {
        const auto value = real.UniformReal(-2.0, 3.0);
        Check(value >= -2.0 && value < 3.0, 31);
    }

    RandomSequence chance({7}, RandomStream::FromString("chance"));
    Check(!chance.RollMicro(0), 32);
    Check(chance.RollMicro(1'000'000), 33);
    Check(chance.RollMicro(1'000'001), 34);
    Check(RandomSequence({7}, RandomStream::FromString("chance")).RollMicro(500'000) ==
              RandomSequence({7}, RandomStream::FromString("chance")).RollMicro(500'000),
          35);

    RandomSequence weighted({42}, RandomStream::FromString("weighted"));
    const std::array<std::uint64_t, 4> weights{0, 4, 6, 0};
    const auto weighted_index = weighted.WeightedIndex(weights);
    Check(weighted_index && *weighted_index == 2, 36);
    const std::array<std::uint64_t, 0> empty_weights{};
    Check(!weighted.WeightedIndex(empty_weights), 37);
    const std::array<std::uint64_t, 3> zero_weights{0, 0, 0};
    Check(!weighted.WeightedIndex(zero_weights), 38);
    const std::array<std::uint64_t, 2> overflow_weights{std::numeric_limits<std::uint64_t>::max(), 1};
    Check(!weighted.WeightedIndex(overflow_weights), 39);
    const std::array<std::uint64_t, 1> single_weight{9};
    Check(weighted.WeightedIndex(single_weight).value_or(99) == 0, 40);

    std::vector<int> shuffled{1, 2, 3, 4, 5};
    RandomSequence({42}, RandomStream::FromString("shuffle.items")).Shuffle(shuffled);
    Check((shuffled == std::vector<int>{2, 5, 3, 1, 4}), 41);
    std::vector<int> sorted = shuffled;
    std::sort(sorted.begin(), sorted.end());
    Check((sorted == std::vector<int>{1, 2, 3, 4, 5}), 42);
    std::vector<int> empty;
    RandomSequence({1}, RandomStream::FromString("shuffle.items")).Shuffle(empty);
    Check(empty.empty(), 43);
    std::array<int, 1> one{7};
    RandomSequence({1}, RandomStream::FromString("shuffle.items")).Shuffle(one);
    Check(one[0] == 7, 44);

    RandomSequence original({42}, RandomStream::FromString("snapshot"));
    const auto before = original.NextU64();
    const auto snapshot = original.CaptureSnapshot();
    const auto after_snapshot = original.NextU64();
    RandomSequence restored(snapshot);
    Check(restored.NextU64() == after_snapshot, 45);
    restored.RestoreSnapshot({RandomSeed{42}, RandomStream::FromString("snapshot"), 0});
    Check(restored.NextU64() == before, 46);

    RandomSequence exhausted({42}, combat, std::numeric_limits<std::uint64_t>::max() - 1);
    (void)exhausted.NextU64();
    Check(exhausted.IsExhausted() && exhausted.Sequence() == std::numeric_limits<std::uint64_t>::max(), 47);
    const auto exhausted_snapshot = exhausted.CaptureSnapshot();
    RandomSequence exhausted_restore(exhausted_snapshot);
    Check(exhausted_restore.IsExhausted(), 48);

    return 0;
}