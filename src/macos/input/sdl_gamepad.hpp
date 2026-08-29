#pragma once

#include "core/input/gamepad_state.hpp"
#include "core/time/clock.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace ministream {

class SdlGamepad {
 public:
  SdlGamepad();
  ~SdlGamepad();
  SdlGamepad(SdlGamepad&&) noexcept;
  SdlGamepad& operator=(SdlGamepad&&) noexcept;
  SdlGamepad(const SdlGamepad&) = delete;
  SdlGamepad& operator=(const SdlGamepad&) = delete;

  std::optional<GamepadState> poll_latest();
  bool rumble(std::uint16_t low, std::uint16_t high, Microseconds duration);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ministream
