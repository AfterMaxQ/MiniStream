#include "macos/platform/controlled_backend.hpp"

#include "core/config/stream_profile.hpp"
#include "core/net/udp_endpoint.hpp"
#include "macos/audio/coreaudio_capture.hpp"
#include "macos/input/accessibility_input.hpp"
#include "macos/video/screencapturekit_capture.hpp"
#include "macos/video/videotoolbox_encoder.hpp"

#include <ApplicationServices/ApplicationServices.h>
#include <VideoToolbox/VideoToolbox.h>

#include <algorithm>
#include <utility>

namespace ministream {

struct MacControlledBackend::Impl {
  std::unique_ptr<ScreenCaptureKitCapture> capture;
  std::unique_ptr<VideoToolboxEncoder> encoder;
  std::unique_ptr<CoreAudioCapture> audio;
  AccessibilityInput input;
  CodecConfig requested{};
  CodecConfig active{};
  bool configured{};
  bool started{};
};

MacControlledBackend::MacControlledBackend() : impl_(std::make_unique<Impl>()) {}
MacControlledBackend::~MacControlledBackend() { stop(); }

ControlledCapabilities MacControlledBackend::inspect() const {
  const auto network = UdpEndpoint{}.bind(0);
  bool screen_permission = true;
  if (@available(macOS 10.15, *)) {
    screen_permission = CGPreflightScreenCaptureAccess();
  }
  const bool trusted = AccessibilityInput::trusted();
  const bool h264 = VTIsHardwareEncodeSupported(kCMVideoCodecType_H264);
  const bool hevc = VTIsHardwareEncodeSupported(kCMVideoCodecType_HEVC);
  const bool video_ready = screen_permission && (h264 || hevc);
  const auto video_detail = !screen_permission
                                ? "Screen Recording permission required"
                                : (video_ready ? "VideoToolbox hardware encoder detected"
                                               : "Hardware H.264/HEVC encoder unavailable");
  return {{video_ready, video_detail},
          {true, "CoreAudio microphone input"},
          {trusted, trusted ? "Accessibility input available"
                            : "Accessibility permission required"},
          {network.has_value(), network ? "UDP available" : "UDP socket unavailable"},
          {false, "SDL gamepad optional"}};
}

bool MacControlledBackend::start() {
  if (impl_->started) {
    return true;
  }
  impl_->capture = std::make_unique<ScreenCaptureKitCapture>();
  impl_->audio = std::make_unique<CoreAudioCapture>();
  impl_->encoder = std::make_unique<VideoToolboxEncoder>();
  if (!impl_->capture->start() || !impl_->audio->start() || !AccessibilityInput::trusted()) {
    stop();
    return false;
  }
  impl_->started = true;
  return true;
}

void MacControlledBackend::stop() noexcept {
  if (!impl_) return;
  if (impl_->encoder) impl_->encoder->stop();
  if (impl_->audio) impl_->audio->stop();
  if (impl_->capture) impl_->capture->stop();
  impl_->encoder.reset();
  impl_->audio.reset();
  impl_->capture.reset();
  impl_->requested = {};
  impl_->active = {};
  impl_->configured = false;
  impl_->started = false;
}

bool MacControlledBackend::configure_video(const CodecConfig& config) {
  if (!impl_->started || config.width == 0 || config.height == 0 || config.fps == 0 ||
      config.hdr10 || (config.codec != VideoCodec::H264 && config.codec != VideoCodec::Hevc)) {
    return false;
  }
  impl_->requested = config;
  impl_->configured = true;
  if (impl_->encoder) impl_->encoder->stop();
  impl_->active = {};
  return true;
}

std::optional<EncodedFrame> MacControlledBackend::next_video() {
  if (!impl_->started || !impl_->capture || !impl_->encoder) return std::nullopt;
  const auto captured = impl_->capture->take_latest();
  if (!captured) return std::nullopt;
  if (!impl_->encoder->ready()) {
    const auto profile = stream_profile(StreamProfileId::Debug1080);
    const auto codec = impl_->configured ? impl_->requested.codec : profile.codec;
    const auto fps = impl_->configured ? impl_->requested.fps : profile.fps;
    const auto bitrate = impl_->configured
                             ? std::max<std::uint32_t>(1, impl_->requested.width >= 3'840
                                                               ? 50'000'000U
                                                               : 20'000'000U)
                             : static_cast<std::uint32_t>(profile.initial_bitrate_bps);
    if (!impl_->encoder->start({codec, captured->width, captured->height, fps, bitrate, false})) {
      CVPixelBufferRelease(captured->pixel_buffer);
      return std::nullopt;
    }
    impl_->active = impl_->encoder->codec_config();
  }
  const auto encoded = impl_->encoder->encode(captured->pixel_buffer, captured->timestamp_us);
  CVPixelBufferRelease(captured->pixel_buffer);
  if (!encoded) return std::nullopt;
  impl_->active = impl_->encoder->codec_config();
  return *encoded;
}

CodecConfig MacControlledBackend::codec_config() const {
  return impl_->encoder && impl_->encoder->ready() ? impl_->encoder->codec_config() : impl_->active;
}

std::optional<PcmBlock> MacControlledBackend::next_audio() {
  if (!impl_->started || !impl_->audio) return std::nullopt;
  const auto block = impl_->audio->read();
  return block ? std::optional<PcmBlock>{*block} : std::nullopt;
}

bool MacControlledBackend::inject_input(const DesktopInput& input) {
  return impl_->started && impl_->input.inject(input);
}

}  // namespace ministream
