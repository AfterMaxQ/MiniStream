#pragma once

#include <cstdint>

namespace ministream {

enum class PacketType : std::uint8_t {
  Control = 1,
  Video = 2,
  VideoFec = 3,
  Audio = 4,
  Input = 5,
  Feedback = 6,
  Telemetry = 7,
};

}  // namespace ministream
