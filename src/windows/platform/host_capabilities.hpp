#pragma once

#include <cstdint>
#include <string>

namespace ministream {

struct CapabilityStatus {
  bool ready{};
  std::string detail;
};

struct HostCapabilities {
  CapabilityStatus video;
  CapabilityStatus audio;
  CapabilityStatus input;
  CapabilityStatus controller;
  CapabilityStatus network;
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

HostCapabilities inspect_host_capabilities();

}  // namespace ministream
