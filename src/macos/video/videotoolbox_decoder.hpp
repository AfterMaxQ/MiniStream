#pragma once

#include "core/base/result.hpp"
#include "core/video/codec_config.hpp"

#include <CoreVideo/CoreVideo.h>

#include <cstdint>
#include <memory>
#include <span>

namespace ministream {

enum class VideoDecodeError {
  InvalidConfig,
  UnsupportedCodec,
  FormatDescription,
  Session,
  Decode,
};

struct DecodedVideoFrame {
  CVPixelBufferRef pixel_buffer{};
  std::uint64_t timestamp_us{};
};

class VideoToolboxDecoder {
 public:
  VideoToolboxDecoder();
  ~VideoToolboxDecoder();
  VideoToolboxDecoder(VideoToolboxDecoder&&) noexcept;
  VideoToolboxDecoder& operator=(VideoToolboxDecoder&&) noexcept;
  VideoToolboxDecoder(const VideoToolboxDecoder&) = delete;
  VideoToolboxDecoder& operator=(const VideoToolboxDecoder&) = delete;

  Result<void, VideoDecodeError> initialize(const CodecConfig& config);
  Result<void, VideoDecodeError> decode(std::span<const std::byte> annex_b,
                                        std::uint64_t timestamp_us);
  std::optional<DecodedVideoFrame> take_latest();
  void stop() noexcept;

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
