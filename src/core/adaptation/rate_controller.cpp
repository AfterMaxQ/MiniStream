#include "core/adaptation/rate_controller.hpp"

#include <algorithm>
#include <chrono>

namespace ministream {

RateController::RateController(
    std::uint64_t minimum_bps, std::uint64_t maximum_bps, std::uint64_t initial_bps)
    : minimum_bps_(minimum_bps),
      maximum_bps_(std::max(maximum_bps, minimum_bps)),
      bitrate_bps_(std::clamp(initial_bps, minimum_bps_, maximum_bps_)) {}

AdaptationDecision RateController::update(
    const NetworkFeedback& feedback, SteadyClock::time_point now) {
  AdaptationDecision::Reason reason = AdaptationDecision::Reason::None;
  if (feedback.unrecoverable_frames > 0) {
    reason = AdaptationDecision::Reason::UnrecoverableFrame;
  } else if (feedback.send_queue > std::chrono::milliseconds{4}) {
    reason = AdaptationDecision::Reason::QueueGrowth;
  } else if (feedback.loss_fraction > 0.01) {
    reason = AdaptationDecision::Reason::Loss;
  }

  if (reason != AdaptationDecision::Reason::None) {
    bitrate_bps_ = std::max(
        minimum_bps_, static_cast<std::uint64_t>(static_cast<double>(bitrate_bps_) * 0.85));
    stable_since_.reset();
    last_recovery_.reset();
    return {bitrate_bps_, fec_ratio(feedback.loss_fraction), reason};
  }

  const bool stable = feedback.send_queue < std::chrono::milliseconds{2} &&
                      feedback.loss_fraction < 0.002 &&
                      feedback.jitter < std::chrono::milliseconds{3} &&
                      feedback.unrecoverable_frames == 0;
  if (!stable) {
    stable_since_.reset();
    last_recovery_.reset();
  } else if (!stable_since_) {
    stable_since_ = now;
  } else if (now - *stable_since_ >= std::chrono::seconds{2}) {
    if (!last_recovery_) {
      last_recovery_ = now;
      bitrate_bps_ = std::min(maximum_bps_, bitrate_bps_ + 1'000'000U);
      reason = AdaptationDecision::Reason::StableRecovery;
    } else {
      const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now - *last_recovery_).count();
      if (seconds > 0) {
        bitrate_bps_ = std::min(
            maximum_bps_, bitrate_bps_ + static_cast<std::uint64_t>(seconds) * 1'000'000U);
        *last_recovery_ += std::chrono::seconds{seconds};
        reason = AdaptationDecision::Reason::StableRecovery;
      }
    }
  }

  return {bitrate_bps_, fec_ratio(feedback.loss_fraction), reason};
}

double RateController::fec_ratio(double loss_fraction) {
  if (loss_fraction < 0.001) {
    return 0.03;
  }
  if (loss_fraction <= 0.005) {
    return 0.05;
  }
  if (loss_fraction <= 0.01) {
    return 0.10;
  }
  return 0.15;
}

}  // namespace ministream
