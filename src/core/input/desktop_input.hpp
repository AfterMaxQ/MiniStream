#pragma once

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
};

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
