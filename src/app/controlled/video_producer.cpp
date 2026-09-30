#include "app/controlled/video_producer.hpp"
#include "core/time/clock.hpp"

#include <chrono>
#include <iostream>
#include <utility>

namespace ministream {

VideoProducer::VideoProducer(ControlledBackend& backend, std::uint32_t bitrate)
    : backend_(backend), config_(backend.codec_config()), bitrate_(bitrate),
      worker_([this] { run(); }) {}

VideoProducer::~VideoProducer() {
  {
    std::scoped_lock lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_one();
  worker_.join();
}

std::optional<ProducedVideo> VideoProducer::take() {
  std::optional<ProducedVideo> result;
  {
    std::scoped_lock lock(mutex_);
    result = std::move(pending_);
    pending_.reset();
  }
  wake_.notify_one();
  return result;
}

CodecConfig VideoProducer::config() const {
  std::scoped_lock lock(mutex_);
  return config_;
}

void VideoProducer::request_keyframe() {
  { std::scoped_lock lock(mutex_); keyframe_ = true; }
  wake_.notify_one();
}

void VideoProducer::request_bitrate(std::uint32_t bitrate) {
  { std::scoped_lock lock(mutex_); requested_bitrate_ = bitrate; }
  wake_.notify_one();
}

void VideoProducer::run() {
  for (;;) {
    bool keyframe;
    std::optional<std::uint32_t> rate;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [this] { return stopping_ || !pending_; });
      if (stopping_) return;
      keyframe = std::exchange(keyframe_, false);
      rate = std::exchange(requested_bitrate_, std::nullopt);
    }
    if (rate && *rate != bitrate_.load()) {
      if (backend_.reconfigure_bitrate(*rate)) bitrate_.store(*rate);
      else std::clog << "rate update rejected by encoder; keeping bitrate=" << bitrate_.load() << '\n';
    }
    if (keyframe) backend_.request_keyframe();
    const auto started = SteadyClock::now();
    auto frame = backend_.next_video();
    const auto work_ms = std::chrono::duration<double, std::milli>(SteadyClock::now() - started).count();
    auto config = backend_.codec_config();
    std::unique_lock lock(mutex_);
    config_ = std::move(config);
    if (frame) pending_ = ProducedVideo{std::move(*frame), work_ms};
    else wake_.wait_for(lock, std::chrono::milliseconds{1},
                       [this] { return stopping_ || keyframe_ || requested_bitrate_; });
    if (stopping_) return;
  }
}

}  // namespace ministream
