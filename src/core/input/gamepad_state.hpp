#pragma once

#include <cstdint>

namespace ministream {

struct GamepadState {
  std::uint32_t buttons{};
  std::uint16_t left_trigger{};
  std::uint16_t right_trigger{};
  std::int16_t left_x{};
  std::int16_t left_y{};
  std::int16_t right_x{};
  std::int16_t right_y{};

  friend bool operator==(const GamepadState&, const GamepadState&) = default;
};

}  // namespace ministream
