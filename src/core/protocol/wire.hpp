#pragma once

#include "core/protocol/types.hpp"
#include "core/protocol/value_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ministream {

inline constexpr std::uint32_t kProtocolMagic = 0x4D535452U;
inline constexpr std::uint8_t kProtocolVersion = 2;
inline constexpr std::size_t kCommonHeaderBytes = 12;

struct CommonHeader {
  PacketType type{PacketType::Control};
  SessionId session_id{};
  std::uint16_t payload_bytes{};
};

std::array<std::byte, kCommonHeaderBytes> encode_common_header(const CommonHeader& header);
std::optional<CommonHeader> decode_common_header(std::span<const std::byte> bytes);

}  // namespace ministream
