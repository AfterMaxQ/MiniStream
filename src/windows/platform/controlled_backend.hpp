#pragma once

#include "../../platform/controlled_backend.hpp"

#include <memory>
#include <functional>

namespace ministream {

class DxgiCapture;
class NvencEncoder;
class WasapiLoopback;
class VirtualGamepad;
class RemoteInputSink;

// Native Windows source used by ControlledRuntime.  Frames stay in the
// D3D11 device until NVENC consumes them; audio is read as bounded PCM blocks.
class WindowsControlledBackend final : public ControlledBackend {
 public:
  WindowsControlledBackend();
  ~WindowsControlledBackend() override;

  [[nodiscard]] ControlledCapabilities inspect() const override;
  bool start() override;
  void stop() noexcept override;
  bool configure_video(const CodecConfig& config) override;
  bool reconfigure_bitrate(std::uint32_t bitrate_bps) override;
  [[nodiscard]] std::optional<EncodedFrame> next_video() override;
  [[nodiscard]] CodecConfig codec_config() const override;
  [[nodiscard]] std::optional<PcmBlock> next_audio() override;
  bool inject_input(const DesktopInput& input) override;
  bool submit_gamepad(const GamepadState& state) override;
  void clear_gamepad() noexcept override;
  void set_rumble_sender(std::function<void(const RumblePacket&)> sender) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
