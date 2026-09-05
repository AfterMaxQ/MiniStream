#pragma once

#include "../../platform/remote_backend.hpp"

#include <functional>
#include <memory>
#include <optional>

namespace ministream {

class MfDecoder;
class D3D11VideoSurface;
class WasapiOutput;
struct D3D11SurfaceFrame;

class WindowsRemoteBackend final : public RemoteBackend {
 public:
  WindowsRemoteBackend();
  ~WindowsRemoteBackend() override;

  [[nodiscard]] RemoteCapabilities inspect() const override;
  bool start() override;
  void stop() noexcept override;
  bool configure_video(const CodecConfig& config) override;
  void reset_video() noexcept override;
  bool decode_video(std::span<const std::byte> encoded,
                    std::uint64_t timestamp_us) override;
  bool play_audio(std::span<const float> interleaved_stereo) override;
  void play_rumble(std::uint16_t low, std::uint16_t high,
                   std::uint32_t duration_ms) override;
  void clear_rumble() noexcept override;
  void tick(SteadyClock::time_point now) noexcept override;
  std::optional<GamepadState> poll_gamepad() override;

  // The Qt surface consumes the latest GPU texture on the render thread.
  // Keeping this accessor outside the shared RemoteBackend contract leaves
  // D3D11 types confined to the Windows adapter.
  [[nodiscard]] bool surface_available() const noexcept;
  std::optional<D3D11SurfaceFrame> take_surface_frame();
  void set_surface_notifier(std::function<void()> notifier);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
