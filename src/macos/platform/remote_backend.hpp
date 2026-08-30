#pragma once

#include "../../platform/remote_backend.hpp"

#include <memory>

namespace ministream {

class VideoToolboxDecoder;
class VideoSurfaceBridge;
class CoreAudioOutput;
class SdlGamepad;

class MacRemoteBackend final : public RemoteBackend {
 public:
  MacRemoteBackend();
  ~MacRemoteBackend() override;

  [[nodiscard]] RemoteCapabilities inspect() const override;
  bool start() override;
  void stop() noexcept override;
  bool configure_video(const CodecConfig& config) override;
  bool decode_video(std::span<const std::byte> encoded,
                    std::uint64_t timestamp_us) override;
  bool play_audio(std::span<const float> interleaved_stereo) override;
  void play_rumble(std::uint16_t low, std::uint16_t high,
                   std::uint32_t duration_ms) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
