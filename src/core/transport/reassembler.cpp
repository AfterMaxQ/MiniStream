#include "core/transport/reassembler.hpp"

#include <algorithm>

namespace ministream {

FrameReassembler::FrameReassembler(ReassemblyConfig config) : config_(config) {}

std::optional<EncodedFrame> FrameReassembler::push(
    const Datagram& datagram, SteadyClock::time_point now) {
  expire(now);
  const auto shard = parse_video_datagram(datagram);
  if (!shard || config_.max_incomplete_frames == 0) {
    return std::nullopt;
  }

  auto found = frames_.find(shard->header.frame_id);
  if (found == frames_.end()) {
    if (frames_.size() == config_.max_incomplete_frames) {
      erase_frame(insertion_order_.front());
    }
    IncompleteFrame frame;
    frame.capture_timestamp_us = shard->header.capture_timestamp_us;
    frame.keyframe = shard->keyframe;
    frame.deadline = now + config_.deadline;
    frame.shards.resize(shard->header.shard_count);
    insertion_order_.push_back(shard->header.frame_id);
    found = frames_.emplace(shard->header.frame_id, std::move(frame)).first;
  }

  auto& frame = found->second;
  if (frame.shards.size() != shard->header.shard_count ||
      frame.capture_timestamp_us != shard->header.capture_timestamp_us ||
      frame.keyframe != shard->keyframe) {
    erase_frame(shard->header.frame_id);
    return std::nullopt;
  }

  auto& slot = frame.shards[shard->header.shard_index];
  if (!slot) {
    slot = std::move(shard->payload);
    ++frame.received;
  }
  if (frame.received != frame.shards.size()) {
    return std::nullopt;
  }

  EncodedFrame completed;
  completed.frame_id = shard->header.frame_id;
  completed.capture_timestamp_us = frame.capture_timestamp_us;
  completed.keyframe = frame.keyframe;
  std::size_t total_bytes = 0;
  for (const auto& part : frame.shards) {
    total_bytes += part->size();
  }
  completed.bytes.reserve(total_bytes);
  for (auto& part : frame.shards) {
    completed.bytes.insert(completed.bytes.end(), part->begin(), part->end());
  }
  erase_frame(completed.frame_id);
  return completed;
}

std::vector<std::uint32_t> FrameReassembler::expire(SteadyClock::time_point now) {
  std::vector<std::uint32_t> expired;
  for (auto id : insertion_order_) {
    const auto found = frames_.find(id);
    if (found != frames_.end() && now >= found->second.deadline) {
      expired.push_back(id);
    }
  }
  for (auto id : expired) {
    erase_frame(id);
  }
  return expired;
}

void FrameReassembler::erase_frame(std::uint32_t frame_id) {
  frames_.erase(frame_id);
  const auto position = std::find(insertion_order_.begin(), insertion_order_.end(), frame_id);
  if (position != insertion_order_.end()) {
    insertion_order_.erase(position);
  }
}

}  // namespace ministream
