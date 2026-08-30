#include "macos/platform/controlled_backend.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("macOS controlled backend can be stopped before capture starts") {
  MacControlledBackend backend;
  backend.stop();
  REQUIRE_FALSE(backend.next_video().has_value());
  REQUIRE_FALSE(backend.next_audio().has_value());
}
