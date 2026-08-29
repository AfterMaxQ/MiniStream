#pragma once

#include "core/protocol/value_types.hpp"
#include "core/transport/media_header.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ministream {

inline constexpr std::size_t kMediaHeaderBytes = 20;
inline constexpr std::size_t kVideoShardPayloadBytes =
    kMaxDatagramBytes - 12 - kMediaHeaderBytes;
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

std::vector<Datagram> packetize_video(const EncodedFrame& frame, SessionId session_id);
std::optional<VideoShard> parse_video_datagram(const Datagram& datagram);

}  // namespace ministream
