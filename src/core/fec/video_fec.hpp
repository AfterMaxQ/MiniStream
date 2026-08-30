#pragma once

#include "core/fec/fec_codec.hpp"
#include "core/time/clock.hpp"
#include "core/transport/packetizer.hpp"
#include "core/transport/reassembler.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace ministream {

inline constexpr std::size_t kVideoFecHeaderBytes = 28;
inline constexpr std::size_t kVideoFecShardPayloadBytes =
    ((kMaxSealedPayloadBytes - kVideoFecHeaderBytes) / 64U) * 64U;

struct VideoFecHeader {
  std::uint32_t frame_id{};
  std::uint64_t capture_timestamp_us{};
  std::uint16_t shard_index{};
  std::uint16_t data_shards{};
  std::uint16_t parity_shards{};
  std::uint16_t shard_bytes{};
  std::uint32_t frame_bytes{};
  bool keyframe{};

  friend bool operator==(const VideoFecHeader&, const VideoFecHeader&) = default;
};

struct VideoFecPacket {
  VideoFecHeader header;
  std::vector<std::byte> shard;
};

struct VideoFecFrame {
  std::vector<Datagram> video_datagrams;
  std::vector<Datagram> fec_datagrams;
};

std::vector<std::byte> encode_video_fec_payload(
    const VideoFecHeader& header, std::span<const std::byte> shard);
std::optional<VideoFecPacket> decode_video_fec_payload(
    std::span<const std::byte> payload);

class VideoFecEncoder {
 public:
  explicit VideoFecEncoder(SessionId session_id) : session_id_(session_id) {}

  VideoFecFrame encode_frame(const EncodedFrame& frame, double parity_ratio) const;

 private:
  SessionId session_id_{};
};

class VideoFecReassembler {
 public:
  explicit VideoFecReassembler(ReassemblyConfig config = {});

  std::optional<EncodedFrame> push_data(const VideoShard& shard,
                                        SteadyClock::time_point now);
  std::optional<EncodedFrame> push_parity(std::span<const std::byte> payload,
                                           SteadyClock::time_point now);
  std::vector<std::uint32_t> expire(SteadyClock::time_point now);
  [[nodiscard]] std::uint64_t recovered_frames() const noexcept {
    return recovered_frames_;
  }
  [[nodiscard]] std::uint64_t unrecoverable_frames() const noexcept {
    return unrecoverable_frames_;
  }

 public:
  struct PendingFrame {
    std::uint32_t frame_id{};
    std::uint64_t capture_timestamp_us{};
    bool keyframe{};
    std::uint16_t data_shards{};
    std::uint16_t parity_shards{};
    std::uint16_t shard_bytes{};
    std::uint32_t frame_bytes{};
    SteadyClock::time_point deadline;
    std::vector<FecShard> shards;
  };

 private:
  std::optional<EncodedFrame> complete(PendingFrame& frame, bool recovered);
  PendingFrame* find_or_create(const VideoFecHeader& header,
                               SteadyClock::time_point now);
  void erase_frame(std::uint32_t frame_id);
  void remember_completed(std::uint32_t frame_id);
  [[nodiscard]] bool was_completed(std::uint32_t frame_id) const noexcept;

  ReassemblyConfig config_;
  FecCodec codec_;
  std::unordered_map<std::uint32_t, PendingFrame> frames_;
  std::vector<std::uint32_t> insertion_order_;
  std::unordered_set<std::uint32_t> completed_frames_;
  std::deque<std::uint32_t> completed_order_;
  std::uint64_t recovered_frames_{};
  std::uint64_t unrecoverable_frames_{};
};

}  // namespace ministream
