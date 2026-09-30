#include "app/controlled/video_producer.hpp"
#include "core/time/clock.hpp"

#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>

using namespace ministream;
using namespace std::chrono_literals;

namespace {
class SlowVideoBackend final : public ControlledBackend {
 public:
  SlowVideoBackend() : resume(allow.get_future().share()) {}
  ControlledCapabilities inspect() const override { return {}; }
  bool start() override { return true; }
  void stop() noexcept override {}
  std::optional<EncodedFrame> next_video() override {
    if (calls++ == 0) entered.set_value();
    resume.wait();
    return EncodedFrame{calls.load() - 1, 0, false, {std::byte{1}}};
  }
  std::optional<PcmBlock> next_audio() override { ++audio_calls; return std::nullopt; }
  bool inject_input(const DesktopInput&) override { ++inputs; return true; }
  bool reconfigure_bitrate(std::uint32_t rate) override { return rate != 0; }
  std::promise<void> entered;
  std::promise<void> allow;
  std::shared_future<void> resume;
  std::atomic<std::uint32_t> calls{};
  unsigned audio_calls{};
  unsigned inputs{};
};
}

TEST_CASE("GPU encode wait leaves video dequeue input and audio responsive") {
  SlowVideoBackend backend;
  auto entered = backend.entered.get_future();
  auto producer = std::make_unique<VideoProducer>(backend, 8'000'000);
  const bool started = entered.wait_for(1s) == std::future_status::ready;
  const auto before = SteadyClock::now();
  const auto pending = producer->take();
  producer->request_keyframe();
  producer->request_bitrate(9'000'000);
  (void)producer->config();
  (void)backend.next_audio();
  (void)backend.inject_input({});
  const auto elapsed = SteadyClock::now() - before;
  backend.allow.set_value();
  producer.reset();
  REQUIRE(started);
  REQUIRE_FALSE(pending);
  REQUIRE(elapsed < 60ms);
  REQUIRE(backend.inputs == 1);
  REQUIRE(backend.audio_calls == 1);
}

TEST_CASE("video producer backpressure preserves coded frame order and bounds its queue") {
  SlowVideoBackend backend;
  backend.allow.set_value();
  VideoProducer producer(backend, 8'000'000);
  const auto until = SteadyClock::now() + 1s;
  while (backend.calls == 0 && SteadyClock::now() < until) std::this_thread::sleep_for(1ms);
  std::this_thread::sleep_for(10ms);
  REQUIRE(backend.calls == 1);
  const auto first = producer.take();
  REQUIRE(first);
  REQUIRE(first->frame.frame_id == 0);
  producer.request_bitrate(9'000'000);
  std::optional<ProducedVideo> second;
  while (!second && SteadyClock::now() < until) {
    second = producer.take();
    std::this_thread::sleep_for(1ms);
  }
  REQUIRE(second);
  REQUIRE(second->frame.frame_id == 1);
}
