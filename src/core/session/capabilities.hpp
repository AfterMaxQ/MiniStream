#pragma once

#include <cstdint>

namespace ministream {

struct DeviceCapabilities {
  bool h264{};
  bool hevc{};
  bool hdr10{};
  bool rumble{};
  std::uint32_t max_width{};
  std::uint32_t max_height{};
  std::uint32_t max_fps{};
};

}  // namespace ministream
