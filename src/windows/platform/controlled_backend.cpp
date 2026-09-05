#include "windows/platform/controlled_backend.hpp"

#include "core/config/stream_profile.hpp"
#include "windows/audio/wasapi_loopback.hpp"
#include "windows/input/remote_input_sink.hpp"
#include "windows/input/virtual_gamepad.hpp"
#include "windows/platform/host_capabilities.hpp"
#include "windows/video/dxgi_capture.hpp"
#include "windows/video/nvenc_encoder.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace ministream {

struct WindowsControlledBackend::Impl {
  std::unique_ptr<DxgiCapture> capture;
  std::unique_ptr<NvencEncoder> encoder;
  std::unique_ptr<WasapiLoopback> audio;
  std::unique_ptr<RemoteInputSink> input;
  std::unique_ptr<VirtualGamepad> gamepad;
  CodecConfig requested{};
  CodecConfig active{};
  std::uint32_t bitrate_bps{20'000'000};
  bool started{};
  bool configured{};
  std::optional<CapturedFrame> last_capture;
  bool keyframe_pending{true};
  std::uint32_t next_video_id{};
  std::optional<SteadyClock::time_point> next_capture_at;
  bool codec_support_probed{};
  bool h264_supported{};
  bool hevc_supported{};
  std::string last_start_error;
  std::function<void(const RumblePacket&)> rumble_sender;
};

WindowsControlledBackend::WindowsControlledBackend() : impl_(std::make_unique<Impl>()) {}
WindowsControlledBackend::~WindowsControlledBackend() { stop(); }

ControlledCapabilities WindowsControlledBackend::inspect() const {
  const auto host = inspect_host_capabilities();
  const PlatformCapability video = impl_->last_start_error.empty()
                                       ? PlatformCapability{host.video.ready, host.video.detail}
                                       : PlatformCapability{false, impl_->last_start_error};
  const auto h264 = impl_->codec_support_probed ? impl_->h264_supported : host.h264;
  const auto hevc = impl_->codec_support_probed ? impl_->hevc_supported : host.hevc;
  return {video,
          {host.audio.ready, host.audio.detail},
          {host.input.ready, host.input.detail},
          {host.network.ready, host.network.detail},
          {host.controller.ready, host.controller.detail},
          video.ready && h264,
          video.ready && hevc,
          host.hdr10,
          video.ready ? host.max_width : 0U,
          video.ready ? host.max_height : 0U,
          video.ready ? host.max_fps : 0U};
}

bool WindowsControlledBackend::start() {
  if (impl_->started) {
    return true;
  }
  impl_->last_start_error.clear();
  impl_->capture = std::make_unique<DxgiCapture>();
  impl_->audio = std::make_unique<WasapiLoopback>();
  impl_->input = std::make_unique<RemoteInputSink>();
  impl_->gamepad = std::make_unique<VirtualGamepad>();
  if (!impl_->capture->initialize() || !impl_->audio->start()) {
    stop();
    return false;
  }
  // ViGEm is optional.  A keyboard/mouse-only session must still start.
  if (!impl_->gamepad->start()) {
    impl_->gamepad.reset();
  } else if (impl_->rumble_sender) {
    impl_->gamepad->set_rumble_callback([this](const RumbleState& state) {
      if (impl_->rumble_sender) {
        impl_->rumble_sender({state.low, state.high, 100});
      }
    });
  }
  const auto host = inspect_host_capabilities();
  const auto probe_width = std::max(1U, std::min(host.max_width, 1920U));
  const auto probe_height = std::max(1U, std::min(host.max_height, 1080U));
  const auto supports = [&](VideoCodec codec) {
    NvencEncoder probe;
    return static_cast<bool>(probe.initialize(
        impl_->capture->device(), impl_->capture->context(),
        {codec, probe_width, probe_height, 60, 20'000'000, false}));
  };
  impl_->h264_supported = supports(VideoCodec::H264);
  impl_->hevc_supported = supports(VideoCodec::Hevc);
  impl_->codec_support_probed = true;
  if (!impl_->h264_supported) {
    stop();
    return false;
  }
  const auto capture_info = impl_->capture->capture_info();
  const auto capture_status = classify_dxgi_capture(capture_info);
  if (capture_status == DxgiCaptureStatus::HdrActive) {
    const auto diagnostic = describe_dxgi_capture(capture_info);
    stop();
    impl_->last_start_error =
        "Windows HDR capture is not supported yet; turn off HDR for this display; " +
        diagnostic;
    return false;
  }
  if (capture_status == DxgiCaptureStatus::UnsupportedFormat) {
    const auto diagnostic = describe_dxgi_capture(capture_info);
    stop();
    impl_->last_start_error = "Unsupported Windows desktop capture format; " + diagnostic;
    return false;
  }
  impl_->encoder = std::make_unique<NvencEncoder>();
  impl_->requested = {};
  impl_->active = {};
  impl_->bitrate_bps = 20'000'000;
  impl_->configured = false;
  impl_->last_capture.reset();
  impl_->next_capture_at.reset();
  impl_->next_video_id = 0;
  impl_->started = true;
  return true;
}

