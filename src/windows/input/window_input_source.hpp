#pragma once

#include "core/input/desktop_input.hpp"

#include <cstdint>
#include <optional>

namespace ministream {

// Converts events received by the Qt window into the wire-neutral desktop
// input representation.  It deliberately has no global hook or OS capture.
class WindowInputSource {
 public:
  static std::optional<DesktopInput> key(std::uint32_t qt_key,
                                         bool pressed) noexcept;
  static std::optional<DesktopInput> mouse_move(std::int32_t dx,
                                                std::int32_t dy) noexcept;
  static std::optional<DesktopInput> mouse_button(std::uint32_t qt_button,
                                                  bool pressed) noexcept;
  static std::optional<DesktopInput> mouse_wheel(std::int32_t delta) noexcept;
};

}  // namespace ministream
