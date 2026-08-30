#pragma once

#include "core/base/result.hpp"
#include "core/transport/packetizer.hpp"
#include "core/video/codec_config.hpp"

#include <CoreVideo/CoreVideo.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace ministream {

enum class VideoEncodeError { Unavailable, InvalidConfig, Initialize, Encode, Reconfigure };

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
  Result<void, VideoEncodeError> submit(CVPixelBufferRef pixel_buffer,
                                        std::uint64_t timestamp_us,
                                        bool force_idr = false);
  std::optional<EncodedFrame> take_next();
  // Compatibility alias. Frames are returned in encoded order rather than
  // replacing an unread frame with a newer one.
  std::optional<EncodedFrame> take_latest();
  Result<EncodedFrame, VideoEncodeError> encode(CVPixelBufferRef pixel_buffer,
                                                std::uint64_t timestamp_us,
                                                bool force_idr = false);
  Result<void, VideoEncodeError> reconfigure_bitrate(std::uint32_t bitrate_bps);
  void request_idr() noexcept;
  void stop() noexcept;
  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] CodecConfig codec_config() const;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