void WindowsControlledBackend::stop() noexcept {
  if (!impl_) {
    return;
  }
  if (impl_->gamepad) {
    impl_->gamepad->stop();
  }
  if (impl_->input) {
    impl_->input->clear();
  }
  if (impl_->encoder) {
    impl_->encoder->stop();
  }
  impl_->gamepad.reset();
  impl_->encoder.reset();
  impl_->last_capture.reset();
  impl_->next_capture_at.reset();
  impl_->audio.reset();
  impl_->capture.reset();
  impl_->input.reset();
  impl_->requested = {};
  impl_->active = {};
  impl_->bitrate_bps = 20'000'000;
  impl_->configured = false;
  impl_->codec_support_probed = false;
  impl_->h264_supported = false;
  impl_->hevc_supported = false;
  impl_->started = false;
  impl_->keyframe_pending = true;
}

bool WindowsControlledBackend::configure_video(const CodecConfig& config) {
  if (!impl_->started || config.width == 0 || config.height == 0 || config.fps == 0 ||
      (config.codec != VideoCodec::H264 && config.codec != VideoCodec::Hevc) || config.hdr10) {
    return false;
  }
  if (!impl_->encoder) {
    return false;
  }
  impl_->encoder->stop();
  impl_->next_capture_at.reset();
  impl_->next_video_id = 0;
  impl_->keyframe_pending = true;
  const NvencConfig encoder_config{config.codec, config.width, config.height,
                                   config.fps, impl_->bitrate_bps, false};
  if (!impl_->encoder->initialize(impl_->capture->device(), impl_->capture->context(),
                                  encoder_config)) {
    impl_->configured = false;
    impl_->requested = {};
    impl_->active = {};
    return false;
  }
  impl_->requested = config;
  impl_->active = impl_->encoder->codec_config();
  impl_->configured = true;
  return true;
}

bool WindowsControlledBackend::reconfigure_bitrate(std::uint32_t bitrate_bps) {
  if (!impl_->started || !impl_->encoder || bitrate_bps == 0) {
    return false;
  }
  if (!impl_->encoder->ready()) {
    impl_->bitrate_bps = bitrate_bps;
    return true;
  }
  if (!impl_->encoder->reconfigure_bitrate(bitrate_bps)) {
    return false;
  }
  impl_->bitrate_bps = bitrate_bps;
  return true;
}

void WindowsControlledBackend::request_keyframe() noexcept {
  impl_->keyframe_pending = true;
  if (impl_->encoder) {
    impl_->encoder->request_idr();
  }
}

