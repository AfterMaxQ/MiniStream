#pragma once

#include "core/base/result.hpp"
#include "core/input/desktop_input.hpp"

namespace ministream {

enum class AccessibilityInputError { Permission, InvalidEvent, PostFailed };

class AccessibilityInput {
 public:
  static bool trusted() noexcept;
  Result<void, AccessibilityInputError> inject(const DesktopInput& input) const;
};

}  // namespace ministream
