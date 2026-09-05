#include "macos/platform/controlled_backend.hpp"

#include "core/config/stream_profile.hpp"
#include "core/net/udp_endpoint.hpp"
#include "macos/audio/system_audio_capture.hpp"
#include "macos/input/accessibility_input.hpp"
#include "macos/video/cgdisplaystream_capture.hpp"
#include "macos/video/videotoolbox_encoder.hpp"

#include <ApplicationServices/ApplicationServices.h>
#include <VideoToolbox/VideoToolbox.h>
#import <AppKit/AppKit.h>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace ministream {

namespace {

bool has_hardware_encoder(CMVideoCodecType codec) {
  CFArrayRef encoders = nullptr;
  if (VTCopyVideoEncoderList(nullptr, &encoders) != noErr || !encoders) {
    return false;
  }

  bool found = false;
  for (CFIndex index = 0; index < CFArrayGetCount(encoders); ++index) {
    const auto encoder =
        static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(encoders, index));
    const auto codec_value =
        CFDictionaryGetValue(encoder, kVTVideoEncoderList_CodecType);
    const auto hardware_value =
        CFDictionaryGetValue(encoder, kVTVideoEncoderList_IsHardwareAccelerated);
    if (!codec_value || !hardware_value ||
        CFGetTypeID(codec_value) != CFNumberGetTypeID() ||
        CFGetTypeID(hardware_value) != CFBooleanGetTypeID()) {
      continue;
    }

    std::int32_t listed_codec{};
    if (CFNumberGetValue(static_cast<CFNumberRef>(codec_value), kCFNumberSInt32Type,
                         &listed_codec) &&
        static_cast<CMVideoCodecType>(listed_codec) == codec &&
        CFBooleanGetValue(static_cast<CFBooleanRef>(hardware_value))) {
      found = true;
      break;
    }
  }
  CFRelease(encoders);
  return found;
}

}  // namespace

struct MacControlledBackend::Impl {
  std::unique_ptr<CGDisplayStreamCapture> capture;
  std::unique_ptr<VideoToolboxEncoder> encoder;
  std::unique_ptr<SystemAudioCapture> audio;
  AccessibilityInput input;
  CodecConfig requested{};
  CodecConfig active{};
  std::uint32_t bitrate_bps{20'000'000};
  bool configured{};
  bool started{};
  CapturedDisplayFrame last_capture{};
  bool capture_dirty{};
  bool keyframe_pending{};
  std::optional<SteadyClock::time_point> next_capture_at;
};

MacControlledBackend::MacControlledBackend() : impl_(std::make_unique<Impl>()) {}
MacControlledBackend::~MacControlledBackend() { stop(); }

