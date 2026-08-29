#include "core/time/clock_sync.hpp"

#include <algorithm>

namespace ministream {

ClockSample compute_clock_sample(
    std::int64_t t0, std::int64_t t1, std::int64_t t2, std::int64_t t3) {
  return {(t3 - t0) - (t2 - t1), ((t1 - t0) + (t2 - t3)) / 2};
}

ClockSynchronizer::ClockSynchronizer(std::size_t capacity) : capacity_(capacity) {}

void ClockSynchronizer::push(ClockSample sample) {
  if (capacity_ == 0) {
    return;
  }
  if (samples_.size() == capacity_) {
    samples_.pop_front();
  }
  samples_.push_back(sample);
}

std::optional<ClockSample> ClockSynchronizer::preferred() const {
  if (samples_.empty()) {
    return std::nullopt;
  }
  return *std::min_element(
      samples_.begin(), samples_.end(),
      [](const ClockSample& left, const ClockSample& right) {
        return left.rtt_us < right.rtt_us;
      });
}

}  // namespace ministream
