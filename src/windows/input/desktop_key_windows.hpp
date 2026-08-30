#pragma once

#include "core/input/desktop_key.hpp"

#include <cstdint>
#include <optional>

namespace ministream {

struct WindowsKeyTranslation {
  std::uint16_t scan_code{};
  bool extended{};
  bool operator==(const WindowsKeyTranslation&) const = default;
};

std::optional<WindowsKeyTranslation> windows_key_translation(DesktopKey key) noexcept;

}  // namespace ministream
