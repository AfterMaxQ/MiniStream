#pragma once

#include "core/audio/audio_packet.hpp"
#include "core/audio/pcm_block.hpp"
#include "core/input/desktop_input.hpp"
#include "core/transport/packetizer.hpp"
#include "core/video/codec_config.hpp"
#include "platform/capabilities.hpp"

#include <optional>

namespace ministream {

class ControlledBackend {
 public:
  virtual ~ControlledBackend() = default;

  [[nodiscard]] virtual ControlledCapabilities inspect() const = 0;
  virtual bool start() = 0;
  virtual void stop() noexcept = 0;
  [[nodiscard]] virtual std::optional<EncodedFrame> next_video() = 0;
  [[nodiscard]] virtual CodecConfig codec_config() const { return {}; }
  [[nodiscard]] virtual std::optional<PcmBlock> next_audio() = 0;
  virtual bool inject_input(const DesktopInput& input) = 0;
};

}  // namespace ministream
