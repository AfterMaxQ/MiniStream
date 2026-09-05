#include "core/input/desktop_key.hpp"

namespace ministream {
namespace {

constexpr std::uint16_t raw(DesktopKey key) noexcept {
  return static_cast<std::uint16_t>(key);
}

}  // namespace

std::optional<DesktopKey> desktop_key_from_qt(std::uint32_t key) noexcept {
  if (key >= static_cast<std::uint32_t>('a') && key <= static_cast<std::uint32_t>('z')) {
    key -= static_cast<std::uint32_t>('a' - 'A');
  }
  if (key >= static_cast<std::uint32_t>('A') && key <= static_cast<std::uint32_t>('Z')) {
    return static_cast<DesktopKey>(raw(DesktopKey::A) +
                                   key - static_cast<std::uint32_t>('A'));
  }
  if (key >= static_cast<std::uint32_t>('1') && key <= static_cast<std::uint32_t>('9')) {
    return static_cast<DesktopKey>(raw(DesktopKey::Digit1) +
                                   key - static_cast<std::uint32_t>('1'));
  }
  if (key == static_cast<std::uint32_t>('0')) {
    return DesktopKey::Digit0;
  }
  switch (key) {
    case '!': return DesktopKey::Digit1;
    case '@': return DesktopKey::Digit2;
    case '#': return DesktopKey::Digit3;
    case '$': return DesktopKey::Digit4;
    case '%': return DesktopKey::Digit5;
    case '^': return DesktopKey::Digit6;
    case '&': return DesktopKey::Digit7;
    case '*': return DesktopKey::Digit8;
    case '(': return DesktopKey::Digit9;
    case ')': return DesktopKey::Digit0;
    case '_': return DesktopKey::Minus;
    case '+': return DesktopKey::Equal;
    case '{': return DesktopKey::LeftBracket;
    case '}': return DesktopKey::RightBracket;
    case '|': return DesktopKey::Backslash;
    case ':': return DesktopKey::Semicolon;
    case '"': return DesktopKey::Apostrophe;
    case '~': return DesktopKey::Grave;
    case '<': return DesktopKey::Comma;
    case '>': return DesktopKey::Period;
    case '?': return DesktopKey::Slash;
    case ' ': return DesktopKey::Space;
    case '-': return DesktopKey::Minus;
    case '=': return DesktopKey::Equal;
    case '[': return DesktopKey::LeftBracket;
    case ']': return DesktopKey::RightBracket;
    case '\\': return DesktopKey::Backslash;
    case ';': return DesktopKey::Semicolon;
    case '\'': return DesktopKey::Apostrophe;
    case '`': return DesktopKey::Grave;
    case ',': return DesktopKey::Comma;
    case '.': return DesktopKey::Period;
    case '/': return DesktopKey::Slash;
    case 0x01000000U: return DesktopKey::Escape;
    case 0x01000001U: return DesktopKey::Tab;
    case 0x01000002U: return DesktopKey::Tab;  // Shift+Tab (Backtab)
    case 0x01000003U: return DesktopKey::Backspace;
    case 0x01000004U:
    case 0x01000005U: return DesktopKey::Enter;
    case 0x01000006U: return DesktopKey::Insert;
    case 0x01000007U: return DesktopKey::DeleteForward;
    case 0x01000010U: return DesktopKey::Home;
    case 0x01000011U: return DesktopKey::End;
    case 0x01000012U: return DesktopKey::Left;
    case 0x01000013U: return DesktopKey::Up;
    case 0x01000014U: return DesktopKey::Right;
    case 0x01000015U: return DesktopKey::Down;
    case 0x01000016U: return DesktopKey::PageUp;
    case 0x01000017U: return DesktopKey::PageDown;
    case 0x01000020U: return DesktopKey::LeftShift;
    case 0x01000021U: return DesktopKey::LeftControl;
    case 0x01000022U: return DesktopKey::LeftMeta;
    case 0x01000023U: return DesktopKey::LeftAlt;
    case 0x01000024U: return DesktopKey::CapsLock;
    default: break;
  }
  if (key >= 0x01000030U && key <= 0x0100003BU) {
    return static_cast<DesktopKey>(raw(DesktopKey::F1) + key - 0x01000030U);
  }
  return std::nullopt;
}

std::optional<DesktopKey> desktop_key_from_wire(std::uint16_t value) noexcept {
  if ((value >= raw(DesktopKey::A) && value <= raw(DesktopKey::Backslash)) ||
      (value >= raw(DesktopKey::Semicolon) && value <= raw(DesktopKey::F12)) ||
      (value >= raw(DesktopKey::Insert) && value <= raw(DesktopKey::Up)) ||
      (value >= raw(DesktopKey::LeftControl) && value <= raw(DesktopKey::LeftMeta))) {
    return static_cast<DesktopKey>(value);
  }
  return std::nullopt;
}

}  // namespace ministream
