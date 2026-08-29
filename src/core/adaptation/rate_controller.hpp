#pragma once

#include "core/time/clock.hpp"

#include <cstdint>
#include <optional>

namespace ministream {

struct NetworkFeedback {
  Microseconds send_queue{};
  Microseconds rtt{};
  Microseconds jitter{};
  double loss_fraction{};
  std::uint32_t unrecoverable_frames{};
};

struct AdaptationDecision {
  enum class Reason { None, QueueGrowth, Loss, UnrecoverableFrame, StableRecovery };
  std::uint64_t bitrate_bps{};
  double fec_ratio{};
  Reason reason{Reason::None};
};

class RateController {
 public:
  RateController(std::uint64_t minimum_bps, std::uint64_t maximum_bps, std::uint64_t initial_bps);
  AdaptationDecision update(const NetworkFeedback& feedback, SteadyClock::time_point now);

 private:
  static double fec_ratio(double loss_fraction);

  std::uint64_t minimum_bps_;
  std::uint64_t maximum_bps_;
  std::uint64_t bitrate_bps_;
  std::optional<SteadyClock::time_point> stable_since_;
  std::optional<SteadyClock::time_point> last_recovery_;
};

}  // namespace ministream
