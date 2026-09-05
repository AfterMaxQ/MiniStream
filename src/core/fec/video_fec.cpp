#include "core/fec/video_fec.hpp"

#include "core/protocol/wire.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

namespace ministream {
namespace {

constexpr std::byte kVideoFecVersion{1};
constexpr std::size_t kFrameIdOffset = 2;
constexpr std::size_t kTimestampOffset = 6;
constexpr std::size_t kShardIndexOffset = 14;
constexpr std::size_t kDataShardsOffset = 16;
constexpr std::size_t kParityShardsOffset = 18;
constexpr std::size_t kShardBytesOffset = 20;
constexpr std::size_t kFrameBytesOffset = 22;

void put_u16(std::span<std::byte> output, std::uint16_t value) {
  output[0] = static_cast<std::byte>(value >> 8U);
  output[1] = static_cast<std::byte>(value);
}

void put_u32(std::span<std::byte> output, std::uint32_t value) {
  output[0] = static_cast<std::byte>(value >> 24U);
  output[1] = static_cast<std::byte>(value >> 16U);
  output[2] = static_cast<std::byte>(value >> 8U);
  output[3] = static_cast<std::byte>(value);
}

void put_u64(std::span<std::byte> output, std::uint64_t value) {
  for (std::size_t index = 0; index < 8; ++index) {
    output[index] = static_cast<std::byte>(value >> ((7U - index) * 8U));
  }
}

std::uint16_t get_u16(std::span<const std::byte> input) {
  return static_cast<std::uint16_t>((std::to_integer<std::uint16_t>(input[0]) << 8U) |
                                    std::to_integer<std::uint16_t>(input[1]));
}

std::uint32_t get_u32(std::span<const std::byte> input) {
  return (std::to_integer<std::uint32_t>(input[0]) << 24U) |
         (std::to_integer<std::uint32_t>(input[1]) << 16U) |
         (std::to_integer<std::uint32_t>(input[2]) << 8U) |
         std::to_integer<std::uint32_t>(input[3]);
}

std::uint64_t get_u64(std::span<const std::byte> input) {
  std::uint64_t value = 0;
  for (const auto byte : input.first<8>()) {
    value = (value << 8U) | std::to_integer<std::uint64_t>(byte);
  }
  return value;
}

std::size_t expected_data_bytes(const VideoFecReassembler::PendingFrame& frame,
                                std::size_t index) {
  if (frame.frame_bytes == 0 || index >= frame.data_shards) {
    return 0;
  }
  const auto shard_bytes = frame.shard_bytes != 0 ? frame.shard_bytes
                                                   : kVideoFecShardPayloadBytes;
  const auto offset = index * shard_bytes;
  if (offset >= frame.frame_bytes) {
    return 0;
  }
  return std::min<std::size_t>(shard_bytes, frame.frame_bytes - offset);
}

bool has_all_data(const VideoFecReassembler::PendingFrame& frame) {
  if (frame.shards.size() < frame.data_shards) {
    return false;
  }
  return std::all_of(frame.shards.begin(), frame.shards.begin() + frame.data_shards,
                     [](const FecShard& shard) { return shard.present; });
}

std::size_t present_count(const VideoFecReassembler::PendingFrame& frame) {
  return static_cast<std::size_t>(std::count_if(
      frame.shards.begin(), frame.shards.end(), [](const FecShard& shard) {
        return shard.present;
      }));
}

}  // namespace

std::vector<std::byte> encode_video_fec_payload(
    const VideoFecHeader& header, std::span<const std::byte> shard) {
  if (header.data_shards == 0 || header.parity_shards == 0 || header.shard_bytes == 0 ||
      header.shard_bytes > kVideoFecShardPayloadBytes ||
      header.shard_index >=
          static_cast<std::uint32_t>(header.data_shards) + header.parity_shards ||
      header.frame_bytes == 0 || shard.size() != header.shard_bytes) {
    return {};
  }
  std::vector<std::byte> payload(kVideoFecHeaderBytes + shard.size());
  payload[0] = kVideoFecVersion;
  payload[1] = header.keyframe ? std::byte{1} : std::byte{0};
  put_u32(std::span{payload}.subspan(kFrameIdOffset, 4), header.frame_id);
  put_u64(std::span{payload}.subspan(kTimestampOffset, 8), header.capture_timestamp_us);
  put_u16(std::span{payload}.subspan(kShardIndexOffset, 2), header.shard_index);
  put_u16(std::span{payload}.subspan(kDataShardsOffset, 2), header.data_shards);
  put_u16(std::span{payload}.subspan(kParityShardsOffset, 2), header.parity_shards);
  put_u16(std::span{payload}.subspan(kShardBytesOffset, 2), header.shard_bytes);
  put_u32(std::span{payload}.subspan(kFrameBytesOffset, 4), header.frame_bytes);
  std::copy(shard.begin(), shard.end(), payload.begin() + kVideoFecHeaderBytes);
  return payload;
}

std::optional<VideoFecPacket> decode_video_fec_payload(
    std::span<const std::byte> payload) {
  if (payload.size() < kVideoFecHeaderBytes || payload[0] != kVideoFecVersion ||
      (payload[1] != std::byte{0} && payload[1] != std::byte{1}) || payload[26] != std::byte{0} ||
      payload[27] != std::byte{0}) {
    return std::nullopt;
  }
  VideoFecPacket packet;
  packet.header.keyframe = payload[1] == std::byte{1};
  packet.header.frame_id = get_u32(payload.subspan(kFrameIdOffset, 4));
  packet.header.capture_timestamp_us = get_u64(payload.subspan(kTimestampOffset, 8));
  packet.header.shard_index = get_u16(payload.subspan(kShardIndexOffset, 2));
  packet.header.data_shards = get_u16(payload.subspan(kDataShardsOffset, 2));
  packet.header.parity_shards = get_u16(payload.subspan(kParityShardsOffset, 2));
  packet.header.shard_bytes = get_u16(payload.subspan(kShardBytesOffset, 2));
  packet.header.frame_bytes = get_u32(payload.subspan(kFrameBytesOffset, 4));
  const auto& header = packet.header;
  const auto expected_data_shards =
      (static_cast<std::uint64_t>(header.frame_bytes) + kVideoFecShardPayloadBytes - 1U) /
      kVideoFecShardPayloadBytes;
  if (header.data_shards == 0 || header.parity_shards == 0 || header.shard_bytes == 0 ||
      header.shard_bytes > kVideoFecShardPayloadBytes || header.shard_index < header.data_shards ||
      header.shard_index >=
          static_cast<std::uint32_t>(header.data_shards) + header.parity_shards ||
      header.frame_bytes == 0 || expected_data_shards != header.data_shards ||
      payload.size() != kVideoFecHeaderBytes + header.shard_bytes) {
    return std::nullopt;
  }
  packet.shard.assign(payload.begin() + kVideoFecHeaderBytes, payload.end());
  return packet;
}

VideoFecFrame VideoFecEncoder::encode_frame(const EncodedFrame& frame,
                                            double parity_ratio) const {
  VideoFecFrame result;
  if (frame.bytes.empty()) {
    return result;
  }
  if (parity_ratio <= 0.0) {
    result.video_datagrams = packetize_video(frame, session_id_);
    return result;
  }

  result.video_datagrams = packetize_video(frame, session_id_, kVideoFecShardPayloadBytes);
  if (result.video_datagrams.empty() || frame.bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
    return result;
  }
  std::vector<Datagram> data;
  data.reserve(result.video_datagrams.size());
  for (const auto& datagram : result.video_datagrams) {
    const auto shard = parse_video_datagram(datagram);
    if (!shard) {
      result.fec_datagrams.clear();
      return result;
    }
    data.push_back({shard->payload});
  }
  const auto clamped_ratio = std::clamp(parity_ratio, 0.0, 1.0);
  auto parity_count = static_cast<std::size_t>(
      std::ceil(static_cast<double>(data.size()) * clamped_ratio));
  parity_count = std::clamp<std::size_t>(parity_count, 1U, data.size());
  const auto block = FecCodec{}.encode(data, static_cast<std::uint16_t>(parity_count));
  if (block.layout.data_shards == 0 || block.layout.parity_shards == 0 ||
      block.layout.shard_bytes > kVideoFecShardPayloadBytes) {
    return result;
  }
  for (std::uint16_t index = 0; index < block.layout.parity_shards; ++index) {
    const auto& shard = block.shards[block.layout.data_shards + index];
    const VideoFecHeader header{frame.frame_id,
                                frame.capture_timestamp_us,
                                shard.index,
                                block.layout.data_shards,
                                block.layout.parity_shards,
                                static_cast<std::uint16_t>(block.layout.shard_bytes),
                                static_cast<std::uint32_t>(frame.bytes.size()),
                                frame.keyframe};
    const auto payload = encode_video_fec_payload(header, shard.bytes);
    if (payload.empty()) {
      result.fec_datagrams.clear();
      return result;
    }
    const auto common = encode_common_header(
        {PacketType::VideoFec, session_id_, static_cast<std::uint16_t>(payload.size())});
    Datagram datagram;
    datagram.bytes.reserve(common.size() + payload.size());
    datagram.bytes.insert(datagram.bytes.end(), common.begin(), common.end());
    datagram.bytes.insert(datagram.bytes.end(), payload.begin(), payload.end());
    result.fec_datagrams.push_back(std::move(datagram));
  }
  return result;
}

VideoFecReassembler::VideoFecReassembler(ReassemblyConfig config) : config_(config) {
  config_.deadline = std::max(config_.deadline, Microseconds{25'000});
}

VideoFecReassembler::PendingFrame* VideoFecReassembler::find_or_create(
    const VideoFecHeader& header, SteadyClock::time_point now) {
  if (header.data_shards == 0 || header.data_shards > kMaxVideoShards ||
      header.data_shards + header.parity_shards > kMaxVideoShards + 1U) {
    return nullptr;
  }
  auto found = frames_.find(header.frame_id);
  if (found == frames_.end()) {
    if (config_.max_incomplete_frames == 0) {
      return nullptr;
    }
    while (frames_.size() >= config_.max_incomplete_frames && !insertion_order_.empty()) {
      erase_frame(insertion_order_.front(), true);
    }
    PendingFrame pending;
    pending.frame_id = header.frame_id;
    pending.capture_timestamp_us = header.capture_timestamp_us;
    pending.keyframe = header.keyframe;
    pending.data_shards = header.data_shards;
    pending.parity_shards = header.parity_shards;
    pending.shard_bytes = header.shard_bytes;
    pending.frame_bytes = header.frame_bytes;
    pending.deadline = now + (header.keyframe
        ? std::max(config_.deadline, config_.keyframe_deadline) : config_.deadline);
    pending.shards.resize(static_cast<std::size_t>(header.data_shards) + header.parity_shards);
    insertion_order_.push_back(header.frame_id);
    found = frames_.emplace(header.frame_id, std::move(pending)).first;
  } else {
    auto& pending = found->second;
    if (pending.capture_timestamp_us != header.capture_timestamp_us ||
        pending.keyframe != header.keyframe || pending.data_shards != header.data_shards ||
        (pending.parity_shards != 0 && header.parity_shards != 0 &&
         pending.parity_shards != header.parity_shards) ||
        (pending.shard_bytes != 0 && header.shard_bytes != 0 &&
         pending.shard_bytes != header.shard_bytes) ||
        (pending.frame_bytes != 0 && header.frame_bytes != 0 &&
         pending.frame_bytes != header.frame_bytes)) {
      erase_frame(header.frame_id, true);
      return nullptr;
    }
    if (pending.parity_shards == 0 && header.parity_shards != 0) {
      pending.parity_shards = header.parity_shards;
      pending.shards.resize(static_cast<std::size_t>(pending.data_shards) + pending.parity_shards);
    }
    if (pending.shard_bytes == 0) {
      pending.shard_bytes = header.shard_bytes;
    }
    if (pending.frame_bytes == 0) {
      pending.frame_bytes = header.frame_bytes;
    }
  }
  return &found->second;
}

std::optional<EncodedFrame> VideoFecReassembler::push_data(
    const VideoShard& shard, SteadyClock::time_point now) {
  if (was_completed(shard.header.frame_id) || was_expired(shard.header.frame_id)) {
    return std::nullopt;
  }
  expire(now);
  if (was_expired(shard.header.frame_id)) {
    return std::nullopt;
  }
  const VideoFecHeader header{shard.header.frame_id,
                              shard.header.capture_timestamp_us,
                              shard.header.shard_index,
                              shard.header.shard_count,
                              0,
                              0,
                              0,
                              shard.keyframe};
  auto* pending = find_or_create(header, now);
  if (!pending || shard.header.shard_index >= pending->data_shards) {
    return std::nullopt;
  }
  if (pending->shard_bytes != 0 && shard.payload.size() > pending->shard_bytes) {
    return std::nullopt;
  }
  auto& slot = pending->shards[shard.header.shard_index];
  if (!slot.present) {
    slot = {shard.header.shard_index, static_cast<std::uint16_t>(shard.payload.size()), true,
            shard.payload};
    ++received_data_shards_;
  }
  auto result = try_complete(*pending);
  if (result) {
    erase_frame(pending->frame_id);
  }
  return result;
}

std::optional<EncodedFrame> VideoFecReassembler::push_parity(
    std::span<const std::byte> payload, SteadyClock::time_point now) {
  const auto decoded = decode_video_fec_payload(payload);
  if (!decoded) {
    return std::nullopt;
  }
  if (was_completed(decoded->header.frame_id) || was_expired(decoded->header.frame_id)) {
    return std::nullopt;
  }
  expire(now);
  if (was_expired(decoded->header.frame_id)) {
    return std::nullopt;
  }
  auto* pending = find_or_create(decoded->header, now);
  if (!pending || decoded->header.shard_index >= pending->shards.size()) {
    return std::nullopt;
  }
  if (std::any_of(pending->shards.begin(), pending->shards.end(),
                  [&](const FecShard& shard) {
                    return shard.present && shard.bytes.size() > pending->shard_bytes;
                  })) {
    return std::nullopt;
  }
  auto& slot = pending->shards[decoded->header.shard_index];
  if (!slot.present) {
    slot = {decoded->header.shard_index, decoded->header.shard_bytes, true, decoded->shard};
  }
  auto result = try_complete(*pending);
  if (result) {
    erase_frame(pending->frame_id);
  }
  return result;
}

std::optional<EncodedFrame> VideoFecReassembler::try_complete(PendingFrame& frame) {
  if (has_all_data(frame)) {
    return complete(frame, false);
  }
  if (frame.parity_shards == 0 || frame.shard_bytes == 0 ||
      present_count(frame) < frame.data_shards) {
    return std::nullopt;
  }
  const auto received_data = std::count_if(
      frame.shards.begin(), frame.shards.begin() + frame.data_shards,
      [](const FecShard& shard) { return shard.present; });
  const auto missing = static_cast<std::size_t>(frame.data_shards) - received_data;
  for (std::size_t index = 0; index < frame.data_shards; ++index) {
    auto& data = frame.shards[index];
    if (!data.present) {
      const auto size = expected_data_bytes(frame, index);
      if (size == 0 || size > frame.shard_bytes) {
        return std::nullopt;
      }
      data.index = static_cast<std::uint16_t>(index);
      data.original_payload_bytes = static_cast<std::uint16_t>(size);
    }
  }
  FecBlock block{{frame.data_shards, frame.parity_shards, frame.shard_bytes}, frame.shards};
  if (!codec_.recover(block)) {
    return std::nullopt;
  }
  frame.shards = std::move(block.shards);
  auto result = complete(frame, true);
  if (result) {
    lost_data_shards_ += missing;
    recovered_data_shards_ += missing;
  }
  return result;
}

std::optional<EncodedFrame> VideoFecReassembler::complete(PendingFrame& frame, bool recovered) {
  if (!has_all_data(frame)) {
    return std::nullopt;
  }
  EncodedFrame result;
  result.frame_id = frame.frame_id;
  result.capture_timestamp_us = frame.capture_timestamp_us;
  result.keyframe = frame.keyframe;
  const auto total = frame.frame_bytes != 0 ? frame.frame_bytes : [&] {
    std::size_t bytes = 0;
    for (std::size_t index = 0; index < frame.data_shards; ++index) {
      bytes += frame.shards[index].bytes.size();
    }
    return bytes;
  }();
  if (total == 0 || total > std::numeric_limits<std::uint32_t>::max()) {
    return std::nullopt;
  }
  result.bytes.reserve(total);
  for (std::size_t index = 0; index < frame.data_shards; ++index) {
    const auto expected = frame.frame_bytes != 0
                              ? expected_data_bytes(frame, index)
                              : frame.shards[index].bytes.size();
    if (expected == 0 || frame.shards[index].bytes.size() < expected ||
        result.bytes.size() + expected > total) {
      return std::nullopt;
    }
    result.bytes.insert(result.bytes.end(), frame.shards[index].bytes.begin(),
                        frame.shards[index].bytes.begin() + expected);
  }
  if (result.bytes.size() != total) {
    return std::nullopt;
  }
  if (recovered) {
    ++recovered_frames_;
  }
  remember_completed(frame.frame_id);
  return result;
}

std::vector<std::uint32_t> VideoFecReassembler::expire(SteadyClock::time_point now) {
  std::vector<std::uint32_t> expired;
  for (const auto id : insertion_order_) {
    const auto found = frames_.find(id);
    if (found != frames_.end() && now >= found->second.deadline) {
      if (!has_all_data(found->second)) {
        ++unrecoverable_frames_;
      }
      expired.push_back(id);
    }
  }
  for (const auto id : expired) {
    erase_frame(id, true);
  }
  return expired;
}

void VideoFecReassembler::erase_frame(std::uint32_t frame_id, bool remember_expired_frame) {
  if (const auto found = frames_.find(frame_id); found != frames_.end()) {
    account_missing(found->second);
    frames_.erase(found);
  }
  std::erase(insertion_order_, frame_id);
  if (remember_expired_frame) {
    remember_expired(frame_id);
  }
}

void VideoFecReassembler::account_missing(const PendingFrame& frame) noexcept {
  const auto received = std::count_if(
      frame.shards.begin(),
      frame.shards.begin() + std::min<std::size_t>(frame.data_shards, frame.shards.size()),
      [](const FecShard& shard) { return shard.present; });
  if (received < frame.data_shards) {
    lost_data_shards_ += static_cast<std::size_t>(frame.data_shards) - received;
  }
}

void VideoFecReassembler::remember_expired(std::uint32_t frame_id) {
  constexpr std::size_t kMaxExpiredFrames = 64;
  if (!expired_frames_.insert(frame_id).second) {
    return;
  }
  expired_order_.push_back(frame_id);
  while (expired_order_.size() > kMaxExpiredFrames) {
    expired_frames_.erase(expired_order_.front());
    expired_order_.pop_front();
  }
}

void VideoFecReassembler::remember_completed(std::uint32_t frame_id) {
  constexpr std::size_t kMaxCompletedFrames = 64;
  if (!completed_frames_.insert(frame_id).second) {
    return;
  }
  completed_order_.push_back(frame_id);
  while (completed_order_.size() > kMaxCompletedFrames) {
    completed_frames_.erase(completed_order_.front());
    completed_order_.pop_front();
  }
}

bool VideoFecReassembler::was_completed(std::uint32_t frame_id) const noexcept {
  return completed_frames_.contains(frame_id);
}

bool VideoFecReassembler::was_expired(std::uint32_t frame_id) const noexcept {
  return expired_frames_.contains(frame_id);
}

}  // namespace ministream
