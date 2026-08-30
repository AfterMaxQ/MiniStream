#pragma once

#include <array>
#include <cstddef>
#include <span>

namespace ministream {

inline constexpr std::byte kDisconnectControlPayload{0xD1};

inline std::array<std::byte, 1> encode_disconnect_control() noexcept {
  return {kDisconnectControlPayload};
}

inline bool is_disconnect_control(std::span<const std::byte> bytes) noexcept {
  return bytes.size() == 1 && bytes.front() == kDisconnectControlPayload;
}

}  // namespace ministream
