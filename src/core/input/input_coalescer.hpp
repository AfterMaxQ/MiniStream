#pragma once

#include "core/input/gamepad_packet.hpp"
#include "core/time/clock.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

namespace ministream {

class InputCoalescer {
 public:
  void update(GamepadState state, SteadyClock::time_point now) noexcept;
  std::optional<GamepadPacket> flush_if_due(SteadyClock::time_point now) noexcept;

 private:
  static constexpr auto kWindow = std::chrono::milliseconds{1};
  std::optional<GamepadState> pending_;
  SteadyClock::time_point due_at_{};
  std::uint32_t next_sequence_{};
};

}  // namespace ministream
