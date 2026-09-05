#include "core/audio/audio_packet.hpp"
#include "core/audio/opus_codec.hpp"
#include "core/transport/packetizer.hpp"

namespace ministream {
namespace {
void put(std::vector<std::byte>& output, std::uint64_t value, std::size_t bytes) {
  for (std::size_t i = 0; i < bytes; ++i) {
    output.push_back(static_cast<std::byte>((value >> ((bytes - i - 1U) * 8U)) & 0xFFU));
  }
}
std::uint64_t get(std::span<const std::byte> input) {
  std::uint64_t value = 0;
  for (auto byte : input) {
    value = (value << 8U) | std::to_integer<std::uint64_t>(byte);
  }
  return value;
}
}  // namespace

std::vector<std::byte> encode_audio_packet(const AudioPacket& packet) {
  if (packet.opus.empty() || packet.sample_count != kOpusFrameSamplesPerChannel ||
      packet.opus.size() > kMaxSealedPayloadBytes - kAudioHeaderBytes) {
    return {};
  }
  std::vector<std::byte> bytes;
  bytes.reserve(kAudioHeaderBytes + packet.opus.size());
  put(bytes, packet.sequence, 4);
  put(bytes, packet.host_timestamp_us, 8);
  put(bytes, packet.sample_count, 2);
  put(bytes, packet.opus.size(), 2);
  bytes.insert(bytes.end(), packet.opus.begin(), packet.opus.end());
  return bytes;
}

std::optional<AudioPacket> decode_audio_packet(std::span<const std::byte> bytes) {
  if (bytes.size() <= kAudioHeaderBytes || bytes.size() > kMaxSealedPayloadBytes) {
    return std::nullopt;
  }
  const auto payload_bytes = static_cast<std::size_t>(get(bytes.subspan(14, 2)));
  if (bytes.size() != kAudioHeaderBytes + payload_bytes) {
    return std::nullopt;
  }
  AudioPacket packet;
  packet.sequence = static_cast<std::uint32_t>(get(bytes.subspan(0, 4)));
  packet.host_timestamp_us = get(bytes.subspan(4, 8));
  packet.sample_count = static_cast<std::uint16_t>(get(bytes.subspan(12, 2)));
  if (packet.sample_count != kOpusFrameSamplesPerChannel) {
    return std::nullopt;
  }
  packet.opus.assign(bytes.begin() + kAudioHeaderBytes, bytes.end());
  return packet;
}

bool sequence_is_newer(std::uint32_t candidate, std::uint32_t previous) noexcept {
  return candidate != previous &&
         static_cast<std::int32_t>(candidate - previous) > 0;
}

}  // namespace ministream
