#pragma once

#include <Epidemic/Input/iinput_system.h>

#include <vector>

namespace epidemic::input
{
class InputSystem final : public IInputSystem
{
  public:
    void QueuePlatformEvent(const epidemic::platform::PlatformEvent &event) override;
    void QueuePlatformEvents(std::span<const epidemic::platform::PlatformEvent> events) override;
    void PublishSnapshot() override;
    void Reset() noexcept override;

    [[nodiscard]] const InputSnapshot &CurrentSnapshot() const noexcept override;
    [[nodiscard]] std::span<const InputEvent> CurrentEvents() const noexcept override;

  private:
    KeyboardState keyboard_state_{};
    MouseState mouse_state_{};
    InputSnapshot current_snapshot_{};
    std::vector<epidemic::platform::PlatformEvent> pending_platform_events_{};
    std::vector<InputEvent> current_events_{};
    std::uint64_t next_update_index_{};
};
} // namespace epidemic::input
