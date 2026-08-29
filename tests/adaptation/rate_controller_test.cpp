#include "core/adaptation/rate_controller.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>

using Catch::Approx;
using namespace std::chrono_literals;
using namespace ministream;

TEST_CASE("rate controller reacts immediately to measured overload") {
  RateController controller(20'000'000, 80'000'000, 50'000'000);
  const auto decision = controller.update(
      {5ms, 10ms, 1ms, 0.0, 0}, SteadyClock::time_point{});
  REQUIRE(decision.bitrate_bps == 42'500'000);
  REQUIRE(decision.reason == AdaptationDecision::Reason::QueueGrowth);
  REQUIRE(decision.fec_ratio == Approx(0.03));
}

TEST_CASE("rate controller maps loss to the visible FEC policy") {
  RateController controller(20'000'000, 80'000'000, 50'000'000);
  const auto now = SteadyClock::time_point{};
  REQUIRE(controller.update({0ms, 0ms, 0ms, 0.0005, 0}, now).fec_ratio == Approx(0.03));
  REQUIRE(controller.update({0ms, 0ms, 0ms, 0.0030, 0}, now).fec_ratio == Approx(0.05));
  REQUIRE(controller.update({0ms, 0ms, 0ms, 0.0075, 0}, now).fec_ratio == Approx(0.10));
  REQUIRE(controller.update({0ms, 0ms, 0ms, 0.0200, 0}, now).fec_ratio == Approx(0.15));
}

TEST_CASE("rate controller recovers slowly after two stable seconds") {
  RateController controller(20'000'000, 80'000'000, 50'000'000);
  const NetworkFeedback stable{1ms, 10ms, 1ms, 0.001, 0};
  const auto start = SteadyClock::time_point{};
  REQUIRE(controller.update(stable, start).bitrate_bps == 50'000'000);
  REQUIRE(controller.update(stable, start + 1999ms).bitrate_bps == 50'000'000);
  const auto recovered = controller.update(stable, start + 2s);
  REQUIRE(recovered.bitrate_bps == 51'000'000);
  REQUIRE(recovered.reason == AdaptationDecision::Reason::StableRecovery);
}
