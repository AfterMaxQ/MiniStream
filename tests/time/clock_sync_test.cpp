#include "core/time/clock_sync.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("clock sync computes signed offset and network RTT") {
  const auto positive = compute_clock_sample(0, 15'000, 16'000, 21'000);
  REQUIRE(positive.rtt_us == 20'000);
  REQUIRE(positive.offset_us == 5'000);

  const auto negative = compute_clock_sample(0, 3'000, 4'000, 21'000);
  REQUIRE(negative.rtt_us == 20'000);
  REQUIRE(negative.offset_us == -7'000);
}

TEST_CASE("clock sync selects the recent lowest RTT sample") {
  ClockSynchronizer synchronizer(20);
  synchronizer.push({8'000, 400});
  synchronizer.push({2'000, -120});
  synchronizer.push({4'000, 50});
  REQUIRE(synchronizer.preferred() == ClockSample{2'000, -120});
}
