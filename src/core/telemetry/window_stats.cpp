#include "core/telemetry/window_stats.hpp"

#include <algorithm>
#include <numeric>
#include <vector>

namespace ministream {

WindowStats::WindowStats(std::size_t capacity) : capacity_(capacity) {}

void WindowStats::push(double value) {
  if (capacity_ == 0) {
    return;
  }
  if (samples_.size() == capacity_) {
    samples_.pop_front();
  }
  samples_.push_back(value);
}

double WindowStats::mean() const {
  if (samples_.empty()) {
    return 0.0;
  }
  return std::accumulate(samples_.begin(), samples_.end(), 0.0) /
         static_cast<double>(samples_.size());
}

double WindowStats::percentile(double p) const {
  if (samples_.empty()) {
    return 0.0;
  }

  std::vector<double> sorted(samples_.begin(), samples_.end());
  std::sort(sorted.begin(), sorted.end());
  const auto clamped = std::clamp(p, 0.0, 1.0);
  const auto position = clamped * static_cast<double>(sorted.size() - 1);
  const auto lower = static_cast<std::size_t>(position);
  const auto upper = std::min(lower + 1, sorted.size() - 1);
  const auto fraction = position - static_cast<double>(lower);
  return sorted[lower] + (sorted[upper] - sorted[lower]) * fraction;
}

std::size_t WindowStats::size() const noexcept { return samples_.size(); }

}  // namespace ministream
