#pragma once

#include "core/time/clock.hpp"

#include <span>
#include <vector>

namespace ministream {

struct DriftDecision {
  double resample_ratio{1.0};
};

class DriftController {
 public:
  DriftDecision update(Microseconds media_time_error) const noexcept;
};

std::vector<float> resample_stereo_linear(
    std::span<const float> interleaved_stereo, double ratio);

}  // namespace ministream
