#include "core/config/stream_profile.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("built-in profiles retain the fixed alpha ladder") {
  const auto debug = stream_profile(StreamProfileId::Debug1080);
  REQUIRE(debug.width == 1920);
  REQUIRE(debug.height == 1080);
  REQUIRE(debug.codec == VideoCodec::H264);
  REQUIRE_FALSE(debug.hdr10);
  REQUIRE(debug.initial_bitrate_bps == 20'000'000);

  const auto balanced = stream_profile(StreamProfileId::Balanced1440);
  REQUIRE(balanced.width == 2560);
  REQUIRE(balanced.codec == VideoCodec::Hevc);
  REQUIRE_FALSE(balanced.hdr10);

  const auto quality = stream_profile(StreamProfileId::Quality4K);
  REQUIRE(quality.width == 3840);
  REQUIRE(quality.height == 2160);
  REQUIRE(quality.fps == 60);
  REQUIRE(quality.codec == VideoCodec::Hevc);
  REQUIRE(quality.hdr10);
  REQUIRE(quality.minimum_bitrate_bps == 20'000'000);
  REQUIRE(quality.initial_bitrate_bps == 50'000'000);
  REQUIRE(quality.maximum_bitrate_bps == 80'000'000);
}
