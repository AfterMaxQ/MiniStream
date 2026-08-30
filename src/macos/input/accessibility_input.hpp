#pragma once

#include "core/base/result.hpp"
#include "core/input/desktop_input.hpp"

#include <cstdint>
#include <optional>

namespace ministream {

enum class AccessibilityInputError { Permission, InvalidEvent, PostFailed };

class AccessibilityInput {
 public:
  static bool trusted() noexcept;
  static std::optional<DesktopInput> key_from_qt(std::uint32_t qt_key,
                                                 bool pressed) noexcept;
  static std::optional<DesktopInput> mouse_button_from_qt(std::uint32_t qt_button,
                                                          bool pressed) noexcept;
  Result<void, AccessibilityInputError> inject(const DesktopInput& input) const;
};

}  // namespace ministream
