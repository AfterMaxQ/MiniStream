#include "windows/platform/host_capabilities.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("host readiness requires all four product capabilities") {
  HostCapabilities report{
      {true, "NVENC"}, {true, "WASAPI"}, {true, "ViGEmBus"}, {true, "UDP"}};
  REQUIRE(report.ready());
  report.controller.ready = false;
  REQUIRE_FALSE(report.ready());
}
