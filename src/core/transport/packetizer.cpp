#include "core/transport/packetizer.hpp"

#include "core/protocol/wire.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <span>

namespace ministream {
namespace {

void put_u16(std::span<std::byte> out, std::uint16_t value) {
  out[0] = static_cast<std::byte>((value >> 8U) & 0xFFU);
  out[1] = static_cast<std::byte>(value & 0xFFU);
}

void put_u32(std::span<std::byte> out, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) {
    out[i] = static_cast<std::byte>((value >> ((3U - i) * 8U)) & 0xFFU);
  }
}

void put_u64(std::span<std::byte> out, std::uint64_t value) {
  for (std::size_t i = 0; i < 8; ++i) {
    out[i] = static_cast<std::byte>((value >> ((7U - i) * 8U)) & 0xFFU);
  }
}

std::uint16_t get_u16(std::span<const std::byte> in) {
  return static_cast<std::uint16_t>(
      (std::to_integer<std::uint16_t>(in[0]) << 8U) |
      std::to_integer<std::uint16_t>(in[1]));
}

std::uint32_t get_u32(std::span<const std::byte> in) {
  std::uint32_t value = 0;
  for (auto byte : in.first<4>()) {
    value = (value << 8U) | std::to_integer<std::uint32_t>(byte);
  }
  return value;
}

std::uint64_t get_u64(std::span<const std::byte> in) {
  std::uint64_t value = 0;
  for (auto byte : in.first<8>()) {
    value = (value << 8U) | std::to_integer<std::uint64_t>(byte);
  }
  return value;
}

std::array<std::byte, kMediaHeaderBytes> encode_media_header(
    const MediaHeader& header, bool keyframe) {
  std::array<std::byte, kMediaHeaderBytes> bytes{};
  put_u32(std::span<std::byte>{bytes}.subspan<0, 4>(), header.packet_seq);
  put_u32(std::span<std::byte>{bytes}.subspan<4, 4>(), header.frame_id);
  put_u16(std::span<std::byte>{bytes}.subspan<8, 2>(), header.shard_index);
  const auto wire_count = static_cast<std::uint16_t>(
      header.shard_count | (keyframe ? 0x8000U : 0U));
  put_u16(std::span<std::byte>{bytes}.subspan<10, 2>(), wire_count);
  put_u64(std::span<std::byte>{bytes}.subspan<12, 8>(), header.capture_timestamp_us);
  return bytes;
}

}  // namespace

std::vector<Datagram> packetize_video(const EncodedFrame& frame, SessionId session_id,
                                     std::size_t shard_payload_bytes) {
  if (frame.bytes.empty() || shard_payload_bytes == 0 ||
      shard_payload_bytes > kVideoShardPayloadBytes) {
    return {};
  }
  const auto shard_count_size =
      (frame.bytes.size() + shard_payload_bytes - 1) / shard_payload_bytes;
  if (shard_count_size > kMaxVideoShards) {
    return {};
  }

  const auto shard_count = static_cast<std::uint16_t>(shard_count_size);
  std::vector<Datagram> datagrams;
  datagrams.reserve(shard_count);
  for (std::uint16_t index = 0; index < shard_count; ++index) {
    const auto offset = static_cast<std::size_t>(index) * shard_payload_bytes;
    const auto payload_size = std::min(shard_payload_bytes, frame.bytes.size() - offset);
    const CommonHeader common{
        PacketType::Video, session_id,
        static_cast<std::uint16_t>(kMediaHeaderBytes + payload_size)};
    const MediaHeader media{
        static_cast<std::uint32_t>(frame.frame_id * 32768U + index), frame.frame_id,
        index, shard_count, frame.capture_timestamp_us};

    const auto common_bytes = encode_common_header(common);
    const auto media_bytes = encode_media_header(media, frame.keyframe);
    Datagram datagram;
    datagram.bytes.reserve(kCommonHeaderBytes + kMediaHeaderBytes + payload_size);
    datagram.bytes.insert(datagram.bytes.end(), common_bytes.begin(), common_bytes.end());
    datagram.bytes.insert(datagram.bytes.end(), media_bytes.begin(), media_bytes.end());
    datagram.bytes.insert(
        datagram.bytes.end(), frame.bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        frame.bytes.begin() + static_cast<std::ptrdiff_t>(offset + payload_size));
    datagrams.push_back(std::move(datagram));
  }
  return datagrams;
}

std::optional<VideoShard> parse_video_datagram(const Datagram& datagram) {
  const auto bytes = std::span<const std::byte>{datagram.bytes};
  if (bytes.size() < kCommonHeaderBytes + kMediaHeaderBytes) {
    return std::nullopt;
  }
  const auto common = decode_common_header(bytes.first<kCommonHeaderBytes>());
  if (!common || common->type != PacketType::Video ||
      bytes.size() != kCommonHeaderBytes + common->payload_bytes) {
    return std::nullopt;
  }

  const auto media_bytes = bytes.subspan<kCommonHeaderBytes, kMediaHeaderBytes>();
  const auto wire_count = get_u16(media_bytes.subspan<10, 2>());
  const auto shard_count = static_cast<std::uint16_t>(wire_count & 0x7FFFU);
  const auto shard_index = get_u16(media_bytes.subspan<8, 2>());
  if (shard_count == 0 || shard_index >= shard_count) {
    return std::nullopt;
  }

  VideoShard shard;
  shard.session_id = common->session_id;
  shard.header = MediaHeader{
      get_u32(media_bytes.subspan<0, 4>()), get_u32(media_bytes.subspan<4, 4>()),
      shard_index, shard_count, get_u64(media_bytes.subspan<12, 8>())};
  shard.keyframe = (wire_count & 0x8000U) != 0;
  const auto payload = bytes.subspan(kCommonHeaderBytes + kMediaHeaderBytes);
  shard.payload.assign(payload.begin(), payload.end());
  return shard;
}

}  // namespace ministream
