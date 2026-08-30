#include "windows/video/dxgi_capture.hpp"
#include "windows/video/nvenc_encoder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace ministream;

TEST_CASE("NVENC encodes one captured BGRA frame", "[.hardware]") {
  DxgiCapture capture;
  REQUIRE(capture.initialize());
  const auto frame = capture.acquire(std::chrono::seconds{1});
  REQUIRE(frame);

  NvencEncoder encoder;
  REQUIRE(encoder.initialize(capture.device(), capture.context(),
                             {VideoCodec::H264, frame->width, frame->height, 60,
                              20'000'000, false}));
  if (frame->format != DXGI_FORMAT_B8G8R8A8_UNORM) {
    SKIP("Windows HDR desktop capture is outside the SDR encoder contract");
  }
  const auto encoded = encoder.encode(
      *frame, static_cast<std::uint64_t>(frame->frame_id), true);
  INFO("NVENC encode error=" << (encoded ? -1 : static_cast<int>(encoded.error()))
                              << " DXGI format=" << static_cast<int>(frame->format));
  REQUIRE(encoded);
  REQUIRE(encoded->keyframe);
  REQUIRE_FALSE(encoded->bytes.empty());
  REQUIRE_FALSE(encoder.codec_config().parameter_sets.empty());
}
