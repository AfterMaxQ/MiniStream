#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ministream {

struct RumblePacket {
  std::uint16_t low{};
  std::uint16_t high{};
  std::uint16_t duration_ms{};

  friend bool operator==(const RumblePacket&, const RumblePacket&) = default;
};

std::array<std::byte, 6> encode_rumble_packet(const RumblePacket& packet);
std::optional<RumblePacket> decode_rumble_packet(std::span<const std::byte> bytes);

}  // namespace ministream
