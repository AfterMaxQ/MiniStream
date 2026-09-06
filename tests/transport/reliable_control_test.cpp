#include "core/transport/reliable_control.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace std::chrono_literals;
using namespace ministream;

TEST_CASE("reliable control removes acknowledged work") {
  ReliableControl control;
  const auto start = SteadyClock::time_point{};
  const auto sequence = control.send({0, ControlKind::Start, {}}, start);
  REQUIRE(sequence == ControlSeq{1});
  control.acknowledge(*sequence);
  REQUIRE(control.due_retries(start + 1s).empty());
  REQUIRE(control.pending() == 0);
}

TEST_CASE("reliable control follows bounded retry deadlines") {
  ReliableControl control;
  const auto start = SteadyClock::time_point{};
  REQUIRE(control.send({0, ControlKind::RequestIdr, {}}, start) == ControlSeq{1});

  REQUIRE(control.due_retries(start + 49ms).empty());
  REQUIRE(control.due_retries(start + 50ms).size() == 1);
  REQUIRE(control.due_retries(start + 149ms).empty());
  REQUIRE(control.due_retries(start + 150ms).size() == 1);
  REQUIRE(control.due_retries(start + 349ms).empty());
  REQUIRE(control.due_retries(start + 350ms).size() == 1);
  REQUIRE(control.due_retries(start + 749ms).empty());
  REQUIRE(control.due_retries(start + 750ms).size() == 1);
  REQUIRE(control.due_retries(start + 1149ms).empty());
  REQUIRE(control.due_retries(start + 1150ms).empty());
  REQUIRE(control.take_failures() == std::vector<ControlSeq>{1});
}

TEST_CASE("reliable control rejects work beyond its explicit bound") {
  ReliableControl control;
  const auto now = SteadyClock::time_point{};
  for (std::size_t i = 0; i < 64; ++i) {
    REQUIRE(control.send({0, ControlKind::Start, {}}, now));
  }
  REQUIRE_FALSE(control.send({0, ControlKind::Start, {}}, now));
}
