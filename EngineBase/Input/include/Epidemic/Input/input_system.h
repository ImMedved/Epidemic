#pragma once

#include <Epidemic/Input/iinput_system.h>

#include <vector>

namespace epidemic::input
{
// This file declares the baseline InputSystem implementation.
// The system buffers raw PlatformEvent values until PublishSnapshot() normalizes them into
// keyboard/mouse state and transient InputEvent records.

class InputSystem final : public IInputSystem
{
  public:
    // Queues one raw platform event for the next publish pass.
    void QueuePlatformEvent(const epidemic::platform::PlatformEvent &event) override;

    // Queues a batch of raw platform events for the next publish pass.
    void QueuePlatformEvents(std::span<const epidemic::platform::PlatformEvent> events) override;

    // Normalizes queued platform events into the current snapshot and current event list.
    void PublishSnapshot() override;

    // Resets all cached state and queued events.
    void Reset() noexcept override;

    // Returns the most recently published input snapshot.
    [[nodiscard]] const InputSnapshot &CurrentSnapshot() const noexcept override;

    // Returns the transient input events produced by the most recent publish.
    [[nodiscard]] std::span<const InputEvent> CurrentEvents() const noexcept override;

  private:
    KeyboardState keyboard_state_{};
    MouseState mouse_state_{};
    InputSnapshot current_snapshot_{};
    std::vector<epidemic::platform::PlatformEvent> pending_platform_events_{};
    std::vector<InputEvent> current_events_{};
    std::uint64_t next_update_index_{};
};
} 
