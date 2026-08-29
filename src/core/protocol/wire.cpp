#include "core/protocol/wire.hpp"

#include <limits>

namespace ministream {
namespace {

void put_u16(std::span<std::byte> out, std::uint16_t value) {
  out[0] = static_cast<std::byte>((value >> 8U) & 0xFFU);
  out[1] = static_cast<std::byte>(value & 0xFFU);
}

void put_u32(std::span<std::byte> out, std::uint32_t value) {
  out[0] = static_cast<std::byte>((value >> 24U) & 0xFFU);
  out[1] = static_cast<std::byte>((value >> 16U) & 0xFFU);
  out[2] = static_cast<std::byte>((value >> 8U) & 0xFFU);
  out[3] = static_cast<std::byte>(value & 0xFFU);
}

std::uint16_t get_u16(std::span<const std::byte> in) {
  return static_cast<std::uint16_t>(
      (std::to_integer<std::uint16_t>(in[0]) << 8U) |
      std::to_integer<std::uint16_t>(in[1]));
}

std::uint32_t get_u32(std::span<const std::byte> in) {
  return (std::to_integer<std::uint32_t>(in[0]) << 24U) |
         (std::to_integer<std::uint32_t>(in[1]) << 16U) |
         (std::to_integer<std::uint32_t>(in[2]) << 8U) |
         std::to_integer<std::uint32_t>(in[3]);
}

bool is_known_packet_type(std::uint8_t value) {
  return value >= static_cast<std::uint8_t>(PacketType::Control) &&
         value <= static_cast<std::uint8_t>(PacketType::Telemetry);
}

}  // namespace

std::array<std::byte, kCommonHeaderBytes> encode_common_header(const CommonHeader& header) {
  std::array<std::byte, kCommonHeaderBytes> bytes{};
  put_u32(std::span<std::byte>{bytes}.subspan<0, 4>(), kProtocolMagic);
  bytes[4] = static_cast<std::byte>(kProtocolVersion);
  bytes[5] = static_cast<std::byte>(header.type);
  put_u32(std::span<std::byte>{bytes}.subspan<6, 4>(), header.session_id);
  put_u16(std::span<std::byte>{bytes}.subspan<10, 2>(), header.payload_bytes);
  return bytes;
}

std::optional<CommonHeader> decode_common_header(std::span<const std::byte> bytes) {
  if (bytes.size() < kCommonHeaderBytes || get_u32(bytes.first<4>()) != kProtocolMagic ||
      std::to_integer<std::uint8_t>(bytes[4]) != kProtocolVersion) {
    return std::nullopt;
  }

  const auto raw_type = std::to_integer<std::uint8_t>(bytes[5]);
  const auto payload_bytes = get_u16(bytes.subspan<10, 2>());
  if (!is_known_packet_type(raw_type) ||
      payload_bytes > kMaxDatagramBytes - kCommonHeaderBytes) {
    return std::nullopt;
  }

  return CommonHeader{
      static_cast<PacketType>(raw_type), get_u32(bytes.subspan<6, 4>()), payload_bytes};
}

}  // namespace ministream
