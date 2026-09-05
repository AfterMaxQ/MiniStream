#pragma once

#include "../../platform/controlled_backend.hpp"

#include <memory>
#include <functional>

namespace ministream {

class MacControlledBackend final : public ControlledBackend {
 public:
  MacControlledBackend();
  ~MacControlledBackend() override;
  static void request_permissions();

  [[nodiscard]] ControlledCapabilities inspect() const override;
  bool start() override;
  void stop() noexcept override;
  bool configure_video(const CodecConfig& config) override;
  bool reconfigure_bitrate(std::uint32_t bitrate_bps) override;
  void request_keyframe() noexcept override;
  [[nodiscard]] std::optional<EncodedFrame> next_video() override;
  [[nodiscard]] CodecConfig codec_config() const override;
  [[nodiscard]] std::optional<PcmBlock> next_audio() override;
  bool inject_input(const DesktopInput& input) override;
  void clear_input() noexcept override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
