#include "core/config/stream_profile.hpp"

#include <algorithm>

namespace ministream {

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
  const auto supports = [&](const StreamProfile& profile) {
    const bool host_codec = profile.codec == VideoCodec::H264 ? host.capabilities.h264
                                                               : host.capabilities.hevc;
    const bool remote_codec = profile.codec == VideoCodec::H264 ? remote.h264 : remote.hevc;
    return host_codec && remote_codec && host.max_width >= profile.width &&
           host.max_height >= profile.height && host.max_fps >= profile.fps &&
           remote.max_width >= profile.width && remote.max_height >= profile.height &&
           remote.max_fps >= profile.fps &&
           (!profile.hdr10 || (host.capabilities.hdr10 && remote.hdr10));
  };

  for (const auto id : {StreamProfileId::Quality4K, StreamProfileId::Balanced1440,
                        StreamProfileId::Debug1080}) {
    auto profile = stream_profile(id);
    if (host.capabilities.hevc && remote.hevc && host.capabilities.hdr10 && remote.hdr10)
      profile.codec = VideoCodec::Hevc;
    if (profile.codec == VideoCodec::Hevc)
      profile.hdr10 = host.capabilities.hdr10 && remote.hdr10;
    if (supports(profile)) {
      profile.initial_bitrate_bps = std::clamp(
          profile.initial_bitrate_bps, profile.minimum_bitrate_bps,
          profile.maximum_bitrate_bps);
      return profile;
    }
  }
  return std::nullopt;
}

}  // namespace ministream
