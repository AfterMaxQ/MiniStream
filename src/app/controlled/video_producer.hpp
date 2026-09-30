#pragma once

#include "platform/controlled_backend.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace ministream {

struct ProducedVideo {
  EncodedFrame frame;
  double work_ms{};
};

// Owns the backend's video operations until destruction joins the worker.
// Audio and input stay on the session thread. A single pending coded frame
// applies backpressure without dropping interdependent P frames.
class VideoProducer {
 public:
  VideoProducer(ControlledBackend& backend, std::uint32_t bitrate);
  ~VideoProducer();
  std::optional<ProducedVideo> take();
  CodecConfig config() const;
  void request_keyframe();
  void request_bitrate(std::uint32_t bitrate);
  std::uint32_t bitrate() const noexcept { return bitrate_.load(); }

 private:
  void run();
  ControlledBackend& backend_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  bool stopping_{};
  bool keyframe_{};
  std::optional<std::uint32_t> requested_bitrate_;
  std::optional<ProducedVideo> pending_;
  CodecConfig config_;
  std::atomic<std::uint32_t> bitrate_;
  std::thread worker_;
};

}  // namespace ministream
