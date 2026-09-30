#pragma once

#include "core/time/clock.hpp"

#include <span>
#include <array>
#include <vector>

namespace ministream {

struct DriftDecision {
  double resample_ratio{1.0};
};

class DriftController {
 public:
  DriftDecision update(Microseconds media_time_error) const noexcept;
};

class StereoClockResampler {
 public:
  std::vector<float> process(std::span<const float> samples, double ratio);
  void reset() noexcept { position_ = 0.0; have_previous_ = false; }
 private:
  double position_{};
  std::array<float, 2> previous_{};
  bool have_previous_{};
};

std::vector<float> resample_stereo_linear(
    std::span<const float> interleaved_stereo, double ratio);

}  // namespace ministream
