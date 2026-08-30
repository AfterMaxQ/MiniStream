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
  std::function<void(const RumblePacket&)> rumble_sender;
};

WindowsControlledBackend::WindowsControlledBackend() : impl_(std::make_unique<Impl>()) {}
WindowsControlledBackend::~WindowsControlledBackend() { stop(); }

ControlledCapabilities WindowsControlledBackend::inspect() const {
  const auto host = inspect_host_capabilities();
  return {{host.video.ready, host.video.detail},
          {host.audio.ready, host.audio.detail},
          {host.input.ready, host.input.detail},
          {host.network.ready, host.network.detail},
          {host.controller.ready, host.controller.detail},
          host.h264,
          host.hevc,
          host.hdr10,
          host.max_width,
          host.max_height,
          host.max_fps};
}

bool WindowsControlledBackend::start() {
  if (impl_->started) {
    return true;
  }
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
  impl_->encoder = std::make_unique<NvencEncoder>();
  impl_->requested = {};
  impl_->active = {};
  impl_->bitrate_bps = 20'000'000;
  impl_->configured = false;
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
  if (impl_->encoder) {
    impl_->encoder->stop();
  }
  impl_->gamepad.reset();
  impl_->encoder.reset();
  impl_->audio.reset();
  impl_->capture.reset();
  impl_->input.reset();
  impl_->requested = {};
  impl_->active = {};
  impl_->bitrate_bps = 20'000'000;
  impl_->configured = false;
  impl_->started = false;
}

bool WindowsControlledBackend::configure_video(const CodecConfig& config) {
  if (!impl_->started || config.width == 0 || config.height == 0 || config.fps == 0 ||
      (config.codec != VideoCodec::H264 && config.codec != VideoCodec::Hevc) || config.hdr10) {
    return false;
  }
  impl_->requested = config;
  impl_->configured = true;
  // The source adapter has no scaler, so it preserves the capture dimensions
  // when the first frame arrives.  The requested codec/fps/bitrate remain in
  // effect and the emitted CodecConfig reports the actual dimensions.
  if (impl_->encoder) {
    impl_->encoder->stop();
  }
  impl_->active = {};
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
  if (impl_->encoder) {
    impl_->encoder->request_idr();
  }
}

std::optional<EncodedFrame> WindowsControlledBackend::next_video() {
  if (!impl_->started || !impl_->capture || !impl_->encoder) {
    return std::nullopt;
  }
  const auto captured = impl_->capture->acquire(Microseconds{0});
  if (!captured) {
    return std::nullopt;
  }
  auto frame = *captured;
  if (impl_->configured &&
      (frame.width != impl_->requested.width || frame.height != impl_->requested.height)) {
    const auto resized = impl_->capture->resize(
        frame, impl_->requested.width, impl_->requested.height);
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
    return std::nullopt;
  }
  impl_->active = impl_->encoder->codec_config();
  return *encoded;
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
