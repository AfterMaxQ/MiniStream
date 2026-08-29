#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

namespace ministream {

struct ClockSample {
  std::int64_t rtt_us{};
  std::int64_t offset_us{};
  bool operator==(const ClockSample&) const = default;
};

ClockSample compute_clock_sample(
    std::int64_t t0, std::int64_t t1, std::int64_t t2, std::int64_t t3);

class ClockSynchronizer {
 public:
  explicit ClockSynchronizer(std::size_t capacity = 20);
  void push(ClockSample sample);
  [[nodiscard]] std::optional<ClockSample> preferred() const;

 private:
  std::size_t capacity_;
  std::deque<ClockSample> samples_;
};

}  // namespace ministream
