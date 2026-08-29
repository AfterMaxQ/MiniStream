#pragma once

#include <cstdint>

namespace ministream {

struct StreamSnapshot {
  double capture_fps{};
  double encode_ms_avg{};
  double encode_ms_p95{};
  std::uint64_t bitrate_bps{};
  double fec_ratio{};
  double send_queue_ms_p95{};
  double rtt_ms_avg{};
  double rtt_ms_p95{};
  double jitter_ms{};
  double loss_fraction{};
  std::uint64_t fec_recovered{};
  std::uint64_t fec_unrecoverable{};
  double decode_ms_avg{};
  double decode_ms_p95{};
  double render_fps{};
  double audio_buffer_ms{};
  std::uint64_t audio_underruns{};
  double audio_drift_ppm{};
  bool controller_connected{};
  double input_latency_ms_p50{};
  double input_latency_ms_p95{};
};

}  // namespace ministream
