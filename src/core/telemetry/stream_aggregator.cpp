#include "core/telemetry/stream_aggregator.hpp"

namespace ministream {

StreamAggregator::StreamAggregator()
    : encode_ms_(300), send_queue_ms_(300), rtt_ms_(300), decode_ms_(300),
      input_latency_ms_(300) {}

void StreamAggregator::push(const StreamSample& sample) {
  latest_ = sample;
  has_sample_ = true;
  encode_ms_.push(sample.encode_ms);
  send_queue_ms_.push(sample.send_queue_ms);
  rtt_ms_.push(sample.rtt_ms);
  decode_ms_.push(sample.decode_ms);
  input_latency_ms_.push(sample.input_latency_ms);
}

std::optional<StreamSnapshot> StreamAggregator::publish_if_due(
    SteadyClock::time_point now) {
  if (!has_sample_ ||
      (last_publish_.has_value() && now - *last_publish_ < kPublishInterval)) {
    return std::nullopt;
  }
  last_publish_ = now;
  return StreamSnapshot{
      latest_.capture_fps,
      encode_ms_.mean(),
      encode_ms_.percentile(0.95),
      latest_.bitrate_bps,
      latest_.fec_ratio,
      send_queue_ms_.percentile(0.95),
      rtt_ms_.mean(),
      rtt_ms_.percentile(0.95),
      latest_.jitter_ms,
      latest_.loss_fraction,
      latest_.fec_recovered,
      latest_.fec_unrecoverable,
      decode_ms_.mean(),
      decode_ms_.percentile(0.95),
      latest_.render_fps,
      latest_.audio_buffer_ms,
      latest_.audio_underruns,
      latest_.audio_drift_ppm,
      latest_.controller_connected,
      input_latency_ms_.percentile(0.50),
      input_latency_ms_.percentile(0.95)};
}

}  // namespace ministream
