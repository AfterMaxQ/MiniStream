#include "core/input/gamepad_packet.hpp"

#include <bit>

namespace ministream {
namespace {

void put_u16(std::span<std::byte> out, std::uint16_t value) {
  out[0] = static_cast<std::byte>(value >> 8U);
  out[1] = static_cast<std::byte>(value);
}

void put_u32(std::span<std::byte> out, std::uint32_t value) {
  for (std::size_t index = 0; index < 4; ++index) {
    out[index] = static_cast<std::byte>(value >> ((3U - index) * 8U));
  }
}

void put_u64(std::span<std::byte> out, std::uint64_t value) {
  for (std::size_t index = 0; index < 8; ++index) {
    out[index] = static_cast<std::byte>(value >> ((7U - index) * 8U));
  }
}

std::uint16_t get_u16(std::span<const std::byte> in) {
  return static_cast<std::uint16_t>(
      (std::to_integer<std::uint16_t>(in[0]) << 8U) |
      std::to_integer<std::uint16_t>(in[1]));
}

std::uint32_t get_u32(std::span<const std::byte> in) {
  std::uint32_t value{};
  for (const auto byte : in.first(4)) {
    value = (value << 8U) | std::to_integer<std::uint32_t>(byte);
  }
  return value;
}

std::uint64_t get_u64(std::span<const std::byte> in) {
  std::uint64_t value{};
  for (const auto byte : in.first(8)) {
    value = (value << 8U) | std::to_integer<std::uint64_t>(byte);
  }
  return value;
}

}  // namespace

std::array<std::byte, kGamepadPacketBytes> encode_gamepad_packet(
    const GamepadPacket& packet) {
  std::array<std::byte, kGamepadPacketBytes> bytes{};
  put_u32(std::span{bytes}.subspan<0, 4>(), packet.sequence);
  put_u64(std::span{bytes}.subspan<4, 8>(), packet.client_timestamp_us);
  put_u32(std::span{bytes}.subspan<12, 4>(), packet.state.buttons);
  put_u16(std::span{bytes}.subspan<16, 2>(), packet.state.left_trigger);
  put_u16(std::span{bytes}.subspan<18, 2>(), packet.state.right_trigger);
  put_u16(std::span{bytes}.subspan<20, 2>(), std::bit_cast<std::uint16_t>(packet.state.left_x));
  put_u16(std::span{bytes}.subspan<22, 2>(), std::bit_cast<std::uint16_t>(packet.state.left_y));
  put_u16(std::span{bytes}.subspan<24, 2>(), std::bit_cast<std::uint16_t>(packet.state.right_x));
  put_u16(std::span{bytes}.subspan<26, 2>(), std::bit_cast<std::uint16_t>(packet.state.right_y));
  return bytes;
}

std::optional<GamepadPacket> decode_gamepad_packet(std::span<const std::byte> bytes) {
  if (bytes.size() != kGamepadPacketBytes) {
    return std::nullopt;
  }
  return GamepadPacket{
      get_u32(bytes.subspan<0, 4>()),
      get_u64(bytes.subspan<4, 8>()),
      {get_u32(bytes.subspan<12, 4>()),
       get_u16(bytes.subspan<16, 2>()),
       get_u16(bytes.subspan<18, 2>()),
       std::bit_cast<std::int16_t>(get_u16(bytes.subspan<20, 2>())),
       std::bit_cast<std::int16_t>(get_u16(bytes.subspan<22, 2>())),
       std::bit_cast<std::int16_t>(get_u16(bytes.subspan<24, 2>())),
       std::bit_cast<std::int16_t>(get_u16(bytes.subspan<26, 2>()))}};
}

bool gamepad_sequence_is_newer(std::uint32_t candidate, std::uint32_t reference) noexcept {
  return candidate != reference &&
         static_cast<std::int32_t>(candidate - reference) > 0;
}

bool GamepadSequenceFilter::accept(std::uint32_t sequence) noexcept {
  if (latest_.has_value() && !gamepad_sequence_is_newer(sequence, *latest_)) {
    return false;
  }
  latest_ = sequence;
  return true;
}

}  // namespace ministream
