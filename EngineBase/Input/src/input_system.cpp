#include <Epidemic/Input/input_system.h>

#include "input_index_policy.h"

#include <cstdint>

namespace epidemic::input
{
// This file implements normalization from raw PlatformEvent values to frame-stable input state.
// Public API contracts live in input_system.h; helpers below convert raw platform payloads into normalized enums.

namespace
{
// Converts a raw platform key code into the normalized EngineBase key enum.
[[nodiscard]] KeyCode ToKeyCode(std::uint32_t key_code) noexcept
{
    if (key_code <= 0xFFu)
    {
        return static_cast<KeyCode>(static_cast<std::uint16_t>(key_code));
    }

    return KeyCode::Unknown;
}

// Converts a raw platform mouse-button id into the normalized EngineBase mouse-button enum.
[[nodiscard]] MouseButton ToMouseButton(std::uint8_t mouse_button) noexcept
{
    if (mouse_button < static_cast<std::uint8_t>(MouseButton::Count))
    {
        return static_cast<MouseButton>(mouse_button);
    }

    return MouseButton::Unknown;
}
} // namespace

// Buffers one raw platform event for the next PublishSnapshot() pass.
void InputSystem::QueuePlatformEvent(const epidemic::platform::PlatformEvent &event)
{
    pending_platform_events_.push_back(event);
}

// Buffers one raw platform event for the next PublishSnapshot() pass.
// Buffers a batch of raw platform events for the next PublishSnapshot() pass.
void InputSystem::QueuePlatformEvents(std::span<const epidemic::platform::PlatformEvent> events)
{
    pending_platform_events_.insert(pending_platform_events_.end(), events.begin(), events.end());
}

// Consumes all queued platform events, updates persistent state, and emits the transient event list.
void InputSystem::PublishSnapshot()
{
    auto candidate_keyboard = keyboard_state_;
    auto candidate_mouse = mouse_state_;
    candidate_keyboard.ClearTransient();
    candidate_mouse.ClearTransient();

    std::vector<InputEvent> candidate_events;
    candidate_events.reserve(pending_platform_events_.size());

    for (const auto &event : pending_platform_events_)
    {
        switch (event.type)
        {
        case epidemic::platform::PlatformEventType::WindowFocusChanged:
            candidate_mouse.SetFocus(event.focused);
            candidate_events.push_back(InputEvent{InputEventType::FocusChanged, KeyCode::Unknown, MouseButton::Unknown, false,
                                                  event.focused, candidate_mouse.HasCapture()});
            if (!event.focused)
            {
                candidate_keyboard.ClearAll();
                candidate_mouse.SetCapture(false);
                candidate_mouse.ClearButtons();
                candidate_mouse.ClearTransient();
            }
            break;
        case epidemic::platform::PlatformEventType::MouseCaptureChanged:
            candidate_mouse.SetCapture(event.captured);
            candidate_events.push_back(InputEvent{InputEventType::CaptureChanged, KeyCode::Unknown, MouseButton::Unknown, false,
                                                  candidate_mouse.HasFocus(), event.captured});
            break;
        case epidemic::platform::PlatformEventType::KeyPressed:
        {
            const auto key_code = ToKeyCode(event.key_code);
            if (!IsKnownKeyCode(key_code))
            {
                break;
            }

            if (!candidate_keyboard.IsKeyDown(key_code))
            {
                candidate_keyboard.MarkPressed(key_code);
            }

            candidate_keyboard.SetKeyDown(key_code, true);
            candidate_events.push_back(InputEvent{InputEventType::KeyPressed, key_code, MouseButton::Unknown, event.repeated,
                                                  candidate_mouse.HasFocus(), candidate_mouse.HasCapture()});
            break;
        }
        case epidemic::platform::PlatformEventType::KeyReleased:
        {
            const auto key_code = ToKeyCode(event.key_code);
            if (!IsKnownKeyCode(key_code))
            {
                break;
            }

            if (candidate_keyboard.IsKeyDown(key_code))
            {
                candidate_keyboard.MarkReleased(key_code);
                candidate_keyboard.SetKeyDown(key_code, false);
            }
            candidate_events.push_back(InputEvent{InputEventType::KeyReleased, key_code, MouseButton::Unknown, false,
                                                  candidate_mouse.HasFocus(), candidate_mouse.HasCapture()});
            break;
        }
        case epidemic::platform::PlatformEventType::MouseMoved:
        {
            const auto previous_x = candidate_mouse.PositionX();
            const auto previous_y = candidate_mouse.PositionY();
            candidate_mouse.SetPosition(event.mouse_x, event.mouse_y);
            candidate_events.push_back(InputEvent{InputEventType::MouseMoved, KeyCode::Unknown, MouseButton::Unknown, false,
                                                  candidate_mouse.HasFocus(), candidate_mouse.HasCapture(), event.mouse_x,
                                                  event.mouse_y, detail::SaturatingDifference(event.mouse_x, previous_x),
                                                  detail::SaturatingDifference(event.mouse_y, previous_y)});
            break;
        }
        case epidemic::platform::PlatformEventType::MouseButtonPressed:
        {
            const auto mouse_button = ToMouseButton(event.mouse_button);
            if (!IsKnownMouseButton(mouse_button))
            {
                break;
            }

            if (!candidate_mouse.IsButtonDown(mouse_button))
            {
                candidate_mouse.MarkPressed(mouse_button);
            }

            candidate_mouse.SetButtonDown(mouse_button, true);
            candidate_events.push_back(InputEvent{InputEventType::MouseButtonPressed, KeyCode::Unknown, mouse_button, false,
                                                  candidate_mouse.HasFocus(), candidate_mouse.HasCapture(), event.mouse_x,
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

            if (candidate_mouse.IsButtonDown(mouse_button))
            {
                candidate_mouse.MarkReleased(mouse_button);
                candidate_mouse.SetButtonDown(mouse_button, false);
            }
            candidate_events.push_back(InputEvent{InputEventType::MouseButtonReleased, KeyCode::Unknown, mouse_button, false,
                                                  candidate_mouse.HasFocus(), candidate_mouse.HasCapture(), event.mouse_x,
                                                  event.mouse_y});
            break;
        }
        case epidemic::platform::PlatformEventType::MouseWheel:
            candidate_mouse.AddWheelDelta(event.wheel_delta);
            candidate_events.push_back(InputEvent{InputEventType::MouseWheel, KeyCode::Unknown, MouseButton::Unknown, false,
                                                  candidate_mouse.HasFocus(), candidate_mouse.HasCapture(), event.mouse_x,
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

    InputSnapshot candidate_snapshot;
    candidate_snapshot.update_index = next_update_index_;
    candidate_snapshot.keyboard = candidate_keyboard;
    candidate_snapshot.mouse = candidate_mouse;
    const auto candidate_next_update_index = detail::AdvanceUpdateIndex(next_update_index_);

    // All fallible work is complete. Publish the frame as one no-fail state transition so a failed
    // build leaves the previous snapshot/events and the pending raw event queue intact for retry.
    keyboard_state_ = candidate_keyboard;
    mouse_state_ = candidate_mouse;
    current_events_.swap(candidate_events);
    current_snapshot_ = candidate_snapshot;
    next_update_index_ = candidate_next_update_index;
    pending_platform_events_.clear();
}

// Clears all persistent and transient input state.
void InputSystem::Reset() noexcept
{
    keyboard_state_.ClearAll();
    mouse_state_ = MouseState{};
    current_snapshot_ = InputSnapshot{};
    pending_platform_events_.clear();
    current_events_.clear();
    next_update_index_ = 0;
}

// Returns the most recently published input snapshot.
const InputSnapshot &InputSystem::CurrentSnapshot() const noexcept
{
    return current_snapshot_;
}

// Returns a view over the transient events produced by the last publish.
std::span<const InputEvent> InputSystem::CurrentEvents() const noexcept
{
    return std::span<const InputEvent>(current_events_.data(), current_events_.size());
}
}
