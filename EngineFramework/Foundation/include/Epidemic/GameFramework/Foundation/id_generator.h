#pragma once

#include "Epidemic/GameFramework/Foundation/ids.h"

#include <cstdint>
#include <limits>

namespace epidemic::gameplay
{
template <typename TId> class MonotonicIdGenerator
{
  public:
    struct Snapshot
    {
        std::uint64_t scope = 0;
        std::uint64_t next = 1;
    };

    constexpr MonotonicIdGenerator() noexcept = default;
    explicit constexpr MonotonicIdGenerator(std::uint64_t scope) noexcept : scope_(scope == 0 ? 1 : scope) {}

    [[nodiscard]] TId Next() noexcept
    {
        if (next_ == 0)
        {
            return {};
        }
        const auto result = TId::FromRaw(scope_, next_);
        if (next_ == std::numeric_limits<std::uint64_t>::max())
        {
            next_ = 0;
        }
        else
        {
            ++next_;
        }
        return result;
    }

    [[nodiscard]] Snapshot GetSnapshot() const noexcept { return Snapshot{scope_, next_}; }

    void Restore(Snapshot snapshot) noexcept
    {
        scope_ = snapshot.scope == 0 ? 1 : snapshot.scope;
        next_ = snapshot.next == 0 ? 1 : snapshot.next;
    }

  private:
    std::uint64_t scope_ = 1;
    std::uint64_t next_ = 1;
};
} // namespace epidemic::gameplay
