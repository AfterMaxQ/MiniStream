#pragma once

#include <cstddef>
#include <deque>

namespace ministream {

class WindowStats {
 public:
  explicit WindowStats(std::size_t capacity);

  void push(double value);
  [[nodiscard]] double mean() const;
  [[nodiscard]] double percentile(double p) const;
  [[nodiscard]] std::size_t size() const noexcept;

 private:
  std::size_t capacity_;
  std::deque<double> samples_;
};

}  // namespace ministream
