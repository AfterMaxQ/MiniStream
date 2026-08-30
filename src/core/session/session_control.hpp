#pragma once

#include "core/protocol/value_types.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>

namespace ministream {

inline constexpr std::byte kDisconnectControlPayload{0xD1};
inline constexpr std::byte kRequestKeyframeControlPayload{0xD2};
inline constexpr std::byte kInputAckControlPayload{0xD3};
inline constexpr std::byte kHeartbeatControlPayload{0xD4};

inline std::array<std::byte, 1> encode_disconnect_control() noexcept {
  return {kDisconnectControlPayload};
}

inline bool is_disconnect_control(std::span<const std::byte> bytes) noexcept {
  return bytes.size() == 1 && bytes.front() == kDisconnectControlPayload;
}

inline std::array<std::byte, 1> encode_request_keyframe_control() noexcept {
  return {kRequestKeyframeControlPayload};
}

inline bool is_request_keyframe_control(std::span<const std::byte> bytes) noexcept {
  return bytes.size() == 1 && bytes.front() == kRequestKeyframeControlPayload;
}

inline std::array<std::byte, 1> encode_heartbeat_control() noexcept {
  return {kHeartbeatControlPayload};
}

inline bool is_heartbeat_control(std::span<const std::byte> bytes) noexcept {
  return bytes.size() == 1 && bytes.front() == kHeartbeatControlPayload;
}

inline std::array<std::byte, 5> encode_input_ack_control(ControlSeq sequence) noexcept {
  return {kInputAckControlPayload, static_cast<std::byte>(sequence >> 24U),
          static_cast<std::byte>(sequence >> 16U), static_cast<std::byte>(sequence >> 8U),
          static_cast<std::byte>(sequence)};
}

inline std::optional<ControlSeq> decode_input_ack_control(
    std::span<const std::byte> bytes) noexcept {
  if (bytes.size() != 5 || bytes.front() != kInputAckControlPayload) {
    return std::nullopt;
  }
  return (std::to_integer<ControlSeq>(bytes[1]) << 24U) |
         (std::to_integer<ControlSeq>(bytes[2]) << 16U) |
         (std::to_integer<ControlSeq>(bytes[3]) << 8U) |
         std::to_integer<ControlSeq>(bytes[4]);
}

}  // namespace ministream
