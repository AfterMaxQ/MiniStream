#include "core/input/rumble_packet.hpp"

namespace ministream {
namespace {

void put_u16(std::span<std::byte> out, std::uint16_t value) {
  out[0] = static_cast<std::byte>(value >> 8U);
  out[1] = static_cast<std::byte>(value);
}

std::uint16_t get_u16(std::span<const std::byte> in) {
  return static_cast<std::uint16_t>(
      (std::to_integer<std::uint16_t>(in[0]) << 8U) |
      std::to_integer<std::uint16_t>(in[1]));
}

}  // namespace

std::array<std::byte, 6> encode_rumble_packet(const RumblePacket& packet) {
  std::array<std::byte, 6> bytes{};
  put_u16(std::span{bytes}.subspan<0, 2>(), packet.low);
  put_u16(std::span{bytes}.subspan<2, 2>(), packet.high);
  put_u16(std::span{bytes}.subspan<4, 2>(), packet.duration_ms);
  return bytes;
}

std::optional<RumblePacket> decode_rumble_packet(std::span<const std::byte> bytes) {
  if (bytes.size() != 6) {
    return std::nullopt;
  }
  return RumblePacket{get_u16(bytes.subspan<0, 2>()), get_u16(bytes.subspan<2, 2>()),
                      get_u16(bytes.subspan<4, 2>())};
}

}  // namespace ministream
