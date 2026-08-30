#pragma once

#include <cstdint>
#include <vector>

namespace ministream {

struct PcmBlock {
  std::uint64_t host_timestamp_us{};
  std::uint32_t frames{};
  std::vector<float> interleaved_stereo;
  bool discontinuity{};
};

}  // namespace ministream
