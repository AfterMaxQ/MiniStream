#include "core/config/stream_profile.hpp"

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

}  // namespace ministream
