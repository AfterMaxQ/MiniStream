#pragma once

#include "core/protocol/value_types.hpp"
#include "core/protocol/wire.hpp"
#include "core/transport/media_header.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ministream {

inline constexpr std::size_t kMediaHeaderBytes = 20;
// SessionCrypto adds a common routing header, an eight-byte nonce counter,
// and a ChaCha20-Poly1305 authentication tag around every media payload.
// Keep the plaintext shard below that envelope so the authenticated datagram
// remains within the 1200-byte wire budget.
inline constexpr std::size_t kSessionCryptoOverheadBytes =
    kCommonHeaderBytes + sizeof(std::uint64_t) + 16;
inline constexpr std::size_t kMaxSealedPayloadBytes =
    kMaxDatagramBytes - kSessionCryptoOverheadBytes;
inline constexpr std::size_t kVideoShardPayloadBytes =
    kMaxSealedPayloadBytes - kMediaHeaderBytes;
inline constexpr std::uint16_t kMaxVideoShards = 0x7FFFU;

struct EncodedFrame {
  std::uint32_t frame_id{};
  std::uint64_t capture_timestamp_us{};
  bool keyframe{};
  std::vector<std::byte> bytes;
};

struct VideoShard {
  SessionId session_id{};
  MediaHeader header;
  bool keyframe{};
  std::vector<std::byte> payload;
};

std::vector<Datagram> packetize_video(
    const EncodedFrame& frame, SessionId session_id,
    std::size_t shard_payload_bytes = kVideoShardPayloadBytes);
std::optional<VideoShard> parse_video_datagram(const Datagram& datagram);

}  // namespace ministream
