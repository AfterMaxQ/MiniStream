#include "macos/platform/remote_backend.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("macOS remote backend rejects unsupported codec configuration") {
  MacRemoteBackend backend;
  REQUIRE_FALSE(backend.configure_video(
      {static_cast<VideoCodec>(0x7F), 1920, 1080, 60, false, {}}));
  REQUIRE_FALSE(backend.decode_video({}, 0));
}
