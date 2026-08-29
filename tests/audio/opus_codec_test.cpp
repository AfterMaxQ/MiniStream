#include "core/audio/opus_codec.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace ministream;

TEST_CASE("Opus low-delay wrapper preserves frame shape and finite output") {
  OpusEncoder48kStereo encoder;
  OpusDecoder48kStereo decoder;
  REQUIRE(encoder.ready());
  REQUIRE(decoder.ready());

  std::vector<float> samples(kOpusFrameSamplesPerChannel * 2);
  for (std::size_t frame = 0; frame < kOpusFrameSamplesPerChannel; ++frame) {
    const auto value = static_cast<float>(
        std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(frame) / 48000.0));
    samples[frame * 2] = value;
    samples[frame * 2 + 1] = value;
  }
  const auto compressed = encoder.encode(samples);
  REQUIRE(compressed);
  REQUIRE_FALSE(compressed->empty());
  const auto decoded = decoder.decode(*compressed);
  REQUIRE(decoded);
  REQUIRE(decoded->size() == samples.size());
  REQUIRE(std::ranges::all_of(*decoded, [](float value) { return std::isfinite(value); }));

  const auto concealed = decoder.decode_loss();
  REQUIRE(concealed);
  REQUIRE(concealed->size() == samples.size());
}

TEST_CASE("Opus encoder rejects anything except one 10 ms stereo frame") {
  OpusEncoder48kStereo encoder;
  REQUIRE_FALSE(encoder.encode(std::vector<float>(100)));
}
