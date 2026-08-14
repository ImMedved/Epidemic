#pragma once

#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <cstdint>
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

[[nodiscard]] std::uint64_t StableMix(std::uint64_t value) noexcept;
[[nodiscard]] RandomSeed DeriveSeed(RandomSeed root, RandomStream stream, std::uint64_t salt = 0) noexcept;

class RandomSequence
{
  public:
    RandomSequence(RandomSeed seed, RandomStream stream, std::uint64_t sequence = 0) noexcept;

    [[nodiscard]] std::uint64_t NextU64() noexcept;
    [[nodiscard]] std::uint64_t Uniform(std::uint64_t exclusive_max) noexcept;
    [[nodiscard]] bool RollMicro(std::uint32_t chance_micro) noexcept;
    [[nodiscard]] std::uint64_t Sequence() const noexcept { return sequence_; }

  private:
    RandomSeed seed_{};
    RandomStream stream_{};
    std::uint64_t sequence_ = 0;
};
} // namespace epidemic::gameplay::random
