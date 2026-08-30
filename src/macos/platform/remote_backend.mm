#include "macos/platform/remote_backend.hpp"

#include "core/net/udp_endpoint.hpp"
#include "macos/audio/coreaudio_output.hpp"
#include "macos/input/sdl_gamepad.hpp"
#include "macos/video/video_surface_bridge.hpp"
#include "macos/video/videotoolbox_decoder.hpp"

#include <algorithm>

namespace ministream {

struct MacRemoteBackend::Impl {
  std::unique_ptr<VideoToolboxDecoder> decoder;
  std::unique_ptr<VideoSurfaceBridge> surface;
  std::unique_ptr<CoreAudioOutput> audio;
  std::unique_ptr<SdlGamepad> gamepad;
  bool started{};
};

MacRemoteBackend::MacRemoteBackend() : impl_(std::make_unique<Impl>()) {}
MacRemoteBackend::~MacRemoteBackend() { stop(); }

RemoteCapabilities MacRemoteBackend::inspect() const {
  const auto network = UdpEndpoint{}.bind(0);
  return {{true, "VideoToolbox hardware decoder"},
          {true, "CoreAudio output"},
          {true, "Window-local keyboard and mouse"},
          {network.has_value(), network ? "UDP available" : "UDP socket unavailable"}};
}

bool MacRemoteBackend::start() {
  if (impl_->started) return true;
  impl_->decoder = std::make_unique<VideoToolboxDecoder>();
  impl_->surface = std::make_unique<VideoSurfaceBridge>();
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
  impl_->surface.reset();
  impl_->decoder.reset();
  impl_->started = false;
}

bool MacRemoteBackend::configure_video(const CodecConfig& config) {
  return impl_->started && impl_->decoder && impl_->decoder->initialize(config);
}

bool MacRemoteBackend::decode_video(std::span<const std::byte> encoded,
                                    std::uint64_t timestamp_us) {
  if (!impl_->started || !impl_->decoder || !impl_->surface) return false;
  if (!impl_->decoder->decode(encoded, timestamp_us)) return false;
  if (const auto frame = impl_->decoder->take_latest(); frame) {
    impl_->surface->publish(frame->pixel_buffer, frame->timestamp_us);
    CVPixelBufferRelease(frame->pixel_buffer);
  }
  return true;
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

}  // namespace ministream
