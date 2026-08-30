#include "core/transport/packet_scheduler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace std::chrono_literals;
using namespace ministream;

namespace {
Datagram marked(std::byte marker, std::size_t size = 10) {
  return Datagram{std::vector<std::byte>(size, marker)};
}
}  // namespace

TEST_CASE("packet scheduler never traps input behind video") {
  PacketScheduler scheduler;
  const auto now = SteadyClock::time_point{};
  scheduler.enqueue(Priority::Video, marked(std::byte{1}), now + 1s);
  scheduler.enqueue(Priority::Input, marked(std::byte{2}), now + 1s);

  const auto next = scheduler.next(now);
  REQUIRE(next.has_value());
  REQUIRE(next->bytes.front() == std::byte{2});
}

TEST_CASE("packet scheduler drops expired deadline-driven work") {
  PacketScheduler scheduler;
  const auto now = SteadyClock::time_point{};
  scheduler.enqueue(Priority::Video, marked(std::byte{1}), now + 1ms);
  scheduler.enqueue(Priority::Telemetry, marked(std::byte{2}), now + 1ms);
  REQUIRE_FALSE(scheduler.next(now + 2ms));
}

TEST_CASE("packet scheduler reports video serialization delay") {
  PacketScheduler scheduler;
  scheduler.set_video_rate(1'000'000);
  REQUIRE(scheduler.video_rate_bps() == 1'000'000);
  const auto now = SteadyClock::time_point{};
  scheduler.enqueue(Priority::Video, marked(std::byte{1}, 1000), now + 1s);
  scheduler.enqueue(Priority::Video, marked(std::byte{2}, 1000), now + 1s);
  REQUIRE(scheduler.estimated_video_queue_delay() == 16ms);
}

TEST_CASE("50 Mbps scheduler releases a real burst instead of two packets per tick") {
  PacketScheduler scheduler;
  scheduler.set_video_rate(50'000'000);
  const auto now = SteadyClock::time_point{};

  for (int index = 0; index < 300; ++index) {
    REQUIRE(scheduler.enqueue(Priority::Video, marked(std::byte{1}, 1200), now + 1s));
  }

  const auto first = scheduler.drain(now, 256);
  const auto first_count = first.size();
  REQUIRE(first_count > 32);

  const auto second = scheduler.drain(now + 10ms, 256);
  const auto second_count = second.size();
  REQUIRE(second_count >= 40);
}
