#include "Epidemic/GameFramework/SupportRandom/deterministic_random.h"

namespace epidemic::gameplay::random
{
std::uint64_t StableMix(std::uint64_t value) noexcept
{
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30u)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27u)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31u);
}

RandomSeed DeriveSeed(RandomSeed root, RandomStream stream, std::uint64_t salt) noexcept
{
    return RandomSeed{StableMix(root.value ^ StableMix(stream.id.Raw()) ^ StableMix(salt))};
}

RandomSequence::RandomSequence(RandomSeed seed, RandomStream stream, std::uint64_t sequence) noexcept
    : seed_(seed), stream_(stream), sequence_(sequence)
{
}

std::uint64_t RandomSequence::NextU64() noexcept
{
    const auto counter = sequence_++;
    return StableMix(seed_.value ^ StableMix(stream_.id.Raw()) ^ StableMix(counter));
}

std::uint64_t RandomSequence::Uniform(std::uint64_t exclusive_max) noexcept
{
    if (exclusive_max <= 1)
    {
        return 0;
    }
    // Lemire-style multiply-high without relying on non-standard 128-bit types.
    // Rejection on modulo is deterministic and sufficient for gameplay table sizes.
    const auto limit = UINT64_MAX - (UINT64_MAX % exclusive_max);
    for (;;)
    {
        const auto value = NextU64();
        if (value < limit)
        {
            return value % exclusive_max;
        }
    }
}

bool RandomSequence::RollMicro(std::uint32_t chance_micro) noexcept
{
    if (chance_micro == 0)
    {
        return false;
    }
    if (chance_micro >= 1'000'000u)
    {
        return true;
    }
    return Uniform(1'000'000u) < chance_micro;
}
} // namespace epidemic::gameplay::random
