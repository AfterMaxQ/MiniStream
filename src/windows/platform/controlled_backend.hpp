#pragma once

#include "../../platform/controlled_backend.hpp"

#include <memory>

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
  [[nodiscard]] std::optional<EncodedFrame> next_video() override;
  [[nodiscard]] CodecConfig codec_config() const override;
  [[nodiscard]] std::optional<PcmBlock> next_audio() override;
  bool inject_input(const DesktopInput& input) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
