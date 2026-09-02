#include "windows/platform/remote_backend.hpp"
#include "windows/video/mf_decoder.hpp"

#include "core/video/codec_config.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("Windows remote backend rejects unsupported codec configuration") {
  WindowsRemoteBackend backend;
  REQUIRE_FALSE(backend.configure_video(
      {static_cast<VideoCodec>(0x7F), 1920, 1080, 60, false, {}}));
  REQUIRE_FALSE(backend.decode_video({}, 0));
}

TEST_CASE("Windows remote backend teardown is idempotent") {
  WindowsRemoteBackend backend;
  backend.stop();
  backend.stop();
  backend.play_rumble(0, 0, 0);
  REQUIRE_FALSE(backend.play_audio({}));
}

TEST_CASE("Windows Media Foundation initializes the H264 decoder", "[.hardware]") {
  REQUIRE(MfDecoder::hardware_available(VideoCodec::H264));
  MfDecoder decoder;
  REQUIRE(decoder.start());
  REQUIRE(decoder.configure({VideoCodec::H264, 1920, 1080, 60, false, {}}));
}
