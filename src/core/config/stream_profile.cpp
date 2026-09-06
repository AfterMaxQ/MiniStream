#include "core/config/stream_profile.hpp"

#include <algorithm>

namespace ministream {

namespace {

std::optional<StreamProfile> supported_profile(
    const DiscoveredHost& host, const RemoteCapabilities& remote,
    StreamProfileId id) noexcept {
  auto profile = stream_profile(id);
  const bool host_codec = profile.codec == VideoCodec::H264
                              ? host.capabilities.h264
                              : host.capabilities.hevc;
  const bool remote_codec = profile.codec == VideoCodec::H264
                                ? remote.h264
                                : remote.hevc;
  if (!host_codec || !remote_codec || host.max_width < profile.width ||
      host.max_height < profile.height || host.max_fps < profile.fps ||
      remote.max_width < profile.width || remote.max_height < profile.height ||
      remote.max_fps < profile.fps) {
    return std::nullopt;
  }
  if (profile.codec == VideoCodec::Hevc) {
    profile.hdr10 = profile.hdr10 && host.capabilities.hdr10 && remote.hdr10;
  }
  profile.initial_bitrate_bps = std::clamp(
      profile.initial_bitrate_bps, profile.minimum_bitrate_bps,
      profile.maximum_bitrate_bps);
  return profile;
}

}  // namespace

StreamProfile stream_profile(StreamProfileId id) noexcept {
  switch (id) {
    case StreamProfileId::Debug1080:
      return {id, 1920, 1080, 60, VideoCodec::H264, false,
              10'000'000, 20'000'000, 30'000'000};
    case StreamProfileId::Balanced1440:
      return {id, 2560, 1440, 60, VideoCodec::Hevc, false,
              20'000'000, 35'000'000, 60'000'000};
    case StreamProfileId::Quality4K:
      return {id, 3840, 2160, 60, VideoCodec::Hevc, true,
              20'000'000, 50'000'000, 80'000'000};
  }
  return {};
}

std::optional<StreamProfile> select_common_stream_profile(
    const DiscoveredHost& host, const RemoteCapabilities& remote) noexcept {
  for (const auto id : {StreamProfileId::Quality4K, StreamProfileId::Balanced1440,
                        StreamProfileId::Debug1080}) {
    if (const auto profile = supported_profile(host, remote, id)) return profile;
  }
  return std::nullopt;
}

std::optional<StreamProfile> select_stream_profile(
    const DiscoveredHost& host, const RemoteCapabilities& remote,
    StreamProfileId preferred) noexcept {
  return supported_profile(host, remote, preferred);
}

}  // namespace ministream
