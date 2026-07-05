#include <Epidemic/Input/input_system.h>

#include <cstdint>

namespace epidemic::input
{
namespace
{
[[nodiscard]] KeyCode ToKeyCode(std::uint32_t key_code) noexcept
{
    if (key_code <= 0xFFu)
    {
        return static_cast<KeyCode>(static_cast<std::uint16_t>(key_code));
    }

    return KeyCode::Unknown;
}

[[nodiscard]] MouseButton ToMouseButton(std::uint8_t mouse_button) noexcept
{
    if (mouse_button < static_cast<std::uint8_t>(MouseButton::Count))
    {
        return static_cast<MouseButton>(mouse_button);
    }

    return MouseButton::Unknown;
}
} // namespace

void InputSystem::QueuePlatformEvent(const epidemic::platform::PlatformEvent &event)
{
    pending_platform_events_.push_back(event);
}

void InputSystem::QueuePlatformEvents(std::span<const epidemic::platform::PlatformEvent> events)
{
    pending_platform_events_.insert(pending_platform_events_.end(), events.begin(), events.end());
}

void InputSystem::PublishSnapshot()
{
    keyboard_state_.ClearTransient();
    mouse_state_.ClearTransient();
    current_events_.clear();

    for (const auto &event : pending_platform_events_)
    {
        switch (event.type)
        {
        case epidemic::platform::PlatformEventType::WindowFocusChanged:
            mouse_state_.SetFocus(event.focused);
            current_events_.push_back(InputEvent{InputEventType::FocusChanged, KeyCode::Unknown, MouseButton::Unknown, false,
                                                 event.focused, mouse_state_.HasCapture()});
            if (!event.focused)
            {
                keyboard_state_.ClearAll();
                mouse_state_.SetCapture(false);
                mouse_state_.ClearButtons();
                mouse_state_.ClearTransient();
            }
            break;
        case epidemic::platform::PlatformEventType::MouseCaptureChanged:
            mouse_state_.SetCapture(event.captured);
            current_events_.push_back(InputEvent{InputEventType::CaptureChanged, KeyCode::Unknown, MouseButton::Unknown, false,
                                                 mouse_state_.HasFocus(), event.captured});
            break;
        case epidemic::platform::PlatformEventType::KeyPressed:
        {
            const auto key_code = ToKeyCode(event.key_code);
            if (!IsKnownKeyCode(key_code))
            {
                break;
            }

            if (!keyboard_state_.IsKeyDown(key_code))
            {
                keyboard_state_.MarkPressed(key_code);
            }

            keyboard_state_.SetKeyDown(key_code, true);
            current_events_.push_back(InputEvent{InputEventType::KeyPressed, key_code, MouseButton::Unknown, event.repeated,
                                                 mouse_state_.HasFocus(), mouse_state_.HasCapture()});
            break;
        }
        case epidemic::platform::PlatformEventType::KeyReleased:
        {
            const auto key_code = ToKeyCode(event.key_code);
            if (!IsKnownKeyCode(key_code))
            {
                break;
            }

            keyboard_state_.MarkReleased(key_code);
            keyboard_state_.SetKeyDown(key_code, false);
            current_events_.push_back(InputEvent{InputEventType::KeyReleased, key_code, MouseButton::Unknown, false,
                                                 mouse_state_.HasFocus(), mouse_state_.HasCapture()});
            break;
        }
        case epidemic::platform::PlatformEventType::MouseMoved:
            mouse_state_.SetPosition(event.mouse_x, event.mouse_y);
            current_events_.push_back(InputEvent{InputEventType::MouseMoved, KeyCode::Unknown, MouseButton::Unknown, false,
                                                 mouse_state_.HasFocus(), mouse_state_.HasCapture(), event.mouse_x,
                                                 event.mouse_y, mouse_state_.DeltaX(), mouse_state_.DeltaY()});
            break;
        case epidemic::platform::PlatformEventType::MouseButtonPressed:
        {
            const auto mouse_button = ToMouseButton(event.mouse_button);
            if (!IsKnownMouseButton(mouse_button))
            {
                break;
            }

            if (!mouse_state_.IsButtonDown(mouse_button))
            {
                mouse_state_.MarkPressed(mouse_button);
            }

            mouse_state_.SetButtonDown(mouse_button, true);
            current_events_.push_back(InputEvent{InputEventType::MouseButtonPressed, KeyCode::Unknown, mouse_button, false,
                                                 mouse_state_.HasFocus(), mouse_state_.HasCapture(), event.mouse_x,
                                                 event.mouse_y});
            break;
        }
        case epidemic::platform::PlatformEventType::MouseButtonReleased:
        {
            const auto mouse_button = ToMouseButton(event.mouse_button);
            if (!IsKnownMouseButton(mouse_button))
            {
                break;
            }

            mouse_state_.MarkReleased(mouse_button);
            mouse_state_.SetButtonDown(mouse_button, false);
            current_events_.push_back(InputEvent{InputEventType::MouseButtonReleased, KeyCode::Unknown, mouse_button, false,
                                                 mouse_state_.HasFocus(), mouse_state_.HasCapture(), event.mouse_x,
                                                 event.mouse_y});
            break;
        }
        case epidemic::platform::PlatformEventType::MouseWheel:
            mouse_state_.AddWheelDelta(event.wheel_delta);
            current_events_.push_back(InputEvent{InputEventType::MouseWheel, KeyCode::Unknown, MouseButton::Unknown, false,
                                                 mouse_state_.HasFocus(), mouse_state_.HasCapture(), event.mouse_x,
                                                 event.mouse_y, 0, 0, event.wheel_delta});
            break;
        case epidemic::platform::PlatformEventType::None:
        case epidemic::platform::PlatformEventType::WindowCloseRequested:
        case epidemic::platform::PlatformEventType::WindowResized:
        case epidemic::platform::PlatformEventType::WindowMinimized:
        case epidemic::platform::PlatformEventType::WindowRestored:
            break;
        }
    }

    pending_platform_events_.clear();
    current_snapshot_.update_index = next_update_index_++;
    current_snapshot_.keyboard = keyboard_state_;
    current_snapshot_.mouse = mouse_state_;
}

void InputSystem::Reset() noexcept
{
    keyboard_state_.ClearAll();
    mouse_state_ = MouseState{};
    current_snapshot_ = InputSnapshot{};
    pending_platform_events_.clear();
    current_events_.clear();
    next_update_index_ = 0;
}

const InputSnapshot &InputSystem::CurrentSnapshot() const noexcept
{
    return current_snapshot_;
}

std::span<const InputEvent> InputSystem::CurrentEvents() const noexcept
{
    return std::span<const InputEvent>(current_events_.data(), current_events_.size());
}
} // namespace epidemic::input


