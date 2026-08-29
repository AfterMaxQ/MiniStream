#include "core/audio/drift_controller.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <vector>

using Catch::Approx;
using namespace std::chrono_literals;
using namespace ministream;

TEST_CASE("drift controller ignores tiny error and clamps larger correction") {
  DriftController controller;
  REQUIRE(controller.update(1ms).resample_ratio == Approx(1.0));
  REQUIRE(controller.update(100ms).resample_ratio == Approx(1.005));
  REQUIRE(controller.update(-100ms).resample_ratio == Approx(0.995));
}

TEST_CASE("linear stereo resampler changes duration without leaving finite bounds") {
  const std::vector<float> input{0, 0, 1, 1, 2, 2, 3, 3};
  const auto faster = resample_stereo_linear(input, 2.0);
  REQUIRE(faster == std::vector<float>{0, 0, 2, 2});
  const auto unchanged = resample_stereo_linear(input, 1.0);
  REQUIRE(unchanged == input);
}
