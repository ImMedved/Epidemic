#include "Epidemic/GameFramework/SupportRandom/deterministic_random.h"

#include <cmath>
#include <exception>
#include <stdexcept>

namespace epidemic::gameplay::random
{
namespace
{
template <typename T> T RequireValue(std::optional<T> value) noexcept
{
    if (!value.has_value())
    {
        std::terminate();
    }
    return *value;
}
} // namespace

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

RandomSequence::RandomSequence(RandomSeed seed, RandomStream stream, std::uint64_t sequence)
    : seed_(seed), stream_(stream), sequence_(sequence)
{
    if (!stream_.IsValid())
    {
        throw std::invalid_argument("random stream must be valid");
    }
}

std::optional<RandomSequence> RandomSequence::TryFromSnapshot(RandomSequenceSnapshot snapshot) noexcept
{
    if (!IsValidSnapshot(snapshot))
    {
        return std::nullopt;
    }
    return RandomSequence(snapshot.seed, snapshot.stream, snapshot.sequence);
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

std::optional<std::uint64_t> RandomSequence::TryUniformRange(std::uint64_t minimum,
                                                              std::uint64_t maximum_exclusive) noexcept
{
    if (maximum_exclusive <= minimum)
    {
        return std::nullopt;
    }
    const auto offset = TryUniform(maximum_exclusive - minimum);
    if (!offset)
    {
        return std::nullopt;
    }
    return minimum + *offset;
}

std::optional<double> RandomSequence::TryUniform01() noexcept
{
    const auto value = TryNextU64();
    if (!value)
    {
        return std::nullopt;
    }
    constexpr double kUnit = 1.0 / static_cast<double>(std::uint64_t{1} << 53u);
    return static_cast<double>(*value >> 11u) * kUnit;
}

std::optional<double> RandomSequence::TryUniformReal(double minimum, double maximum) noexcept
{
    if (!(maximum > minimum) || !std::isfinite(minimum) || !std::isfinite(maximum))
    {
        return std::nullopt;
    }
    const auto span = maximum - minimum;
    if (!std::isfinite(span))
    {
        return std::nullopt;
    }
    const auto unit = TryUniform01();
    if (!unit)
    {
        return std::nullopt;
    }
    auto value = minimum + span * *unit;
    if (!std::isfinite(value))
    {
        return std::nullopt;
    }
    // Floating-point rounding can turn a mathematically half-open sample into the
    // nominal exclusive upper endpoint. Preserve the public [minimum, maximum) contract.
    if (value >= maximum)
    {
        value = std::nextafter(maximum, minimum);
        if (!(value >= minimum) || !(value < maximum))
        {
            return std::nullopt;
        }
    }
    return value;
}

std::optional<bool> RandomSequence::TryRollMicro(std::uint32_t chance_micro) noexcept
{
    if (chance_micro == 0)
    {
        return false;
    }
    if (chance_micro > 1'000'000u)
    {
        return std::nullopt;
    }
    if (chance_micro == 1'000'000u)
    {
        return true;
    }
    const auto roll = TryUniform(1'000'000u);
    if (!roll)
    {
        return std::nullopt;
    }
    return *roll < chance_micro;
}

std::uint64_t RandomSequence::NextU64Unchecked() noexcept
{
    return RequireValue(TryNextU64());
}

std::uint64_t RandomSequence::UniformUnchecked(std::uint64_t exclusive_max) noexcept
{
    return RequireValue(TryUniform(exclusive_max));
}

std::uint64_t RandomSequence::UniformRangeUnchecked(std::uint64_t minimum,
                                                     std::uint64_t maximum_exclusive) noexcept
{
    return RequireValue(TryUniformRange(minimum, maximum_exclusive));
}

double RandomSequence::Uniform01Unchecked() noexcept
{
    return RequireValue(TryUniform01());
}

double RandomSequence::UniformRealUnchecked(double minimum, double maximum) noexcept
{
    return RequireValue(TryUniformReal(minimum, maximum));
}

bool RandomSequence::RollMicroUnchecked(std::uint32_t chance_micro) noexcept
{
    return RequireValue(TryRollMicro(chance_micro));
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

    auto roll = TryUniform(total);
    if (!roll)
    {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < weights.size(); ++index)
    {
        if (*roll < weights[index])
        {
            return index;
        }
        *roll -= weights[index];
    }
    return std::nullopt;
}

bool RandomSequence::TryRestoreSnapshot(RandomSequenceSnapshot snapshot) noexcept
{
    if (!IsValidSnapshot(snapshot))
    {
        return false;
    }
    seed_ = snapshot.seed;
    stream_ = snapshot.stream;
    sequence_ = snapshot.sequence;
    return true;
}
} // namespace epidemic::gameplay::random
