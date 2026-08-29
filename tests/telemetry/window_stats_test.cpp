#include "core/telemetry/window_stats.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace ministream;

TEST_CASE("rolling statistics discard the oldest sample at capacity") {
  WindowStats stats(3);
  stats.push(1.0);
  stats.push(2.0);
  stats.push(3.0);
  stats.push(6.0);

  REQUIRE(stats.size() == 3);
  REQUIRE(stats.mean() == Approx(11.0 / 3.0));
  REQUIRE(stats.percentile(0.5) == Approx(3.0));
  REQUIRE(stats.percentile(0.95) == Approx(5.7));
}

TEST_CASE("empty rolling statistics have neutral aggregate values") {
  WindowStats stats(4);
  REQUIRE(stats.size() == 0);
  REQUIRE(stats.mean() == 0.0);
  REQUIRE(stats.percentile(0.95) == 0.0);
}
