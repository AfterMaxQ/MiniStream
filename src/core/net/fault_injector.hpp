#pragma once

#include "core/time/clock.hpp"

#include <cstdint>
#include <random>

namespace ministream {

struct FaultConfig {
  double random_loss{};
  Microseconds jitter{};
  std::uint32_t burst_every_packets{};
  std::uint32_t burst_length{};
  std::uint64_t seed{};
};

struct FaultDecision {
  bool drop{};
  Microseconds delay{};
  bool operator==(const FaultDecision&) const = default;
};

class FaultInjector {
 public:
  explicit FaultInjector(FaultConfig config);
  FaultDecision decide(std::uint64_t packet_index);

 private:
  FaultConfig config_;
  std::mt19937_64 random_;
};

}  // namespace ministream
