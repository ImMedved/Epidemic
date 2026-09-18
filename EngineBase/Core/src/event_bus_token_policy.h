#pragma once

#include <Epidemic/Core/event_bus.h>

#include <limits>

namespace epidemic::core::events::detail
{
// Zero is the exhausted/invalid sentinel; every non-zero value can be allocated once.
[[nodiscard]] constexpr bool CanAllocateHandlerToken(IEventBus::HandlerToken next_token) noexcept
{
    return next_token != 0;
}

[[nodiscard]] constexpr IEventBus::HandlerToken AdvanceHandlerToken(IEventBus::HandlerToken current) noexcept
{
    return current == std::numeric_limits<IEventBus::HandlerToken>::max() ? 0 : current + 1;
}
} // namespace epidemic::core::events::detail
