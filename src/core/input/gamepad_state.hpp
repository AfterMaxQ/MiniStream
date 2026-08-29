#pragma once

#include <cstdint>

namespace ministream {

inline constexpr std::uint32_t kGamepadDpadUp = 0x0001U;
inline constexpr std::uint32_t kGamepadDpadDown = 0x0002U;
inline constexpr std::uint32_t kGamepadDpadLeft = 0x0004U;
inline constexpr std::uint32_t kGamepadDpadRight = 0x0008U;
inline constexpr std::uint32_t kGamepadStart = 0x0010U;
inline constexpr std::uint32_t kGamepadBack = 0x0020U;
inline constexpr std::uint32_t kGamepadLeftThumb = 0x0040U;
inline constexpr std::uint32_t kGamepadRightThumb = 0x0080U;
inline constexpr std::uint32_t kGamepadLeftShoulder = 0x0100U;
inline constexpr std::uint32_t kGamepadRightShoulder = 0x0200U;
inline constexpr std::uint32_t kGamepadGuide = 0x0400U;
inline constexpr std::uint32_t kGamepadA = 0x1000U;
inline constexpr std::uint32_t kGamepadB = 0x2000U;
inline constexpr std::uint32_t kGamepadX = 0x4000U;
inline constexpr std::uint32_t kGamepadY = 0x8000U;

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
