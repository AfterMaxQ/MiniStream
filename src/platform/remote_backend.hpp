#pragma once

#include "core/input/gamepad_state.hpp"
#include "core/time/clock.hpp"
#include "core/video/codec_config.hpp"
#include "platform/capabilities.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ministream {

class RemoteBackend {
 public:
  virtual ~RemoteBackend() = default;

  [[nodiscard]] virtual RemoteCapabilities inspect() const = 0;
  virtual bool start() = 0;
  virtual void stop() noexcept = 0;
  virtual bool configure_video(const CodecConfig& config) = 0;
  virtual bool decode_video(std::span<const std::byte> encoded,
                            std::uint64_t timestamp_us) = 0;
  virtual bool play_audio(std::span<const float> interleaved_stereo) = 0;
  virtual void play_rumble(std::uint16_t low, std::uint16_t high,
                           std::uint32_t duration_ms) = 0;
  virtual void clear_rumble() noexcept {}
  virtual void tick(SteadyClock::time_point) noexcept {}
  virtual std::optional<GamepadState> poll_gamepad() { return std::nullopt; }
};

}  // namespace ministream
