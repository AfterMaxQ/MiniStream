#include "windows/input/window_input_source.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace ministream {

std::optional<DesktopInput> WindowInputSource::key(std::uint16_t virtual_key,
                                                   bool pressed) noexcept {
  if (virtual_key == VK_ESCAPE) {
    return std::nullopt;
  }
  return DesktopInput{DesktopInputKind::Key,
                      static_cast<std::uint16_t>(pressed ? 0 : KEYEVENTF_KEYUP), 0, 0,
                      virtual_key};
}

std::optional<DesktopInput> WindowInputSource::mouse_move(std::int32_t dx,
                                                          std::int32_t dy) noexcept {
  return DesktopInput{DesktopInputKind::MouseMove, 0, dx, dy, 0};
}

std::optional<DesktopInput> WindowInputSource::mouse_button(std::uint16_t flags) noexcept {
  return DesktopInput{DesktopInputKind::MouseButton, flags, 0, 0, 0};
}

std::optional<DesktopInput> WindowInputSource::mouse_wheel(std::int32_t delta) noexcept {
  return DesktopInput{DesktopInputKind::MouseWheel, 0, 0, delta, 0};
}

}  // namespace ministream
