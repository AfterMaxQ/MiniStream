#pragma once

#include "core/input/gamepad_state.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ministream {

inline constexpr std::size_t kGamepadPacketBytes = 28;

struct GamepadPacket {
  std::uint32_t sequence{};
  std::uint64_t client_timestamp_us{};
  GamepadState state{};

  friend bool operator==(const GamepadPacket&, const GamepadPacket&) = default;
};

std::array<std::byte, kGamepadPacketBytes> encode_gamepad_packet(
    const GamepadPacket& packet);
std::optional<GamepadPacket> decode_gamepad_packet(std::span<const std::byte> bytes);
bool gamepad_sequence_is_newer(std::uint32_t candidate, std::uint32_t reference) noexcept;

class GamepadSequenceFilter {
 public:
  bool accept(std::uint32_t sequence) noexcept;

 private:
  std::optional<std::uint32_t> latest_;
};

}  // namespace ministream
