#pragma once

#include <string>

namespace ministream {

struct CapabilityStatus {
  bool ready{};
  std::string detail;
};

struct HostCapabilities {
  CapabilityStatus video;
  CapabilityStatus audio;
  CapabilityStatus controller;
  CapabilityStatus network;

  [[nodiscard]] bool ready() const noexcept {
    return video.ready && audio.ready && controller.ready && network.ready;
  }
};

HostCapabilities inspect_host_capabilities();

}  // namespace ministream
