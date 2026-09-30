#include "windows/video/dxgi_capture.hpp"
#include "windows/video/nvenc_encoder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <iostream>

using namespace ministream;

TEST_CASE("NVENC encodes one captured BGRA frame", "[.hardware]") {
  DxgiCapture capture;
  REQUIRE(capture.initialize());
  const auto captured = capture.acquire(std::chrono::seconds{1});
  REQUIRE(captured);
  const auto frame = capture.resize(*captured, 1920, 1080);
  INFO(describe_dxgi_capture(capture.capture_info()));
  REQUIRE(frame);
  REQUIRE(frame->format == DXGI_FORMAT_B8G8R8A8_UNORM);

  NvencEncoder encoder;
  REQUIRE(encoder.initialize(capture.device(), capture.context(),
                             {VideoCodec::H264, frame->width, frame->height, 60,
                              20'000'000, false}));
  const auto encoded = encoder.encode(
      *frame, static_cast<std::uint64_t>(frame->frame_id), true);
  INFO("NVENC encode error=" << (encoded ? -1 : static_cast<int>(encoded.error()))
                              << " DXGI format=" << static_cast<int>(frame->format));
  REQUIRE(encoded);
  REQUIRE(encoded->keyframe);
  REQUIRE_FALSE(encoded->bytes.empty());
  REQUIRE_FALSE(encoder.codec_config().parameter_sets.empty());
  const auto start = SteadyClock::now();
  for (unsigned index = 0; index < 60; ++index) {
    const auto next = encoder.encode(*frame, 16'667ULL * (index + 1));
    REQUIRE(next);
    REQUIRE_FALSE(next->bytes.empty());
  }
  const auto elapsed = std::chrono::duration<double, std::milli>(SteadyClock::now() - start).count();
  std::cout << "NVENC 1080p repeated encode: " << elapsed / 60.0 << " ms/frame\n";
}
