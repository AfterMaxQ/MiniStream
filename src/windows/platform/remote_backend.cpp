#include "windows/platform/remote_backend.hpp"

#include "core/net/udp_endpoint.hpp"
#include "windows/audio/wasapi_output.hpp"
#include "windows/platform/host_capabilities.hpp"
#include "windows/video/d3d11_video_surface.hpp"
#include "windows/video/mf_decoder.hpp"

#include <Windows.h>
#include <Xinput.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace ministream {

struct WindowsRemoteBackend::Impl {
  std::unique_ptr<MfDecoder> decoder;
  std::unique_ptr<D3D11VideoSurface> surface;
  std::unique_ptr<WasapiOutput> audio;
  std::function<void()> surface_notifier;
  std::optional<SteadyClock::time_point> rumble_deadline;
  bool started{};
};

WindowsRemoteBackend::WindowsRemoteBackend() : impl_(std::make_unique<Impl>()) {}
WindowsRemoteBackend::~WindowsRemoteBackend() { stop(); }

RemoteCapabilities WindowsRemoteBackend::inspect() const {
  const auto audio = inspect_host_capabilities().audio;
  const auto network = inspect_host_capabilities().network;
  const bool h264 = MfDecoder::hardware_available(VideoCodec::H264);
  const bool hevc = MfDecoder::hardware_available(VideoCodec::Hevc);
  return {{h264 || hevc, (h264 || hevc) ? "Media Foundation H.264/HEVC decoder detected"
                                         : "Media Foundation H.264/HEVC decoder unavailable"},
          {audio.ready, audio.detail},
          {true, "Window-local keyboard and mouse"},
          {network.ready, network.detail},
          h264,
          hevc,
          false,
          (h264 || hevc) ? 3840U : 0U,
          (h264 || hevc) ? 2160U : 0U,
          (h264 || hevc) ? 60U : 0U};
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
  clear_rumble();
  impl_->started = false;
}

bool WindowsRemoteBackend::configure_video(const CodecConfig& config) {
  return impl_->started && impl_->decoder && impl_->decoder->start() &&
         impl_->decoder->configure(config);
}

void WindowsRemoteBackend::reset_video() noexcept {
  if (impl_->decoder) impl_->decoder->stop();
  if (impl_->surface) impl_->surface->clear();
  if (impl_->surface_notifier) impl_->surface_notifier();
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
    if (impl_->surface_notifier) {
      impl_->surface_notifier();
    }
  }
  return true;
}

bool WindowsRemoteBackend::play_audio(std::span<const float> interleaved_stereo) {
  return impl_->started && impl_->audio && impl_->audio->push(interleaved_stereo);
}

void WindowsRemoteBackend::play_rumble(std::uint16_t low, std::uint16_t high,
                                       std::uint32_t duration_ms) {
  if (duration_ms == 0) {
    clear_rumble();
    return;
  }
  XINPUT_VIBRATION vibration{};
  vibration.wLeftMotorSpeed = static_cast<WORD>(low);
  vibration.wRightMotorSpeed = static_cast<WORD>(high);
  XInputSetState(0, &vibration);
  impl_->rumble_deadline = SteadyClock::now() + std::chrono::milliseconds{duration_ms};
}

void WindowsRemoteBackend::clear_rumble() noexcept {
  XINPUT_VIBRATION vibration{};
  XInputSetState(0, &vibration);
  if (impl_) {
    impl_->rumble_deadline.reset();
  }
}

void WindowsRemoteBackend::tick(SteadyClock::time_point now) noexcept {
  if (impl_ && impl_->rumble_deadline && now >= *impl_->rumble_deadline) {
    clear_rumble();
  }
}

std::optional<GamepadState> WindowsRemoteBackend::poll_gamepad() {
  XINPUT_STATE state{};
  if (XInputGetState(0, &state) != ERROR_SUCCESS) {
    return std::nullopt;
  }
  const auto buttons = state.Gamepad.wButtons;
  std::uint32_t mapped = 0;
  mapped |= (buttons & XINPUT_GAMEPAD_DPAD_UP) ? kGamepadDpadUp : 0U;
  mapped |= (buttons & XINPUT_GAMEPAD_DPAD_DOWN) ? kGamepadDpadDown : 0U;
  mapped |= (buttons & XINPUT_GAMEPAD_DPAD_LEFT) ? kGamepadDpadLeft : 0U;
  mapped |= (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) ? kGamepadDpadRight : 0U;
  mapped |= (buttons & XINPUT_GAMEPAD_START) ? kGamepadStart : 0U;
  mapped |= (buttons & XINPUT_GAMEPAD_BACK) ? kGamepadBack : 0U;
  mapped |= (buttons & XINPUT_GAMEPAD_LEFT_THUMB) ? kGamepadLeftThumb : 0U;
  mapped |= (buttons & XINPUT_GAMEPAD_RIGHT_THUMB) ? kGamepadRightThumb : 0U;
  mapped |= (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) ? kGamepadLeftShoulder : 0U;
  mapped |= (buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) ? kGamepadRightShoulder : 0U;
  mapped |= (buttons & XINPUT_GAMEPAD_A) ? kGamepadA : 0U;
  mapped |= (buttons & XINPUT_GAMEPAD_B) ? kGamepadB : 0U;
  mapped |= (buttons & XINPUT_GAMEPAD_X) ? kGamepadX : 0U;
  mapped |= (buttons & XINPUT_GAMEPAD_Y) ? kGamepadY : 0U;
  return GamepadState{mapped,
                      static_cast<std::uint16_t>(state.Gamepad.bLeftTrigger * 257U),
                      static_cast<std::uint16_t>(state.Gamepad.bRightTrigger * 257U),
                      state.Gamepad.sThumbLX, state.Gamepad.sThumbLY,
                      state.Gamepad.sThumbRX, state.Gamepad.sThumbRY};
}

std::optional<D3D11SurfaceFrame> WindowsRemoteBackend::take_surface_frame() {
  return impl_->surface ? impl_->surface->take_latest() : std::nullopt;
}

bool WindowsRemoteBackend::surface_available() const noexcept {
  return impl_->surface && impl_->surface->available();
}

void WindowsRemoteBackend::set_surface_notifier(std::function<void()> notifier) {
  impl_->surface_notifier = std::move(notifier);
}

}  // namespace ministream
