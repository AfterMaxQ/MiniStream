#pragma once

#include "core/input/desktop_key.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ministream {

enum class DesktopInputKind : std::uint8_t {
  Key = 1,
  MouseMove = 2,
  MouseButton = 3,
  MouseWheel = 4,
  ReleaseAll = 5,
};

// Mouse buttons use the same small wire representation on every platform.
// The high bit marks a release; the remaining bits identify the button.
enum class DesktopMouseButton : std::uint16_t {
  Left = 1,
  Right = 2,
  Middle = 3,
};

inline constexpr std::uint16_t kDesktopMouseRelease = 0x8000U;
inline constexpr std::uint16_t kDesktopKeyRelease = 0x0001U;

struct DesktopInput {
  DesktopInputKind kind{DesktopInputKind::Key};
  std::uint16_t flags{};
  std::int32_t x{};
  std::int32_t y{};
  std::uint16_t data{};
  bool operator==(const DesktopInput&) const = default;
};

inline constexpr std::size_t kDesktopInputBytes = 16;

std::vector<std::byte> encode_desktop_input(const DesktopInput& input);
std::optional<DesktopInput> decode_desktop_input(std::span<const std::byte> bytes);

}  // namespace ministream
