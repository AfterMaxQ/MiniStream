#include "windows/input/desktop_key_windows.hpp"

namespace ministream {

std::optional<WindowsKeyTranslation> windows_key_translation(DesktopKey key) noexcept {
  switch (key) {
    case DesktopKey::A: return WindowsKeyTranslation{0x1E, false};
    case DesktopKey::B: return WindowsKeyTranslation{0x30, false};
    case DesktopKey::C: return WindowsKeyTranslation{0x2E, false};
    case DesktopKey::D: return WindowsKeyTranslation{0x20, false};
    case DesktopKey::E: return WindowsKeyTranslation{0x12, false};
    case DesktopKey::F: return WindowsKeyTranslation{0x21, false};
    case DesktopKey::G: return WindowsKeyTranslation{0x22, false};
    case DesktopKey::H: return WindowsKeyTranslation{0x23, false};
    case DesktopKey::I: return WindowsKeyTranslation{0x17, false};
    case DesktopKey::J: return WindowsKeyTranslation{0x24, false};
    case DesktopKey::K: return WindowsKeyTranslation{0x25, false};
    case DesktopKey::L: return WindowsKeyTranslation{0x26, false};
    case DesktopKey::M: return WindowsKeyTranslation{0x32, false};
    case DesktopKey::N: return WindowsKeyTranslation{0x31, false};
    case DesktopKey::O: return WindowsKeyTranslation{0x18, false};
    case DesktopKey::P: return WindowsKeyTranslation{0x19, false};
    case DesktopKey::Q: return WindowsKeyTranslation{0x10, false};
    case DesktopKey::R: return WindowsKeyTranslation{0x13, false};
    case DesktopKey::S: return WindowsKeyTranslation{0x1F, false};
    case DesktopKey::T: return WindowsKeyTranslation{0x14, false};
    case DesktopKey::U: return WindowsKeyTranslation{0x16, false};
    case DesktopKey::V: return WindowsKeyTranslation{0x2F, false};
    case DesktopKey::W: return WindowsKeyTranslation{0x11, false};
    case DesktopKey::X: return WindowsKeyTranslation{0x2D, false};
    case DesktopKey::Y: return WindowsKeyTranslation{0x15, false};
    case DesktopKey::Z: return WindowsKeyTranslation{0x2C, false};
    case DesktopKey::Digit1: return WindowsKeyTranslation{0x02, false};
    case DesktopKey::Digit2: return WindowsKeyTranslation{0x03, false};
    case DesktopKey::Digit3: return WindowsKeyTranslation{0x04, false};
    case DesktopKey::Digit4: return WindowsKeyTranslation{0x05, false};
    case DesktopKey::Digit5: return WindowsKeyTranslation{0x06, false};
    case DesktopKey::Digit6: return WindowsKeyTranslation{0x07, false};
    case DesktopKey::Digit7: return WindowsKeyTranslation{0x08, false};
    case DesktopKey::Digit8: return WindowsKeyTranslation{0x09, false};
    case DesktopKey::Digit9: return WindowsKeyTranslation{0x0A, false};
    case DesktopKey::Digit0: return WindowsKeyTranslation{0x0B, false};
    case DesktopKey::Enter: return WindowsKeyTranslation{0x1C, false};
    case DesktopKey::Escape: return WindowsKeyTranslation{0x01, false};
    case DesktopKey::Backspace: return WindowsKeyTranslation{0x0E, false};
    case DesktopKey::Tab: return WindowsKeyTranslation{0x0F, false};
    case DesktopKey::Space: return WindowsKeyTranslation{0x39, false};
    case DesktopKey::Minus: return WindowsKeyTranslation{0x0C, false};
    case DesktopKey::Equal: return WindowsKeyTranslation{0x0D, false};
    case DesktopKey::LeftBracket: return WindowsKeyTranslation{0x1A, false};
    case DesktopKey::RightBracket: return WindowsKeyTranslation{0x1B, false};
    case DesktopKey::Backslash: return WindowsKeyTranslation{0x2B, false};
    case DesktopKey::Semicolon: return WindowsKeyTranslation{0x27, false};
    case DesktopKey::Apostrophe: return WindowsKeyTranslation{0x28, false};
    case DesktopKey::Grave: return WindowsKeyTranslation{0x29, false};
    case DesktopKey::Comma: return WindowsKeyTranslation{0x33, false};
    case DesktopKey::Period: return WindowsKeyTranslation{0x34, false};
    case DesktopKey::Slash: return WindowsKeyTranslation{0x35, false};
    case DesktopKey::CapsLock: return WindowsKeyTranslation{0x3A, false};
    case DesktopKey::F1: return WindowsKeyTranslation{0x3B, false};
    case DesktopKey::F2: return WindowsKeyTranslation{0x3C, false};
    case DesktopKey::F3: return WindowsKeyTranslation{0x3D, false};
    case DesktopKey::F4: return WindowsKeyTranslation{0x3E, false};
    case DesktopKey::F5: return WindowsKeyTranslation{0x3F, false};
    case DesktopKey::F6: return WindowsKeyTranslation{0x40, false};
    case DesktopKey::F7: return WindowsKeyTranslation{0x41, false};
    case DesktopKey::F8: return WindowsKeyTranslation{0x42, false};
    case DesktopKey::F9: return WindowsKeyTranslation{0x43, false};
    case DesktopKey::F10: return WindowsKeyTranslation{0x44, false};
    case DesktopKey::F11: return WindowsKeyTranslation{0x57, false};
    case DesktopKey::F12: return WindowsKeyTranslation{0x58, false};
    case DesktopKey::Insert: return WindowsKeyTranslation{0x52, true};
    case DesktopKey::Home: return WindowsKeyTranslation{0x47, true};
    case DesktopKey::PageUp: return WindowsKeyTranslation{0x49, true};
    case DesktopKey::DeleteForward: return WindowsKeyTranslation{0x53, true};
    case DesktopKey::End: return WindowsKeyTranslation{0x4F, true};
    case DesktopKey::PageDown: return WindowsKeyTranslation{0x51, true};
    case DesktopKey::Right: return WindowsKeyTranslation{0x4D, true};
    case DesktopKey::Left: return WindowsKeyTranslation{0x4B, true};
    case DesktopKey::Down: return WindowsKeyTranslation{0x50, true};
    case DesktopKey::Up: return WindowsKeyTranslation{0x48, true};
    case DesktopKey::LeftControl: return WindowsKeyTranslation{0x1D, false};
    case DesktopKey::LeftShift: return WindowsKeyTranslation{0x2A, false};
    case DesktopKey::LeftAlt: return WindowsKeyTranslation{0x38, false};
    case DesktopKey::LeftMeta: return WindowsKeyTranslation{0x5B, true};
  }
  return std::nullopt;
}

}  // namespace ministream
