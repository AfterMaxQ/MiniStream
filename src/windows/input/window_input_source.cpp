#include "windows/input/window_input_source.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace ministream {

namespace {

std::optional<std::uint16_t> virtual_key(std::uint32_t key) noexcept {
  // Printable Qt keys use their Unicode value. Letters and digits have the
  // same virtual-key values on Windows; punctuation needs OEM mappings.
  if (key <= 0xFFU) {
    if (key >= static_cast<std::uint32_t>('a') &&
        key <= static_cast<std::uint32_t>('z')) {
      key -= static_cast<std::uint32_t>('a' - 'A');
    }
    switch (key) {
      case ';': return VK_OEM_1;
      case '=': return VK_OEM_PLUS;
      case ',': return VK_OEM_COMMA;
      case '-': return VK_OEM_MINUS;
      case '.': return VK_OEM_PERIOD;
      case '/': return VK_OEM_2;
      case '`': return VK_OEM_3;
      case '[': return VK_OEM_4;
      case '\\': return VK_OEM_5;
      case ']': return VK_OEM_6;
      case '\'': return VK_OEM_7;
      default: return static_cast<std::uint16_t>(key);
    }
  }

  // Qt::Key_* values for non-printing keys. Keep this source independent of
  // Qt so the native platform library remains usable without the UI target.
  if (key >= 0x01000030U && key <= 0x01000052U) {
    return static_cast<std::uint16_t>(VK_F1 + (key - 0x01000030U));
  }
  switch (key) {
    case 0x01000000U: return VK_ESCAPE;
    case 0x01000001U: return VK_TAB;
    case 0x01000003U: return VK_BACK;
    case 0x01000004U:
    case 0x01000005U: return VK_RETURN;
    case 0x01000006U: return VK_INSERT;
    case 0x01000007U: return VK_DELETE;
    case 0x01000010U: return VK_HOME;
    case 0x01000011U: return VK_END;
    case 0x01000012U: return VK_LEFT;
    case 0x01000013U: return VK_UP;
    case 0x01000014U: return VK_RIGHT;
    case 0x01000015U: return VK_DOWN;
    case 0x01000016U: return VK_PRIOR;
    case 0x01000017U: return VK_NEXT;
    case 0x01000020U: return VK_SHIFT;
    case 0x01000021U: return VK_CONTROL;
    case 0x01000022U: return VK_LWIN;
    case 0x01000023U: return VK_MENU;
    case 0x01000024U: return VK_CAPITAL;
    case 0x01000025U: return VK_NUMLOCK;
    case 0x01000026U: return VK_SCROLL;
    default: return std::nullopt;
  }
}

}  // namespace

std::optional<DesktopInput> WindowInputSource::key(std::uint32_t qt_key,
                                                   bool pressed) noexcept {
  const auto mapped = virtual_key(qt_key);
  if (!mapped) {
    return std::nullopt;
  }
  return DesktopInput{DesktopInputKind::Key,
                      static_cast<std::uint16_t>(pressed ? 0 : KEYEVENTF_KEYUP), 0, 0,
                      *mapped};
}

std::optional<DesktopInput> WindowInputSource::mouse_move(std::int32_t dx,
                                                          std::int32_t dy) noexcept {
  return DesktopInput{DesktopInputKind::MouseMove, 0, dx, dy, 0};
}

std::optional<DesktopInput> WindowInputSource::mouse_button(std::uint32_t qt_button,
                                                            bool pressed) noexcept {
  std::uint16_t button = 0;
  switch (qt_button) {
    case 1: button = static_cast<std::uint16_t>(DesktopMouseButton::Left); break;
    case 2: button = static_cast<std::uint16_t>(DesktopMouseButton::Right); break;
    case 4: button = static_cast<std::uint16_t>(DesktopMouseButton::Middle); break;
    default: return std::nullopt;
  }
  if (!pressed) {
    button = static_cast<std::uint16_t>(button | kDesktopMouseRelease);
  }
  return DesktopInput{DesktopInputKind::MouseButton, button, 0, 0, 0};
}

std::optional<DesktopInput> WindowInputSource::mouse_wheel(std::int32_t delta) noexcept {
  return DesktopInput{DesktopInputKind::MouseWheel, 0, 0, delta, 0};
}

}  // namespace ministream
