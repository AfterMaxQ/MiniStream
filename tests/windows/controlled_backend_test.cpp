#include "windows/platform/controlled_backend.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("Windows controlled backend reports keyboard and mouse as required") {
  WindowsControlledBackend backend;
  const auto capabilities = backend.inspect();
  REQUIRE(capabilities.input.ready);
  REQUIRE(capabilities.network.detail.size() > 0);
  REQUIRE(capabilities.ready() ==
          (capabilities.video.ready && capabilities.audio.ready && capabilities.input.ready &&
           capabilities.network.ready));
}

TEST_CASE("Windows controlled backend can be stopped before it starts") {
  WindowsControlledBackend backend;
  backend.stop();
  REQUIRE_FALSE(backend.next_video().has_value());
  REQUIRE_FALSE(backend.next_audio().has_value());
}

TEST_CASE("Windows controlled backend preflights advertised codecs", "[.hardware]") {
  WindowsControlledBackend backend;
  if (!backend.start()) {
    const auto capabilities = backend.inspect();
    if (capabilities.video.detail.find("Windows HDR") != std::string::npos) {
      REQUIRE_FALSE(capabilities.video.ready);
      SKIP(capabilities.video.detail);
    }
    SKIP("Controlled hardware backend is not available");
  }
  const auto capabilities = backend.inspect();
  REQUIRE(capabilities.h264);
  REQUIRE(backend.configure_video(
      {VideoCodec::H264, 1920, 1080, 60, false, {}}));
  if (capabilities.hevc) {
    REQUIRE(backend.configure_video(
        {VideoCodec::Hevc, 1920, 1080, 60, false, {}}));
  }
}
