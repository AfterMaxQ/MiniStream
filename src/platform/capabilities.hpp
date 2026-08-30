#pragma once

#include <cstdint>
#include <string>

namespace ministream {

struct PlatformCapability {
  bool ready{};
  std::string detail;
};

struct ControlledCapabilities {
  PlatformCapability video;
  PlatformCapability audio;
  PlatformCapability input;
  PlatformCapability network;
  PlatformCapability optional_gamepad;
  bool h264{};
  bool hevc{};
  bool hdr10{};
  std::uint32_t max_width{};
  std::uint32_t max_height{};
  std::uint32_t max_fps{};

  [[nodiscard]] bool ready() const noexcept {
    return video.ready && audio.ready && input.ready && network.ready;
  }
};

struct RemoteCapabilities {
  PlatformCapability video;
  PlatformCapability audio;
  PlatformCapability input;
  PlatformCapability network;
  bool h264{};
  bool hevc{};
  bool hdr10{};
  std::uint32_t max_width{};
  std::uint32_t max_height{};
  std::uint32_t max_fps{};

  [[nodiscard]] bool ready() const noexcept {
    return video.ready && audio.ready && input.ready && network.ready;
  }
};

}  // namespace ministream
