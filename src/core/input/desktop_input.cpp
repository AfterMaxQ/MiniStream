#include "core/input/desktop_input.hpp"

namespace ministream {
namespace {
bool valid_mouse_button(std::uint16_t flags) {
  const auto button = flags & ~kDesktopMouseRelease;
  return (flags & ~(kDesktopMouseRelease | 0x00FFU)) == 0U &&
         button >= static_cast<std::uint16_t>(DesktopMouseButton::Left) &&
         button <= static_cast<std::uint16_t>(DesktopMouseButton::Middle);
}

void put(std::byte* out, std::uint32_t value) {
  out[0] = static_cast<std::byte>(value >> 24U);
  out[1] = static_cast<std::byte>(value >> 16U);
  out[2] = static_cast<std::byte>(value >> 8U);
  out[3] = static_cast<std::byte>(value);
}
std::uint32_t get(const std::byte* in) {
  return (std::to_integer<std::uint32_t>(in[0]) << 24U) |
         (std::to_integer<std::uint32_t>(in[1]) << 16U) |
         (std::to_integer<std::uint32_t>(in[2]) << 8U) |
         std::to_integer<std::uint32_t>(in[3]);
}
void put16(std::byte* out, std::uint16_t value) {
  out[0] = static_cast<std::byte>(value >> 8U);
  out[1] = static_cast<std::byte>(value);
}
std::uint16_t get16(const std::byte* in) {
  return static_cast<std::uint16_t>((std::to_integer<std::uint16_t>(in[0]) << 8U) |
                                    std::to_integer<std::uint16_t>(in[1]));
}
}  // namespace

std::vector<std::byte> encode_desktop_input(const DesktopInput& input) {
  if (input.kind < DesktopInputKind::Key || input.kind > DesktopInputKind::ReleaseAll) {
    return {};
  }
  if (input.kind == DesktopInputKind::Key &&
      (!desktop_key_from_wire(input.data) || (input.flags & ~kDesktopKeyRelease) != 0U ||
       input.x != 0 || input.y != 0)) {
    return {};
  }
  if (input.kind == DesktopInputKind::MouseButton &&
      (!valid_mouse_button(input.flags) || input.x != 0 || input.y != 0 || input.data != 0)) {
    return {};
  }
  if (input.kind == DesktopInputKind::ReleaseAll &&
      (input.flags != 0 || input.x != 0 || input.y != 0 || input.data != 0)) {
    return {};
  }
  std::vector<std::byte> bytes(kDesktopInputBytes);
  bytes[0] = static_cast<std::byte>(input.kind);
  put16(bytes.data() + 1, input.flags);
  put(bytes.data() + 3, static_cast<std::uint32_t>(input.x));
  put(bytes.data() + 7, static_cast<std::uint32_t>(input.y));
  put16(bytes.data() + 11, input.data);
  return bytes;
}

std::optional<DesktopInput> decode_desktop_input(std::span<const std::byte> bytes) {
  if (bytes.size() != kDesktopInputBytes) {
    return std::nullopt;
  }
  const auto raw_kind = std::to_integer<std::uint8_t>(bytes[0]);
  if (raw_kind < static_cast<std::uint8_t>(DesktopInputKind::Key) ||
      raw_kind > static_cast<std::uint8_t>(DesktopInputKind::ReleaseAll)) {
    return std::nullopt;
  }
  DesktopInput input;
  input.kind = static_cast<DesktopInputKind>(raw_kind);
  input.flags = get16(bytes.data() + 1);
  input.x = static_cast<std::int32_t>(get(bytes.data() + 3));
  input.y = static_cast<std::int32_t>(get(bytes.data() + 7));
  input.data = get16(bytes.data() + 11);
  if (input.kind == DesktopInputKind::Key &&
      (!desktop_key_from_wire(input.data) || (input.flags & ~kDesktopKeyRelease) != 0U ||
       input.x != 0 || input.y != 0)) {
    return std::nullopt;
  }
  if (input.kind == DesktopInputKind::MouseButton &&
      (!valid_mouse_button(input.flags) || input.x != 0 || input.y != 0 || input.data != 0)) {
    return std::nullopt;
  }
  if (input.kind == DesktopInputKind::ReleaseAll &&
      (input.flags != 0 || input.x != 0 || input.y != 0 || input.data != 0)) {
    return std::nullopt;
  }
  return input;
}

}  // namespace ministream
