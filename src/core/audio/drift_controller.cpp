#include "core/audio/drift_controller.hpp"

#include <algorithm>
#include <cmath>

namespace ministream {

DriftDecision DriftController::update(Microseconds media_time_error) const noexcept {
  if (std::abs(media_time_error.count()) < 2000) {
    return {1.0};
  }
  const auto correction = static_cast<double>(media_time_error.count()) / 20'000'000.0;
  return {std::clamp(1.0 + correction, 0.995, 1.005)};
}

std::vector<float> StereoClockResampler::process(std::span<const float> samples, double ratio) {
  if (samples.empty() || samples.size() % 2 != 0 || !std::isfinite(ratio) ||
      ratio < 0.5 || ratio > 2.0) return {};
  const auto prefix = have_previous_ ? 1U : 0U;
  const auto frames = samples.size() / 2 + prefix;
  const auto sample = [&](std::size_t frame, std::size_t channel) {
    return prefix && frame == 0 ? previous_[channel] : samples[(frame - prefix) * 2 + channel];
  };
  std::vector<float> output;
  output.reserve(static_cast<std::size_t>(static_cast<double>(frames) / ratio + 1.0) * 2);
  // Keep the boundary sample until its right neighbour arrives. Carry the
  // fractional phase across blocks so 100 ppm does not round to zero per block.
  while (position_ < static_cast<double>(frames - 1)) {
    const auto lower = static_cast<std::size_t>(position_);
    const auto fraction = static_cast<float>(position_ - static_cast<double>(lower));
    for (std::size_t channel = 0; channel < 2; ++channel) {
      const auto first = sample(lower, channel);
      output.push_back(first + (sample(lower + 1, channel) - first) * fraction);
    }
    position_ += ratio;
  }
  position_ -= static_cast<double>(frames - 1);
  previous_ = {samples[samples.size() - 2], samples.back()};
  have_previous_ = true;
  return output;
}

std::vector<float> resample_stereo_linear(
    std::span<const float> interleaved_stereo, double ratio) {
  if (interleaved_stereo.size() % 2 != 0 || ratio <= 0.0 || interleaved_stereo.empty()) {
    return {};
  }
  const auto frames = interleaved_stereo.size() / 2;
  std::vector<float> output;
  output.reserve(static_cast<std::size_t>(static_cast<double>(frames) / ratio + 1.0) * 2);
  for (double position = 0.0; position < static_cast<double>(frames); position += ratio) {
    const auto lower = static_cast<std::size_t>(position);
    const auto upper = std::min(lower + 1, frames - 1);
    const auto fraction = static_cast<float>(position - static_cast<double>(lower));
    for (std::size_t channel = 0; channel < 2; ++channel) {
      const auto first = interleaved_stereo[lower * 2 + channel];
      const auto second = interleaved_stereo[upper * 2 + channel];
      output.push_back(first + (second - first) * fraction);
    }
  }
  return output;
}

}  // namespace ministream
