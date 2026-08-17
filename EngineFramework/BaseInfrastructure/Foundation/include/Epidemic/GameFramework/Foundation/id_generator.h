#pragma once

#include "Epidemic/GameFramework/Foundation/ids.h"

#include <cstdint>
#include <limits>
#include <string_view>

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
    explicit constexpr MonotonicIdGenerator(std::uint64_t scope) noexcept : scope_(NormalizeScope(scope)) {}
    explicit constexpr MonotonicIdGenerator(IdScopeId scope) noexcept : scope_(NormalizeScope(scope.Raw())) {}

    [[nodiscard]] static constexpr MonotonicIdGenerator FromScopeName(std::string_view stable_scope_name) noexcept
    {
        return MonotonicIdGenerator(IdScopeId::FromString(stable_scope_name));
    }

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

    [[nodiscard]] constexpr bool IsExhausted() const noexcept { return next_ == 0; }
    [[nodiscard]] constexpr IdScopeId Scope() const noexcept { return IdScopeId::FromRaw(scope_); }
    [[nodiscard]] Snapshot GetSnapshot() const noexcept { return Snapshot{scope_, next_}; }

    [[nodiscard]] static constexpr bool IsValidSnapshot(Snapshot snapshot) noexcept { return snapshot.scope != 0; }

    void Restore(Snapshot snapshot) noexcept
    {
        scope_ = NormalizeScope(snapshot.scope);
        next_ = snapshot.next;
    }

  private:
    [[nodiscard]] static constexpr std::uint64_t NormalizeScope(std::uint64_t scope) noexcept
    {
        return scope == 0 ? 1 : scope;
    }

    std::uint64_t scope_ = 1;
    std::uint64_t next_ = 1;
};
} // namespace epidemic::gameplay