std::optional<EncodedFrame> WindowsControlledBackend::next_video() {
  if (!impl_->started || !impl_->capture || !impl_->encoder) {
    return std::nullopt;
  }
  const auto now = SteadyClock::now();
  if (impl_->next_capture_at && now < *impl_->next_capture_at) return std::nullopt;
  const auto fps = impl_->configured ? impl_->requested.fps : 60U;
  const auto interval = Microseconds{1'000'000 / std::max(1U, fps)};
  impl_->next_capture_at = impl_->next_capture_at.value_or(now) + interval;
  if (*impl_->next_capture_at <= now) impl_->next_capture_at = now + interval;
  const auto captured = impl_->capture->acquire(Microseconds{0});
  if (!captured && captured.error() == CaptureError::AccessLost) {
    impl_->last_capture.reset();
    request_keyframe();
    return std::nullopt;
  }
  if (captured) impl_->last_capture = *captured;
  // Desktop Duplication produces no new frame when the desktop is static.
  // A recovery IDR must still be able to encode the last captured desktop.
  if ((!captured && !impl_->keyframe_pending) || !impl_->last_capture) return std::nullopt;
  auto frame = *impl_->last_capture;
  if (!captured) frame.captured_at = now;
  const auto target_width = impl_->configured ? impl_->requested.width : frame.width;
  const auto target_height = impl_->configured ? impl_->requested.height : frame.height;
  if (frame.format != DXGI_FORMAT_B8G8R8A8_UNORM || frame.width != target_width ||
      frame.height != target_height) {
    const auto resized = impl_->capture->resize(frame, target_width, target_height);
    if (!resized) {
      return std::nullopt;
    }
    frame = *resized;
  }
  if (!impl_->encoder->ready()) {
    const auto profile = stream_profile(StreamProfileId::Debug1080);
    const auto codec = impl_->configured ? impl_->requested.codec : profile.codec;
    const auto fps = impl_->configured ? impl_->requested.fps : profile.fps;
    const auto bitrate = impl_->bitrate_bps != 0
                             ? impl_->bitrate_bps
                             : static_cast<std::uint32_t>(profile.initial_bitrate_bps);
    const NvencConfig config{codec, frame.width, frame.height, fps, bitrate, false};
    if (!impl_->encoder->initialize(impl_->capture->device(), impl_->capture->context(), config)) {
      return std::nullopt;
    }
    impl_->active = impl_->encoder->codec_config();
    if (impl_->active.width == 0) {
      impl_->active = {codec, frame.width, frame.height, fps, false, {}};
    }
  }
  const auto timestamp = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          frame.captured_at.time_since_epoch())
      .count());
  const auto encoded = impl_->encoder->encode(frame, timestamp);
  if (!encoded) {
    impl_->keyframe_pending = true;
    impl_->encoder->request_idr();
    return std::nullopt;
  }
  auto result = *encoded;
  result.frame_id = impl_->next_video_id++;
  impl_->keyframe_pending = false;
  impl_->active = impl_->encoder->codec_config();
  return result;
}

CodecConfig WindowsControlledBackend::codec_config() const {
  if (impl_->encoder && impl_->encoder->ready()) {
    return impl_->encoder->codec_config();
  }
  return impl_->active;
}

std::optional<PcmBlock> WindowsControlledBackend::next_audio() {
  if (!impl_->started || !impl_->audio) {
    return std::nullopt;
  }
  const auto block = impl_->audio->read();
  return block ? std::optional<PcmBlock>{*block} : std::nullopt;
}

bool WindowsControlledBackend::inject_input(const DesktopInput& input) {
  return impl_->started && impl_->input && impl_->input->inject(input);
}

void WindowsControlledBackend::clear_input() noexcept {
  if (impl_->input) {
    impl_->input->clear();
  }
}

bool WindowsControlledBackend::submit_gamepad(const GamepadState& state) {
  return impl_->started && impl_->gamepad && impl_->gamepad->submit(state);
}

void WindowsControlledBackend::clear_gamepad() noexcept {
  if (impl_->gamepad) {
    (void)impl_->gamepad->submit(GamepadState{});
  }
}

void WindowsControlledBackend::set_rumble_sender(
    std::function<void(const RumblePacket&)> sender) {
  impl_->rumble_sender = std::move(sender);
  if (impl_->gamepad && impl_->rumble_sender) {
    impl_->gamepad->set_rumble_callback([this](const RumbleState& state) {
      if (impl_->rumble_sender) {
        impl_->rumble_sender({state.low, state.high, 100});
      }
    });
  }
}

}  // namespace ministream
