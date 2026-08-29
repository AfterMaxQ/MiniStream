#include "core/input/input_coalescer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace ministream;
using namespace std::chrono_literals;

TEST_CASE("input coalescer emits one latest full state per millisecond") {
  InputCoalescer coalescer;
  const auto start = SteadyClock::time_point{};

  for (std::int16_t axis = 0; axis < 20; ++axis) {
    GamepadState state{};
    state.left_x = axis;
    coalescer.update(state, start + axis * 40us);
  }

  REQUIRE_FALSE(coalescer.flush_if_due(start + 999us).has_value());
  const auto packet = coalescer.flush_if_due(start + 1ms);
  REQUIRE(packet.has_value());
  REQUIRE(packet->sequence == 0);
  REQUIRE(packet->state.left_x == 19);
  REQUIRE_FALSE(coalescer.flush_if_due(start + 2ms).has_value());
}

TEST_CASE("button edge is emitted within the current coalescing window") {
  InputCoalescer coalescer;
  const auto start = SteadyClock::time_point{};
  GamepadState state{};
  state.buttons = 4;
  coalescer.update(state, start);

  const auto packet = coalescer.flush_if_due(start + 1ms);
  REQUIRE(packet.has_value());
  REQUIRE(packet->state.buttons == 4);
}
