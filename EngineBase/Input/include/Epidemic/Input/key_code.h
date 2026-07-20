#pragma once

#include <cstdint>
#include <string_view>

namespace epidemic::input
{
// This file defines the normalized keyboard keys recognized by EngineBase input code.
// Values intentionally mirror Win32 virtual-key codes for the current baseline backend.

enum class KeyCode : std::uint16_t
{
    Unknown = 0,
    Backspace = 8,
    Tab = 9,
    Enter = 13,
    Shift = 16,
    Control = 17,
    Alt = 18,
    Pause = 19,
    CapsLock = 20,
    Escape = 27,
    Space = 32,
    PageUp = 33,
    PageDown = 34,
    End = 35,
    Home = 36,
    Left = 37,
    Up = 38,
    Right = 39,
    Down = 40,
    Insert = 45,
    Delete = 46,
    Digit0 = 48,
    Digit1 = 49,
    Digit2 = 50,
    Digit3 = 51,
    Digit4 = 52,
    Digit5 = 53,
    Digit6 = 54,
    Digit7 = 55,
    Digit8 = 56,
    Digit9 = 57,
    A = 65,
    B = 66,
    C = 67,
    D = 68,
    E = 69,
    F = 70,
    G = 71,
    H = 72,
    I = 73,
    J = 74,
    K = 75,
    L = 76,
    M = 77,
    N = 78,
    O = 79,
    P = 80,
    Q = 81,
    R = 82,
    S = 83,
    T = 84,
    U = 85,
    V = 86,
    W = 87,
    X = 88,
    Y = 89,
    Z = 90,
    LeftWindows = 91,
    RightWindows = 92,
    Numpad0 = 96,
    Numpad1 = 97,
    Numpad2 = 98,
    Numpad3 = 99,
    Numpad4 = 100,
    Numpad5 = 101,
    Numpad6 = 102,
    Numpad7 = 103,
    Numpad8 = 104,
    Numpad9 = 105,
    Multiply = 106,
    Add = 107,
    Subtract = 109,
    Decimal = 110,
    Divide = 111,
    F1 = 112,
    F2 = 113,
    F3 = 114,
    F4 = 115,
    F5 = 116,
    F6 = 117,
    F7 = 118,
    F8 = 119,
    F9 = 120,
    F10 = 121,
    F11 = 122,
    F12 = 123,
    NumLock = 144,
    ScrollLock = 145,
    Semicolon = 186,
    Plus = 187,
    Comma = 188,
    Minus = 189,
    Period = 190,
    Slash = 191,
    BackQuote = 192,
    LeftBracket = 219,
    Backslash = 220,
    RightBracket = 221,
    Quote = 222,
};

// Returns true for concrete supported keys and false for Unknown or out-of-range values.
[[nodiscard]] constexpr bool IsKnownKeyCode(KeyCode key_code) noexcept
{
    switch (key_code)
    {
    case KeyCode::Backspace:
    case KeyCode::Tab:
    case KeyCode::Enter:
    case KeyCode::Shift:
    case KeyCode::Control:
    case KeyCode::Alt:
    case KeyCode::Pause:
    case KeyCode::CapsLock:
    case KeyCode::Escape:
    case KeyCode::Space:
    case KeyCode::PageUp:
    case KeyCode::PageDown:
    case KeyCode::End:
    case KeyCode::Home:
    case KeyCode::Left:
    case KeyCode::Up:
    case KeyCode::Right:
    case KeyCode::Down:
    case KeyCode::Insert:
    case KeyCode::Delete:
    case KeyCode::Digit0:
    case KeyCode::Digit1:
    case KeyCode::Digit2:
    case KeyCode::Digit3:
    case KeyCode::Digit4:
    case KeyCode::Digit5:
    case KeyCode::Digit6:
    case KeyCode::Digit7:
    case KeyCode::Digit8:
    case KeyCode::Digit9:
    case KeyCode::A:
    case KeyCode::B:
    case KeyCode::C:
    case KeyCode::D:
    case KeyCode::E:
    case KeyCode::F:
    case KeyCode::G:
    case KeyCode::H:
    case KeyCode::I:
    case KeyCode::J:
    case KeyCode::K:
    case KeyCode::L:
    case KeyCode::M:
    case KeyCode::N:
    case KeyCode::O:
    case KeyCode::P:
    case KeyCode::Q:
    case KeyCode::R:
    case KeyCode::S:
    case KeyCode::T:
    case KeyCode::U:
    case KeyCode::V:
    case KeyCode::W:
    case KeyCode::X:
    case KeyCode::Y:
    case KeyCode::Z:
    case KeyCode::LeftWindows:
    case KeyCode::RightWindows:
    case KeyCode::Numpad0:
    case KeyCode::Numpad1:
    case KeyCode::Numpad2:
    case KeyCode::Numpad3:
    case KeyCode::Numpad4:
    case KeyCode::Numpad5:
    case KeyCode::Numpad6:
    case KeyCode::Numpad7:
    case KeyCode::Numpad8:
    case KeyCode::Numpad9:
    case KeyCode::Multiply:
    case KeyCode::Add:
    case KeyCode::Subtract:
    case KeyCode::Decimal:
    case KeyCode::Divide:
    case KeyCode::F1:
    case KeyCode::F2:
    case KeyCode::F3:
    case KeyCode::F4:
    case KeyCode::F5:
    case KeyCode::F6:
    case KeyCode::F7:
    case KeyCode::F8:
    case KeyCode::F9:
    case KeyCode::F10:
    case KeyCode::F11:
    case KeyCode::F12:
    case KeyCode::NumLock:
    case KeyCode::ScrollLock:
    case KeyCode::Semicolon:
    case KeyCode::Plus:
    case KeyCode::Comma:
    case KeyCode::Minus:
    case KeyCode::Period:
    case KeyCode::Slash:
    case KeyCode::BackQuote:
    case KeyCode::LeftBracket:
    case KeyCode::Backslash:
    case KeyCode::RightBracket:
    case KeyCode::Quote:
        return true;
    case KeyCode::Unknown:
        return false;
    }

    return false;
}

// Converts a normalized key code to a stable diagnostic name.
[[nodiscard]] inline std::string_view ToString(KeyCode key_code) noexcept
{
    switch (key_code)
    {
    case KeyCode::Unknown:
        return "Unknown";
    case KeyCode::Backspace:
        return "Backspace";
    case KeyCode::Tab:
        return "Tab";
    case KeyCode::Enter:
        return "Enter";
    case KeyCode::Shift:
        return "Shift";
    case KeyCode::Control:
        return "Control";
    case KeyCode::Alt:
        return "Alt";
    case KeyCode::Pause:
        return "Pause";
    case KeyCode::CapsLock:
        return "CapsLock";
    case KeyCode::Escape:
        return "Escape";
    case KeyCode::Space:
        return "Space";
    case KeyCode::PageUp:
        return "PageUp";
    case KeyCode::PageDown:
        return "PageDown";
    case KeyCode::End:
        return "End";
    case KeyCode::Home:
        return "Home";
    case KeyCode::Left:
        return "Left";
    case KeyCode::Up:
        return "Up";
    case KeyCode::Right:
        return "Right";
    case KeyCode::Down:
        return "Down";
    case KeyCode::Insert:
        return "Insert";
    case KeyCode::Delete:
        return "Delete";
    case KeyCode::Digit0:
        return "0";
    case KeyCode::Digit1:
        return "1";
    case KeyCode::Digit2:
        return "2";
    case KeyCode::Digit3:
        return "3";
    case KeyCode::Digit4:
        return "4";
    case KeyCode::Digit5:
        return "5";
    case KeyCode::Digit6:
        return "6";
    case KeyCode::Digit7:
        return "7";
    case KeyCode::Digit8:
        return "8";
    case KeyCode::Digit9:
        return "9";
    case KeyCode::A:
        return "A";
    case KeyCode::B:
        return "B";
    case KeyCode::C:
        return "C";
    case KeyCode::D:
        return "D";
    case KeyCode::E:
        return "E";
    case KeyCode::F:
        return "F";
    case KeyCode::G:
        return "G";
    case KeyCode::H:
        return "H";
    case KeyCode::I:
        return "I";
    case KeyCode::J:
        return "J";
    case KeyCode::K:
        return "K";
    case KeyCode::L:
        return "L";
    case KeyCode::M:
        return "M";
    case KeyCode::N:
        return "N";
    case KeyCode::O:
        return "O";
    case KeyCode::P:
        return "P";
    case KeyCode::Q:
        return "Q";
    case KeyCode::R:
        return "R";
    case KeyCode::S:
        return "S";
    case KeyCode::T:
        return "T";
    case KeyCode::U:
        return "U";
    case KeyCode::V:
        return "V";
    case KeyCode::W:
        return "W";
    case KeyCode::X:
        return "X";
    case KeyCode::Y:
        return "Y";
    case KeyCode::Z:
        return "Z";
    case KeyCode::LeftWindows:
        return "LeftWindows";
    case KeyCode::RightWindows:
        return "RightWindows";
    case KeyCode::Numpad0:
        return "Numpad0";
    case KeyCode::Numpad1:
        return "Numpad1";
    case KeyCode::Numpad2:
        return "Numpad2";
    case KeyCode::Numpad3:
        return "Numpad3";
    case KeyCode::Numpad4:
        return "Numpad4";
    case KeyCode::Numpad5:
        return "Numpad5";
    case KeyCode::Numpad6:
        return "Numpad6";
    case KeyCode::Numpad7:
        return "Numpad7";
    case KeyCode::Numpad8:
        return "Numpad8";
    case KeyCode::Numpad9:
        return "Numpad9";
    case KeyCode::Multiply:
        return "Multiply";
    case KeyCode::Add:
        return "Add";
    case KeyCode::Subtract:
        return "Subtract";
    case KeyCode::Decimal:
        return "Decimal";
    case KeyCode::Divide:
        return "Divide";
    case KeyCode::F1:
        return "F1";
    case KeyCode::F2:
        return "F2";
    case KeyCode::F3:
        return "F3";
    case KeyCode::F4:
        return "F4";
    case KeyCode::F5:
        return "F5";
    case KeyCode::F6:
        return "F6";
    case KeyCode::F7:
        return "F7";
    case KeyCode::F8:
        return "F8";
    case KeyCode::F9:
        return "F9";
    case KeyCode::F10:
        return "F10";
    case KeyCode::F11:
        return "F11";
    case KeyCode::F12:
        return "F12";
    case KeyCode::NumLock:
        return "NumLock";
    case KeyCode::ScrollLock:
        return "ScrollLock";
    case KeyCode::Semicolon:
        return "Semicolon";
    case KeyCode::Plus:
        return "Plus";
    case KeyCode::Comma:
        return "Comma";
    case KeyCode::Minus:
        return "Minus";
    case KeyCode::Period:
        return "Period";
    case KeyCode::Slash:
        return "Slash";
    case KeyCode::BackQuote:
        return "BackQuote";
    case KeyCode::LeftBracket:
        return "LeftBracket";
    case KeyCode::Backslash:
        return "Backslash";
    case KeyCode::RightBracket:
        return "RightBracket";
    case KeyCode::Quote:
        return "Quote";
    }

    return "Unknown";
}
} 

