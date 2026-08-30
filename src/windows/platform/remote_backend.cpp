#include "windows/platform/remote_backend.hpp"

#include "core/net/udp_endpoint.hpp"
#include "windows/audio/wasapi_output.hpp"
#include "windows/platform/host_capabilities.hpp"
#include "windows/video/d3d11_video_surface.hpp"
#include "windows/video/mf_decoder.hpp"

#include <Windows.h>
#include <Xinput.h>

#include <algorithm>

namespace ministream {

struct WindowsRemoteBackend::Impl {
  std::unique_ptr<MfDecoder> decoder;
  std::unique_ptr<D3D11VideoSurface> surface;
  std::unique_ptr<WasapiOutput> audio;
  bool started{};
};

WindowsRemoteBackend::WindowsRemoteBackend() : impl_(std::make_unique<Impl>()) {}
WindowsRemoteBackend::~WindowsRemoteBackend() { stop(); }

RemoteCapabilities WindowsRemoteBackend::inspect() const {
  const auto audio = inspect_host_capabilities().audio;
  const auto network = inspect_host_capabilities().network;
  const bool h264 = MfDecoder::hardware_available(VideoCodec::H264);
  const bool hevc = MfDecoder::hardware_available(VideoCodec::Hevc);
  return {{h264 || hevc, (h264 || hevc) ? "Media Foundation hardware decoder detected"
                                         : "Hardware H.264/HEVC decoder unavailable"},
          {audio.ready, audio.detail},
          {true, "Window-local keyboard and mouse"},
          {network.ready, network.detail}};
}

bool WindowsRemoteBackend::start() {
  if (impl_->started) {
    return true;
  }
  impl_->decoder = std::make_unique<MfDecoder>();
  impl_->surface = std::make_unique<D3D11VideoSurface>();
  impl_->audio = std::make_unique<WasapiOutput>();
  if (!impl_->decoder->start() || !impl_->audio->start()) {
    stop();
    return false;
  }
  impl_->started = true;
  return true;
}

void WindowsRemoteBackend::stop() noexcept {
  if (!impl_) {
    return;
  }
  if (impl_->audio) {
    impl_->audio->stop();
  }
  if (impl_->decoder) {
    impl_->decoder->stop();
  }
  impl_->surface.reset();
  impl_->audio.reset();
  impl_->decoder.reset();
  impl_->started = false;
}

bool WindowsRemoteBackend::configure_video(const CodecConfig& config) {
  return impl_->started && impl_->decoder && impl_->decoder->configure(config);
}

bool WindowsRemoteBackend::decode_video(std::span<const std::byte> encoded,
                                        std::uint64_t timestamp_us) {
  if (!impl_->started || !impl_->decoder || !impl_->surface) {
    return false;
  }
  if (!impl_->decoder->decode(encoded, timestamp_us)) {
    return false;
  }
  if (const auto frame = impl_->decoder->take_latest(); frame) {
    impl_->surface->publish({frame->texture, frame->timestamp_us, frame->width, frame->height});
  }
  return true;
}

bool WindowsRemoteBackend::play_audio(std::span<const float> interleaved_stereo) {
  return impl_->started && impl_->audio && impl_->audio->push(interleaved_stereo);
}

void WindowsRemoteBackend::play_rumble(std::uint16_t low, std::uint16_t high,
                                       std::uint32_t duration_ms) {
  XINPUT_VIBRATION vibration{};
  vibration.wLeftMotorSpeed = static_cast<WORD>(low >> 8U);
  vibration.wRightMotorSpeed = static_cast<WORD>(high >> 8U);
  XInputSetState(0, &vibration);
  if (duration_ms > 0) {
    // The input path is intentionally non-blocking.  Windows will clear the
    // motor on the next feedback packet or when the session stops.
  }
}

}  // namespace ministream
