#include "core/telemetry/stream_aggregator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace ministream;
using namespace std::chrono_literals;

TEST_CASE("stream aggregator publishes immutable summary no faster than ten hertz") {
  StreamAggregator aggregator;
  const auto start = SteadyClock::time_point{};
  for (int index = 1; index <= 5; ++index) {
    aggregator.push(StreamSample{
        .capture_fps = 60.0,
        .encode_ms = static_cast<double>(index),
        .bitrate_bps = 50'000'000,
        .fec_ratio = 0.05,
        .send_queue_ms = static_cast<double>(index),
        .rtt_ms = static_cast<double>(index * 2),
        .jitter_ms = 1.5,
        .loss_fraction = 0.001,
        .fec_recovered = 3,
        .fec_unrecoverable = 1,
        .decode_ms = static_cast<double>(index + 1),
        .render_fps = 59.9,
        .audio_buffer_ms = 10.0,
        .audio_underruns = 2,
        .audio_drift_ppm = 100.0,
        .controller_connected = true,
        .input_latency_ms = static_cast<double>(index)});
  }

  const auto first = aggregator.publish_if_due(start);
  REQUIRE(first.has_value());
  REQUIRE(first->encode_ms_avg == 3.0);
  REQUIRE(first->encode_ms_p95 == 4.8);
  REQUIRE(first->send_queue_ms_p95 == 4.8);
  REQUIRE(first->rtt_ms_p95 == 9.6);
  REQUIRE(first->decode_ms_avg == 4.0);
  REQUIRE(first->input_latency_ms_p50 == 3.0);
  REQUIRE(first->input_latency_ms_p95 == 4.8);
  REQUIRE(first->audio_underruns == 2);
  REQUIRE(first->controller_connected);

  REQUIRE_FALSE(aggregator.publish_if_due(start + 99ms).has_value());
  REQUIRE(aggregator.publish_if_due(start + 100ms).has_value());
}
