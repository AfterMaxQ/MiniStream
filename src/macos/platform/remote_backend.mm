#include "macos/platform/remote_backend.hpp"

#include "core/net/udp_endpoint.hpp"
#include "macos/audio/coreaudio_output.hpp"
#include "macos/input/sdl_gamepad.hpp"
#include "macos/video/video_surface_bridge.hpp"
#include "macos/video/videotoolbox_decoder.hpp"

#include <VideoToolbox/VideoToolbox.h>

#include <algorithm>

namespace ministream {

struct MacRemoteBackend::Impl {
  std::unique_ptr<VideoToolboxDecoder> decoder;
  std::unique_ptr<VideoSurfaceBridge> owned_surface;
  VideoSurfaceBridge* surface{};
  std::unique_ptr<CoreAudioOutput> audio;
  std::unique_ptr<SdlGamepad> gamepad;
  bool started{};
};

MacRemoteBackend::MacRemoteBackend(VideoSurfaceBridge* surface)
    : impl_(std::make_unique<Impl>()) {
  if (surface) {
    impl_->surface = surface;
  } else {
    impl_->owned_surface = std::make_unique<VideoSurfaceBridge>();
    impl_->surface = impl_->owned_surface.get();
  }
}
MacRemoteBackend::~MacRemoteBackend() { stop(); }

RemoteCapabilities MacRemoteBackend::inspect() const {
  const auto network = UdpEndpoint{}.bind(0);
  const bool h264 = VTIsHardwareDecodeSupported(kCMVideoCodecType_H264);
  const bool hevc = VTIsHardwareDecodeSupported(kCMVideoCodecType_HEVC);
  const bool video_ready = h264 || hevc;
  return {{video_ready, video_ready ? "VideoToolbox hardware decoder detected"
                                    : "Hardware H.264/HEVC decoder unavailable"},
          {true, "CoreAudio output"},
          {true, "Window-local keyboard and mouse"},
          {network.has_value(), network ? "UDP available" : "UDP socket unavailable"},
          h264,
          hevc,
          hevc,
          video_ready ? 3840U : 0U,
          video_ready ? 2160U : 0U,
          video_ready ? 60U : 0U};
}

bool MacRemoteBackend::start() {
  if (impl_->started) return true;
  impl_->decoder = std::make_unique<VideoToolboxDecoder>();
  if (!impl_->surface) {
    impl_->owned_surface = std::make_unique<VideoSurfaceBridge>();
    impl_->surface = impl_->owned_surface.get();
  }
  impl_->audio = std::make_unique<CoreAudioOutput>();
  impl_->gamepad = std::make_unique<SdlGamepad>();
  if (!impl_->audio->start()) {
    stop();
    return false;
  }
  impl_->started = true;
  return true;
}

void MacRemoteBackend::stop() noexcept {
  if (!impl_) return;
  if (impl_->audio) impl_->audio->stop();
  if (impl_->decoder) impl_->decoder->stop();
  impl_->gamepad.reset();
  impl_->audio.reset();
  if (impl_->surface) {
    impl_->surface->clear();
  }
  impl_->decoder.reset();
  impl_->started = false;
}

bool MacRemoteBackend::configure_video(const CodecConfig& config) {
  return impl_->started && impl_->decoder && impl_->decoder->initialize(config);
}

void MacRemoteBackend::reset_video() noexcept {
  if (impl_->decoder) impl_->decoder->stop();
  if (impl_->surface) impl_->surface->clear();
}

bool MacRemoteBackend::decode_video(std::span<const std::byte> encoded,
                                    std::uint64_t timestamp_us) {
  if (!impl_->started || !impl_->decoder || !impl_->surface) return false;
  if (!impl_->decoder->decode(encoded, timestamp_us)) return false;
  tick(SteadyClock::now());
  return true;
}

void MacRemoteBackend::tick(SteadyClock::time_point) noexcept {
  if (!impl_->started || !impl_->decoder || !impl_->surface) return;
  if (const auto frame = impl_->decoder->take_latest(); frame) {
    impl_->surface->publish(frame->pixel_buffer, frame->timestamp_us);
    CVPixelBufferRelease(frame->pixel_buffer);
  }
}

bool MacRemoteBackend::play_audio(std::span<const float> samples) {
  return impl_->started && impl_->audio && impl_->audio->push(samples);
}

void MacRemoteBackend::play_rumble(std::uint16_t low, std::uint16_t high,
                                   std::uint32_t duration_ms) {
  if (impl_->gamepad) {
    impl_->gamepad->rumble(low, high, Microseconds{static_cast<std::int64_t>(duration_ms) * 1000});
  }
}

void MacRemoteBackend::clear_rumble() noexcept {
  if (impl_ && impl_->gamepad) {
    (void)impl_->gamepad->rumble(0, 0, Microseconds{0});
  }
}

std::optional<GamepadState> MacRemoteBackend::poll_gamepad() {
  return impl_->gamepad ? impl_->gamepad->poll_latest() : std::nullopt;
}

}  // namespace ministream
