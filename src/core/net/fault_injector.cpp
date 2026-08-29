#include "core/net/fault_injector.hpp"

#include <algorithm>

namespace ministream {

FaultInjector::FaultInjector(FaultConfig config) : config_(config), random_(config.seed) {
  config_.random_loss = std::clamp(config_.random_loss, 0.0, 1.0);
}

FaultDecision FaultInjector::decide(std::uint64_t packet_index) {
  const bool burst = config_.burst_every_packets > 0 &&
                     packet_index >= config_.burst_every_packets &&
                     packet_index % config_.burst_every_packets < config_.burst_length;
  std::bernoulli_distribution loss(config_.random_loss);
  std::uniform_int_distribution<std::int64_t> jitter(
      0, std::max<std::int64_t>(0, config_.jitter.count()));
  return {burst || loss(random_), Microseconds{jitter(random_)}};
}

}  // namespace ministream
