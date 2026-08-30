#include "core/config/stream_profile.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

namespace {
DiscoveredHost host(DiscoveryCapabilities capabilities, std::uint16_t width,
                    std::uint16_t height, std::uint16_t fps) {
  return {DiscoverySystem::Windows, "host", "192.168.1.10", 47991, capabilities,
          width, height, fps, true};
}

RemoteCapabilities remote(bool h264, bool hevc, bool hdr10, std::uint32_t width,
                           std::uint32_t height, std::uint32_t fps) {
  RemoteCapabilities result;
  result.video = {true, "video"};
  result.audio = {true, "audio"};
  result.input = {true, "input"};
  result.network = {true, "network"};
  result.h264 = h264;
  result.hevc = hevc;
  result.hdr10 = hdr10;
  result.max_width = width;
  result.max_height = height;
  result.max_fps = fps;
  return result;
}
}  // namespace

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

TEST_CASE("profile selection uses the common H264 1080p capability") {
  const auto selected = select_common_stream_profile(
      host({true, false, false, true, true, false}, 1920, 1080, 60),
      remote(true, false, false, 1920, 1080, 60));
  REQUIRE(selected.has_value());
  REQUIRE(selected->id == StreamProfileId::Debug1080);
  REQUIRE(selected->width == 1920);
  REQUIRE(selected->height == 1080);
  REQUIRE(selected->initial_bitrate_bps >= selected->minimum_bitrate_bps);
  REQUIRE(selected->initial_bitrate_bps <= selected->maximum_bitrate_bps);
}

TEST_CASE("profile selection requires HEVC support for 1440p") {
  const auto host_capabilities = DiscoveryCapabilities{false, true, false, true, true, false};
  const auto without_hevc = select_common_stream_profile(
      host(host_capabilities, 2560, 1440, 60), remote(true, false, false, 2560, 1440, 60));
  REQUIRE_FALSE(without_hevc.has_value());

  const auto with_hevc = select_common_stream_profile(
      host(host_capabilities, 2560, 1440, 60), remote(false, true, false, 2560, 1440, 60));
  REQUIRE(with_hevc.has_value());
  REQUIRE(with_hevc->id == StreamProfileId::Balanced1440);
}

TEST_CASE("profile selection rejects a host below the baseline dimensions") {
  REQUIRE_FALSE(select_common_stream_profile(
                    host({true, true, false, true, true, false}, 1280, 720, 60),
                    remote(true, true, false, 3840, 2160, 60))
                    .has_value());
}

TEST_CASE("profile selection enables HDR only when both peers advertise it") {
  const auto capabilities = DiscoveryCapabilities{false, true, true, true, true, false};
  const auto sdr_fallback = select_common_stream_profile(
      host(capabilities, 3840, 2160, 60), remote(false, true, false, 3840, 2160, 60));
  REQUIRE(sdr_fallback.has_value());
  REQUIRE(sdr_fallback->id == StreamProfileId::Balanced1440);
  const auto selected = select_common_stream_profile(
      host(capabilities, 3840, 2160, 60), remote(false, true, true, 3840, 2160, 60));
  REQUIRE(selected.has_value());
  REQUIRE(selected->id == StreamProfileId::Quality4K);
  REQUIRE(selected->hdr10);
}
