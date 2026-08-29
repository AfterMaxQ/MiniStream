#pragma once

#include "core/telemetry/stream_snapshot.hpp"
#include "core/telemetry/window_stats.hpp"
#include "core/time/clock.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

namespace ministream {

struct StreamSample {
  double capture_fps{};
  double encode_ms{};
  std::uint64_t bitrate_bps{};
  double fec_ratio{};
  double send_queue_ms{};
  double rtt_ms{};
  double jitter_ms{};
  double loss_fraction{};
  std::uint64_t fec_recovered{};
  std::uint64_t fec_unrecoverable{};
  double decode_ms{};
  double render_fps{};
  double audio_buffer_ms{};
  std::uint64_t audio_underruns{};
  double audio_drift_ppm{};
  bool controller_connected{};
  double input_latency_ms{};
};

class StreamAggregator {
 public:
  StreamAggregator();

  void push(const StreamSample& sample);
  std::optional<StreamSnapshot> publish_if_due(SteadyClock::time_point now);

 private:
  static constexpr auto kPublishInterval = std::chrono::milliseconds{100};
  WindowStats encode_ms_;
  WindowStats send_queue_ms_;
  WindowStats rtt_ms_;
  WindowStats decode_ms_;
  WindowStats input_latency_ms_;
  StreamSample latest_{};
  bool has_sample_{};
  std::optional<SteadyClock::time_point> last_publish_;
};

}  // namespace ministream
