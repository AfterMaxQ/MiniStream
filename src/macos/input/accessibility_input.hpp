#pragma once

#include "core/base/result.hpp"
#include "core/input/desktop_input.hpp"

#include <cstdint>
#include <optional>
#include <set>

namespace ministream {

enum class AccessibilityInputError { Permission, InvalidEvent, PostFailed };

class AccessibilityInput {
 public:
  static bool trusted() noexcept;
  static std::optional<std::uint16_t> native_key_code(DesktopKey key) noexcept;
  static std::optional<DesktopInput> key_from_qt(std::uint32_t qt_key,
                                                 bool pressed) noexcept;
  static std::optional<DesktopInput> mouse_button_from_qt(std::uint32_t qt_button,
                                                          bool pressed) noexcept;
  Result<void, AccessibilityInputError> inject(const DesktopInput& input);
  void clear() noexcept;

 private:
  std::set<DesktopKey> pressed_keys_;
  std::set<DesktopMouseButton> pressed_buttons_;
  std::int64_t wheel_remainder_{};
  bool caps_lock_{};
  bool modifiers_initialized_{};
};

}  // namespace ministream
