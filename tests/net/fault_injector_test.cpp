#include "core/net/fault_injector.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("fault injector reproduces configured packet bursts") {
  FaultInjector injector({0.0, Microseconds{0}, 10, 5, 42});
  for (std::uint64_t packet = 0; packet < 10; ++packet) {
    REQUIRE_FALSE(injector.decide(packet).drop);
  }
  for (std::uint64_t packet = 10; packet < 15; ++packet) {
    REQUIRE(injector.decide(packet).drop);
  }
  REQUIRE_FALSE(injector.decide(15).drop);
}

TEST_CASE("fault injector seed makes random loss and jitter reproducible") {
  const FaultConfig config{0.25, Microseconds{5000}, 0, 0, 42};
  FaultInjector first(config);
  FaultInjector second(config);
  for (std::uint64_t packet = 0; packet < 100; ++packet) {
    REQUIRE(first.decide(packet) == second.decide(packet));
  }
}
