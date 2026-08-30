#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ministream {

enum class VideoCodec : std::uint8_t { H264 = 1, Hevc = 2 };

struct CodecConfig {
  VideoCodec codec{VideoCodec::H264};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t fps{};
  bool hdr10{};
  std::vector<std::byte> parameter_sets;
  bool operator==(const CodecConfig&) const = default;
};

}  // namespace ministream
