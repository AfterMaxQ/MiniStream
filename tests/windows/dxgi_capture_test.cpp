#include "windows/video/dxgi_capture.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("DXGI capture initializes the primary Windows output", "[.hardware]") {
  DxgiCapture capture;
  REQUIRE(capture.initialize());
}
