#include "windows/input/window_input_source.hpp"
#include "core/input/desktop_key.hpp"

namespace ministream {

std::optional<DesktopInput> WindowInputSource::key(std::uint32_t qt_key,
                                                   bool pressed) noexcept {
  const auto mapped = desktop_key_from_qt(qt_key);
  if (!mapped) {
    return std::nullopt;
  }
  return DesktopInput{DesktopInputKind::Key,
                      static_cast<std::uint16_t>(pressed ? 0 : kDesktopKeyRelease), 0, 0,
                      static_cast<std::uint16_t>(*mapped)};
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
