#pragma once

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

  [[nodiscard]] bool ready() const noexcept {
    return video.ready && audio.ready && input.ready && network.ready;
  }
};

struct RemoteCapabilities {
  PlatformCapability video;
  PlatformCapability audio;
  PlatformCapability input;
  PlatformCapability network;

  [[nodiscard]] bool ready() const noexcept {
    return video.ready && audio.ready && input.ready && network.ready;
  }
};

}  // namespace ministream
