#include "Epidemic/GameFramework/SupportRandom/deterministic_random.h"

#include <cmath>

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

RandomSequence::RandomSequence(RandomSequenceSnapshot snapshot) noexcept
    : seed_(snapshot.seed), stream_(snapshot.stream), sequence_(snapshot.sequence)
{
}

std::optional<std::uint64_t> RandomSequence::TryNextU64() noexcept
{
    if (IsExhausted())
    {
        return std::nullopt;
    }
    const auto counter = sequence_;
    ++sequence_;
    return StableMix(seed_.value ^ StableMix(stream_.id.Raw()) ^ StableMix(counter));
}

std::uint64_t RandomSequence::NextU64() noexcept
{
    const auto value = TryNextU64();
    assert(value.has_value());
    return value.value_or(0);
}

std::optional<std::uint64_t> RandomSequence::TryUniform(std::uint64_t exclusive_max) noexcept
{
    if (exclusive_max == 0)
    {
        return std::nullopt;
    }
    if (exclusive_max == 1)
    {
        return std::uint64_t{0};
    }

    const auto limit = UINT64_MAX - (UINT64_MAX % exclusive_max);
    for (;;)
    {
        const auto value = TryNextU64();
        if (!value)
        {
            return std::nullopt;
        }
        if (*value < limit)
        {
            return *value % exclusive_max;
        }
    }
}

std::uint64_t RandomSequence::Uniform(std::uint64_t exclusive_max) noexcept
{
    const auto value = TryUniform(exclusive_max);
    assert(value.has_value());
    return value.value_or(0);
}

std::uint64_t RandomSequence::UniformRange(std::uint64_t minimum, std::uint64_t maximum_exclusive) noexcept
{
    assert(maximum_exclusive > minimum);
    if (maximum_exclusive <= minimum)
    {
        return minimum;
    }
    return minimum + Uniform(maximum_exclusive - minimum);
}

double RandomSequence::Uniform01() noexcept
{
    constexpr double kUnit = 1.0 / static_cast<double>(std::uint64_t{1} << 53u);
    return static_cast<double>(NextU64() >> 11u) * kUnit;
}

double RandomSequence::UniformReal(double minimum, double maximum) noexcept
{
    assert(std::isfinite(minimum));
    assert(std::isfinite(maximum));
    assert(maximum > minimum);
    if (!(maximum > minimum) || !std::isfinite(minimum) || !std::isfinite(maximum))
    {
        return minimum;
    }
    return minimum + (maximum - minimum) * Uniform01();
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

std::optional<std::size_t> RandomSequence::WeightedIndex(std::span<const std::uint64_t> weights) noexcept
{
    std::uint64_t total = 0;
    for (const auto weight : weights)
    {
        if (weight > std::numeric_limits<std::uint64_t>::max() - total)
        {
            return std::nullopt;
        }
        total += weight;
    }

    if (total == 0)
    {
        return std::nullopt;
    }

    auto roll = Uniform(total);
    for (std::size_t index = 0; index < weights.size(); ++index)
    {
        if (roll < weights[index])
        {
            return index;
        }
        roll -= weights[index];
    }
    return std::nullopt;
}

void RandomSequence::RestoreSnapshot(RandomSequenceSnapshot snapshot) noexcept
{
    seed_ = snapshot.seed;
    stream_ = snapshot.stream;
    sequence_ = snapshot.sequence;
}
} // namespace epidemic::gameplay::random