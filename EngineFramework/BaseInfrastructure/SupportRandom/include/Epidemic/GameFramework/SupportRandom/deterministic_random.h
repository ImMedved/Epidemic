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
    RandomSequence(RandomSeed seed, RandomStream stream, std::uint64_t sequence = 0) noexcept;
    explicit RandomSequence(RandomSequenceSnapshot snapshot) noexcept;

    [[nodiscard]] std::optional<std::uint64_t> TryNextU64() noexcept;
    [[nodiscard]] std::optional<std::uint64_t> TryUniform(std::uint64_t exclusive_max) noexcept;
    [[nodiscard]] std::uint64_t NextU64() noexcept;
    [[nodiscard]] std::uint64_t Uniform(std::uint64_t exclusive_max) noexcept;
    [[nodiscard]] std::uint64_t UniformRange(std::uint64_t minimum, std::uint64_t maximum_exclusive) noexcept;
    [[nodiscard]] double Uniform01() noexcept;
    [[nodiscard]] double UniformReal(double minimum, double maximum) noexcept;
    [[nodiscard]] bool RollMicro(std::uint32_t chance_micro) noexcept;
    [[nodiscard]] std::optional<std::size_t> WeightedIndex(std::span<const std::uint64_t> weights) noexcept;

    template <typename T> void Shuffle(std::span<T> values) noexcept
    {
        if (values.size() < 2)
        {
            return;
        }

        for (std::size_t index = values.size() - 1; index > 0; --index)
        {
            const auto swap_index = static_cast<std::size_t>(Uniform(static_cast<std::uint64_t>(index + 1)));
            using std::swap;
            swap(values[index], values[swap_index]);
        }
    }

    template <typename TContainer> void Shuffle(TContainer& values) noexcept
    {
        Shuffle(std::span{values.data(), values.size()});
    }

    [[nodiscard]] RandomSequenceSnapshot CaptureSnapshot() const noexcept { return {seed_, stream_, sequence_}; }
    void RestoreSnapshot(RandomSequenceSnapshot snapshot) noexcept;

    [[nodiscard]] std::uint64_t Sequence() const noexcept { return sequence_; }
    [[nodiscard]] bool IsExhausted() const noexcept { return sequence_ == std::numeric_limits<std::uint64_t>::max(); }

  private:
    RandomSeed seed_{};
    RandomStream stream_{};
    std::uint64_t sequence_ = 0;
};
} // namespace epidemic::gameplay::random