#include "core/transport/packet_scheduler.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace ministream {
namespace {

constexpr double kVideoBurstSeconds = 0.020;

}  // namespace

bool PacketScheduler::enqueue(
    Priority priority, Datagram datagram, SteadyClock::time_point deadline) {
  const auto index = static_cast<std::size_t>(priority);
  auto& queue = queues_[index];
  if (queue.size() == kQueueLimits[index]) {
    if (priority == Priority::Control || priority == Priority::Audio) {
      return false;
    }
    queue.pop_front();
  }
  queue.push_back({std::move(datagram), deadline});
  return true;
}

std::optional<Datagram> PacketScheduler::next(SteadyClock::time_point now) {
  discard_expired(now);
  refill_video_tokens(now);
  for (std::size_t index = 0; index < queues_.size(); ++index) {
    auto& queue = queues_[index];
    if (queue.empty()) {
      continue;
    }
    if (index == static_cast<std::size_t>(Priority::Video)) {
      const auto bits = static_cast<double>(queue.front().datagram.bytes.size() * 8U);
      if (bits > video_tokens_bits_) {
        continue;
      }
      video_tokens_bits_ -= bits;
    }
    auto datagram = std::move(queue.front().datagram);
    queue.pop_front();
    return datagram;
  }
  return std::nullopt;
}

std::vector<Datagram> PacketScheduler::drain(
    SteadyClock::time_point now, std::size_t max_packets) {
  std::vector<Datagram> result;
  result.reserve(max_packets);
  while (result.size() < max_packets) {
    auto packet = next(now);
    if (!packet) {
      break;
    }
    result.push_back(std::move(*packet));
  }
  return result;
}

Microseconds PacketScheduler::estimated_video_queue_delay() const {
  if (video_rate_bps_ == 0) {
    return Microseconds::max();
  }
  std::uint64_t bytes = 0;
  for (const auto& item : queues_[static_cast<std::size_t>(Priority::Video)]) {
    bytes += item.datagram.bytes.size();
  }
  const auto delay = static_cast<std::uint64_t>(
      static_cast<long double>(bytes) * 8.0L * 1'000'000.0L /
      static_cast<long double>(video_rate_bps_));
  return Microseconds{delay};
}

std::uint64_t PacketScheduler::video_rate_bps() const noexcept { return video_rate_bps_; }

void PacketScheduler::set_video_rate(std::uint64_t bits_per_second) {
  video_rate_bps_ = bits_per_second;
  video_tokens_bits_ = max_video_tokens_bits();
  last_refill_.reset();
}

double PacketScheduler::max_video_tokens_bits() const noexcept {
  const auto rate_window = static_cast<double>(video_rate_bps_) * kVideoBurstSeconds;
  const auto minimum = static_cast<double>(kMaxDatagramBytes * 8ULL * 8ULL);
  return std::max(rate_window, minimum);
}

void PacketScheduler::discard_expired(SteadyClock::time_point now) {
  for (auto& queue : queues_) {
    std::erase_if(queue, [now](const QueuedDatagram& item) { return item.deadline <= now; });
  }
}

void PacketScheduler::refill_video_tokens(SteadyClock::time_point now) {
  const auto maximum = max_video_tokens_bits();
  if (!last_refill_) {
    last_refill_ = now;
    video_tokens_bits_ = maximum;
    return;
  }
  const auto elapsed = std::chrono::duration<double>(now - *last_refill_).count();
  video_tokens_bits_ = std::min(
      maximum, video_tokens_bits_ + elapsed * static_cast<double>(video_rate_bps_));
  last_refill_ = now;
}

}  // namespace ministream
