#pragma once

#include <Epidemic/Input/input_event.h>
#include <Epidemic/Input/input_snapshot.h>
#include <Epidemic/Platform/platform_event.h>

#include <span>

namespace epidemic::input
{
class IInputSystem
{
  public:
    virtual ~IInputSystem() = default;

    virtual void QueuePlatformEvent(const epidemic::platform::PlatformEvent &event) = 0;
    virtual void QueuePlatformEvents(std::span<const epidemic::platform::PlatformEvent> events) = 0;
    virtual void PublishSnapshot() = 0;
    virtual void Reset() noexcept = 0;

    [[nodiscard]] virtual const InputSnapshot &CurrentSnapshot() const noexcept = 0;
    [[nodiscard]] virtual std::span<const InputEvent> CurrentEvents() const noexcept = 0;
};
} // namespace epidemic::input
