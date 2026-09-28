#pragma once

#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace epidemic::gameplay::random
{
struct RandomSeed
{
    std::uint64_t value = 0;
    [[nodiscard]] constexpr bool operator==(const RandomSeed&) const noexcept = default;
};

struct RandomStream
{
    TypeId id{};
    [[nodiscard]] static constexpr RandomStream FromString(std::string_view name) noexcept
    {
        return RandomStream{TypeId::FromString(name)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return id.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const RandomStream&) const noexcept = default;
};

struct RandomSequenceSnapshot
{
    RandomSeed seed{};
    RandomStream stream{};
    std::uint64_t sequence = 0;
};

[[nodiscard]] std::uint64_t StableMix(std::uint64_t value) noexcept;
[[nodiscard]] RandomSeed DeriveSeed(RandomSeed root, RandomStream stream, std::uint64_t salt = 0) noexcept;

class RandomSequence
{
  public:
    RandomSequence(RandomSeed seed, RandomStream stream, std::uint64_t sequence = 0);
    [[nodiscard]] static constexpr bool IsValidSnapshot(const RandomSequenceSnapshot& snapshot) noexcept
    {
        return snapshot.stream.IsValid();
    }
    [[nodiscard]] static std::optional<RandomSequence> TryFromSnapshot(RandomSequenceSnapshot snapshot) noexcept;

    [[nodiscard]] std::optional<std::uint64_t> TryNextU64() noexcept;
    [[nodiscard]] std::optional<std::uint64_t> TryUniform(std::uint64_t exclusive_max) noexcept;
    [[nodiscard]] std::optional<std::uint64_t> TryUniformRange(std::uint64_t minimum,
                                                               std::uint64_t maximum_exclusive) noexcept;
    [[nodiscard]] std::optional<double> TryUniform01() noexcept;
    [[nodiscard]] std::optional<double> TryUniformReal(double minimum, double maximum) noexcept;
    [[nodiscard]] std::optional<bool> TryRollMicro(std::uint32_t chance_micro) noexcept;

    // Unchecked convenience methods are only for call sites that have already proven their preconditions.
    // A violated precondition terminates instead of returning a plausible fallback value in release builds.
    [[nodiscard]] std::uint64_t NextU64Unchecked() noexcept;
    [[nodiscard]] std::uint64_t UniformUnchecked(std::uint64_t exclusive_max) noexcept;
    [[nodiscard]] std::uint64_t UniformRangeUnchecked(std::uint64_t minimum,
                                                      std::uint64_t maximum_exclusive) noexcept;
    [[nodiscard]] double Uniform01Unchecked() noexcept;
    [[nodiscard]] double UniformRealUnchecked(double minimum, double maximum) noexcept;
    [[nodiscard]] bool RollMicroUnchecked(std::uint32_t chance_micro) noexcept;
    [[nodiscard]] std::optional<std::size_t> WeightedIndex(std::span<const std::uint64_t> weights) noexcept;

    template <typename T> void ShuffleUnchecked(std::span<T> values) noexcept(std::is_nothrow_swappable_v<T>)
    {
        if (values.size() < 2)
        {
            return;
        }

        for (std::size_t index = values.size() - 1; index > 0; --index)
        {
            const auto swap_index = static_cast<std::size_t>(UniformUnchecked(static_cast<std::uint64_t>(index + 1)));
            using std::swap;
            swap(values[index], values[swap_index]);
        }
    }

    template <typename TContainer> void ShuffleUnchecked(TContainer& values) noexcept(noexcept(ShuffleUnchecked(std::span{values.data(), values.size()})))
    {
        ShuffleUnchecked(std::span{values.data(), values.size()});
    }

    [[nodiscard]] RandomSequenceSnapshot CaptureSnapshot() const noexcept { return {seed_, stream_, sequence_}; }
    [[nodiscard]] bool TryRestoreSnapshot(RandomSequenceSnapshot snapshot) noexcept;

    [[nodiscard]] std::uint64_t Sequence() const noexcept { return sequence_; }
    [[nodiscard]] bool IsExhausted() const noexcept { return sequence_ == std::numeric_limits<std::uint64_t>::max(); }

  private:
    RandomSeed seed_{};
    RandomStream stream_{};
    std::uint64_t sequence_ = 0;
};
} // namespace epidemic::gameplay::random