void MacControlledBackend::request_permissions() {
  // Prompt from the running bundle so it appears in System Settings directly.
  const void* keys[] = {kAXTrustedCheckOptionPrompt};
  const void* values[] = {kCFBooleanTrue};
  auto options = CFDictionaryCreate(nullptr, keys, values, 1,
      &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  AXIsProcessTrustedWithOptions(options);
  CFRelease(options);
  if (!CGPreflightScreenCaptureAccess()) CGRequestScreenCaptureAccess();
}

ControlledCapabilities MacControlledBackend::inspect() const {
  const auto network = UdpEndpoint{}.bind(0);
  bool screen_permission = true;
  if (@available(macOS 10.15, *)) {
    screen_permission = CGPreflightScreenCaptureAccess();
  }
  const bool trusted = AccessibilityInput::trusted();
  const bool h264 = has_hardware_encoder(kCMVideoCodecType_H264);
  const bool hevc = has_hardware_encoder(kCMVideoCodecType_HEVC);
  bool hdr = false;
#if defined(__arm64__)
  if (@available(macOS 15.0, *)) {
    for (NSScreen* screen in NSScreen.screens) {
      if ([screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue] == CGMainDisplayID())
        hdr = hevc && screen.maximumPotentialExtendedDynamicRangeColorComponentValue > 1.0;
    }
  }
#endif
  const bool video_ready = screen_permission && (h264 || hevc);
  const auto audio = SystemAudioCapture::inspect();
  const auto video_detail = !screen_permission
                                ? "Screen Recording permission required"
                                : (video_ready ? "VideoToolbox hardware encoder detected"
                                               : "Hardware H.264/HEVC encoder unavailable");
  return {{video_ready, video_detail},
          {audio.ready, audio.detail},
          {trusted, trusted ? "Accessibility input available"
                            : "Accessibility permission required"},
          {network.has_value(), network ? "UDP available" : "UDP socket unavailable"},
          {false, "SDL gamepad optional"},
          h264,
          hevc,
          hdr,
          video_ready ? std::min<std::uint32_t>(
                            3840U, static_cast<std::uint32_t>(CGDisplayPixelsWide(CGMainDisplayID())))
                      : 0U,
          video_ready ? std::min<std::uint32_t>(
                            2160U, static_cast<std::uint32_t>(CGDisplayPixelsHigh(CGMainDisplayID())))
                      : 0U,
          video_ready ? 60U : 0U};
}

bool MacControlledBackend::start() {
  if (impl_->started) {
    return true;
  }
  impl_->capture = std::make_unique<CGDisplayStreamCapture>();
  impl_->audio = std::make_unique<SystemAudioCapture>();
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
  impl_->input.clear();
  if (impl_->encoder) impl_->encoder->stop();
  if (impl_->audio) impl_->audio->stop();
  if (impl_->capture) impl_->capture->stop();
  if (impl_->last_capture.pixel_buffer) CVPixelBufferRelease(impl_->last_capture.pixel_buffer);
  impl_->last_capture = {};
  impl_->capture_dirty = impl_->keyframe_pending = false;
  impl_->next_capture_at.reset();
  impl_->encoder.reset();
  impl_->audio.reset();
  impl_->capture.reset();
  impl_->requested = {};
  impl_->active = {};
  impl_->bitrate_bps = 20'000'000;
  impl_->configured = false;
  impl_->started = false;
}

void MacControlledBackend::request_keyframe() noexcept {
  if (impl_ && impl_->encoder) {
    impl_->keyframe_pending = true;
    impl_->encoder->request_idr();
  }
}

bool MacControlledBackend::configure_video(const CodecConfig& config) {
  if (!impl_->started || config.width == 0 || config.height == 0 || config.fps == 0 ||
      (config.hdr10 && (config.codec != VideoCodec::Hevc || !inspect().hdr10)) ||
      (config.codec != VideoCodec::H264 && config.codec != VideoCodec::Hevc)) {
    return false;
  }
  impl_->requested = config;
  impl_->configured = true;
  if (impl_->last_capture.pixel_buffer) CVPixelBufferRelease(impl_->last_capture.pixel_buffer);
  impl_->last_capture = {};
  impl_->capture_dirty = false;
  impl_->keyframe_pending = true;
  impl_->next_capture_at.reset();
  if (impl_->encoder) impl_->encoder->stop();
  if (impl_->capture) {
    impl_->capture->stop();
    if (!impl_->capture->start(config.width, config.height, config.hdr10)) {
      impl_->configured = false;
      return false;
    }
  }
  impl_->active = {};
  return true;
}

bool MacControlledBackend::reconfigure_bitrate(std::uint32_t bitrate_bps) {
  if (!impl_->started || !impl_->encoder || bitrate_bps == 0) return false;
  if (!impl_->encoder->ready()) {
    impl_->bitrate_bps = bitrate_bps;
    return true;
  }
  if (!impl_->encoder->reconfigure_bitrate(bitrate_bps)) return false;
  impl_->bitrate_bps = bitrate_bps;
  return true;
}

std::optional<EncodedFrame> MacControlledBackend::next_video() {
  if (!impl_->started || !impl_->capture || !impl_->encoder) return std::nullopt;
  const auto captured = impl_->capture->take_latest();
  if (captured) {
    if (impl_->last_capture.pixel_buffer) CVPixelBufferRelease(impl_->last_capture.pixel_buffer);
    impl_->last_capture = *captured;
    impl_->capture_dirty = true;
  }
  const auto now = SteadyClock::now();
  const auto drain = [&]() { return impl_->encoder->take_next(); };
  if (!impl_->last_capture.pixel_buffer ||
      (!impl_->capture_dirty && !impl_->keyframe_pending) ||
      (impl_->next_capture_at && now < *impl_->next_capture_at)) return drain();
  const auto& source = impl_->last_capture;
  if (!impl_->encoder->ready()) {
    const auto profile = stream_profile(StreamProfileId::Debug1080);
    const auto codec = impl_->configured ? impl_->requested.codec : profile.codec;
    const auto fps = impl_->configured ? impl_->requested.fps : profile.fps;
    const auto bitrate = impl_->bitrate_bps != 0
                             ? impl_->bitrate_bps
                             : static_cast<std::uint32_t>(profile.initial_bitrate_bps);
    const auto width = impl_->configured ? impl_->requested.width : source.width;
    const auto height = impl_->configured ? impl_->requested.height : source.height;
    if (!impl_->encoder->start({codec, width, height, fps, bitrate, impl_->requested.hdr10})) {
      return std::nullopt;
    }
    impl_->active = impl_->encoder->codec_config();
  }
  const auto timestamp = impl_->capture_dirty ? source.timestamp_us :
      static_cast<std::uint64_t>(std::chrono::duration_cast<Microseconds>(now.time_since_epoch()).count());
  const auto submitted = impl_->encoder->submit(source.pixel_buffer, timestamp, impl_->keyframe_pending);
  if (!submitted) return drain();
  impl_->capture_dirty = impl_->keyframe_pending = false;
  const auto fps = impl_->configured ? impl_->requested.fps : 60U;
  const auto interval = Microseconds{1'000'000 / std::max(1U, fps)};
  impl_->next_capture_at = impl_->next_capture_at.value_or(now) + interval;
  if (*impl_->next_capture_at <= now) impl_->next_capture_at = now + interval;
  const auto encoded = impl_->encoder->take_next();
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

void MacControlledBackend::clear_input() noexcept {
  if (impl_) {
    impl_->input.clear();
  }
}

}  // namespace ministream
