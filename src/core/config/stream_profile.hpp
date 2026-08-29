#pragma once

#include "core/video/codec_config.hpp"

#include <cstdint>

namespace ministream {

enum class StreamProfileId { Debug1080, Balanced1440, Quality4K };

struct StreamProfile {
  StreamProfileId id{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t fps{};
  VideoCodec codec{VideoCodec::H264};
  bool hdr10{};
  std::uint64_t minimum_bitrate_bps{};
  std::uint64_t initial_bitrate_bps{};
  std::uint64_t maximum_bitrate_bps{};
};

StreamProfile stream_profile(StreamProfileId id) noexcept;

}  // namespace ministream
