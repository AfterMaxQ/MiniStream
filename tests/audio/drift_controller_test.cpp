#include "core/audio/drift_controller.hpp"
#include "core/audio/jitter_buffer.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
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

TEST_CASE("audio resampling retains sub-sample clock correction over twenty minutes") {
  const std::vector<float> input(480 * 2, 0.25F);
  for (const auto ratio : {1.0001, 0.9999}) {
    StereoClockResampler resampler;
    std::size_t frames{};
    for (unsigned block = 0; block < 120'000; ++block)
      frames += resampler.process(input, ratio).size() / 2;
    const auto expected = 120'000.0 * 480.0 / ratio;
    REQUIRE(std::abs(static_cast<double>(frames) - expected) <= 2.0);
  }
}

TEST_CASE("clock resampler interpolates continuously across audio packet boundaries") {
  StereoClockResampler resampler;
  const auto first = resampler.process(std::vector<float>{0, 0, 1, 1}, 0.75);
  const auto second = resampler.process(std::vector<float>{2, 2, 3, 3}, 0.75);
  REQUIRE(first == std::vector<float>{0, 0, 0.75F, 0.75F});
  REQUIRE(second == std::vector<float>{1.5F, 1.5F, 2.25F, 2.25F});
  resampler.reset();
  REQUIRE(resampler.process(std::vector<float>{0, 0, 1, 1}, 1.0) == std::vector<float>{0, 0});
}

TEST_CASE("DAC paced audio holds a bounded queue with opposite clocks over twenty minutes") {
  const std::vector<float> pcm(960, 0.25F);
  for (const auto ppm : {-500.0, 500.0}) {
    AudioJitterBuffer jitter({30ms, 120ms});
    DriftController drift;
    StereoClockResampler resampler;
    double next_packet{};
    double queued{};
    const auto packet_interval = 0.01 / (1.0 + ppm / 1'000'000.0);
    std::uint32_t received{}, played{};
    unsigned missing{}, underruns{};
    bool primed{};
    Microseconds max_jitter{};
    for (unsigned tick = 0; tick < 600'000; ++tick) {
      const auto now = static_cast<double>(tick) * 0.002;
      while (next_packet <= now) {
        jitter.push({received++, 0, 480, {std::byte{1}}});
        next_packet += packet_interval;
      }
      if (!primed && !jitter.ready_for_playout()) continue;
      if (primed && queued < 96.0) ++underruns;
      queued = std::max(0.0, queued - 96.0);
      primed = true;
      max_jitter = std::max(max_jitter, jitter.buffered_duration());
      for (unsigned burst = 0; burst < 4 && queued < 960.0; ++burst) {
        const auto ratio = drift.update(jitter.buffered_duration() - 30ms).resample_ratio;
        if (jitter.pop(played++).kind != AudioPlayoutKind::Packet) ++missing;
        queued += static_cast<double>(resampler.process(pcm, ratio).size() / 2);
      }
    }
    INFO("clock difference ppm=" << ppm);
    REQUIRE(missing == 0);
    REQUIRE(underruns == 0);
    REQUIRE(max_jitter <= 60ms);
    REQUIRE(queued <= 1440.0);
  }
}
