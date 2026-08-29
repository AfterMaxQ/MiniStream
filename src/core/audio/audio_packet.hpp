#pragma once

#include "core/protocol/value_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ministream {

inline constexpr std::size_t kAudioHeaderBytes = 16;

struct AudioPacket {
  std::uint32_t sequence{};
  std::uint64_t host_timestamp_us{};
  std::uint16_t sample_count{};
  std::vector<std::byte> opus;
  bool operator==(const AudioPacket&) const = default;
};

std::vector<std::byte> encode_audio_packet(const AudioPacket& packet);
std::optional<AudioPacket> decode_audio_packet(std::span<const std::byte> bytes);
bool sequence_is_newer(std::uint32_t candidate, std::uint32_t previous) noexcept;

}  // namespace ministream
