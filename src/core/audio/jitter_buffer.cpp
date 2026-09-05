#include "core/audio/jitter_buffer.hpp"

#include <algorithm>

namespace ministream {

AudioJitterBuffer::AudioJitterBuffer(AudioJitterConfig config) : config_(config) {}

void AudioJitterBuffer::push(AudioPacket packet) {
  if (packets_.contains(packet.sequence) || packet.sample_count == 0 ||
      (last_played_ && !sequence_is_newer(packet.sequence, *last_played_))) {
    return;
  }
  buffered_samples_ += packet.sample_count;
  arrival_order_.push_back(packet.sequence);
  packets_.emplace(packet.sequence, std::move(packet));
  while (buffered_duration() > config_.max && !arrival_order_.empty()) {
    erase(*first_sequence());
  }
}

AudioPlayoutResult AudioJitterBuffer::pop(std::uint32_t expected_sequence) {
  last_played_ = expected_sequence;
  while (const auto first = first_sequence()) {
    if (!sequence_is_newer(expected_sequence, *first)) break;
    erase(*first);
  }
  const auto found = packets_.find(expected_sequence);
  if (found == packets_.end()) {
    return {AudioPlayoutKind::Plc, std::nullopt};
  }
  auto packet = std::move(found->second);
  erase(expected_sequence);
  return {AudioPlayoutKind::Packet, std::move(packet)};
}

bool AudioJitterBuffer::ready_for_playout() const noexcept {
  return buffered_duration() >= config_.target;
}

std::optional<std::uint32_t> AudioJitterBuffer::first_sequence() const noexcept {
  std::optional<std::uint32_t> first;
  for (const auto sequence : arrival_order_) {
    if (!first || sequence_is_newer(*first, sequence)) first = sequence;
  }
  return first;
}

Microseconds AudioJitterBuffer::buffered_duration() const {
  return Microseconds{static_cast<std::int64_t>(buffered_samples_ * 1'000'000ULL / 48'000ULL)};
}

void AudioJitterBuffer::erase(std::uint32_t sequence) {
  const auto found = packets_.find(sequence);
  if (found == packets_.end()) {
    return;
  }
  buffered_samples_ -= found->second.sample_count;
  packets_.erase(found);
  const auto position = std::find(arrival_order_.begin(), arrival_order_.end(), sequence);
  if (position != arrival_order_.end()) {
    arrival_order_.erase(position);
  }
}

}  // namespace ministream
