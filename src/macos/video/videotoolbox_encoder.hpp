#pragma once

#include "core/base/result.hpp"
#include "core/transport/packetizer.hpp"
#include "core/video/codec_config.hpp"

#include <CoreVideo/CoreVideo.h>

#include <cstdint>
#include <memory>

namespace ministream {

enum class VideoEncodeError { Unavailable, InvalidConfig, Initialize, Encode };

struct VideoEncodeConfig {
  VideoCodec codec{VideoCodec::H264};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t fps{60};
  std::uint32_t bitrate_bps{20'000'000};
  bool hdr10{};
};

class VideoToolboxEncoder {
 public:
  struct Impl;

  VideoToolboxEncoder();
  ~VideoToolboxEncoder();
  VideoToolboxEncoder(VideoToolboxEncoder&&) noexcept;
  VideoToolboxEncoder& operator=(VideoToolboxEncoder&&) noexcept;
  VideoToolboxEncoder(const VideoToolboxEncoder&) = delete;
  VideoToolboxEncoder& operator=(const VideoToolboxEncoder&) = delete;

  Result<void, VideoEncodeError> start(VideoEncodeConfig config);
  Result<EncodedFrame, VideoEncodeError> encode(CVPixelBufferRef pixel_buffer,
                                                std::uint64_t timestamp_us,
                                                bool force_idr = false);
  void stop() noexcept;
  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] const CodecConfig& codec_config() const noexcept;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
