#include "core/concurrency/bounded_queue.hpp"
#include "core/concurrency/latest_value.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

using namespace std::chrono_literals;
using namespace ministream;

TEST_CASE("LatestValue replaces unread stale state") {
  LatestValue<int> latest;
  latest.publish(10);
  latest.publish(20);

  REQUIRE(latest.take() == 20);
  REQUIRE_FALSE(latest.take());
}

TEST_CASE("BoundedQueue DropOldest preserves the newest work") {
  BoundedQueue<int> queue(2, OverflowPolicy::DropOldest);
  REQUIRE(queue.push(1));
  REQUIRE(queue.push(2));
  REQUIRE(queue.push(3));

  REQUIRE(queue.try_pop() == 2);
  REQUIRE(queue.try_pop() == 3);
  REQUIRE_FALSE(queue.try_pop());
}

TEST_CASE("BoundedQueue RejectNewest protects queued work") {
  BoundedQueue<int> queue(2, OverflowPolicy::RejectNewest);
  REQUIRE(queue.push(1));
  REQUIRE(queue.push(2));
  REQUIRE_FALSE(queue.push(3));

  REQUIRE(queue.try_pop() == 1);
  REQUIRE(queue.try_pop() == 2);
}

TEST_CASE("BoundedQueue wakes a waiting consumer") {
  BoundedQueue<int> queue(1, OverflowPolicy::RejectNewest);
  std::thread producer([&queue] {
    std::this_thread::sleep_for(1ms);
    queue.push(7);
  });

  const auto value = queue.wait_pop_for(100ms);
  producer.join();
  REQUIRE(value == 7);
}
