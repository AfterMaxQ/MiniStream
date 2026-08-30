#include "windows/platform/host_capabilities.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("host readiness requires media input and network capabilities") {
  HostCapabilities report{{true, "NVENC"},
                          {true, "WASAPI"},
                          {true, "SendInput"},
                          {false, "ViGEmBus optional"},
                          {true, "UDP"}};
  REQUIRE(report.ready());
  report.input.ready = false;
  REQUIRE_FALSE(report.ready());

  report.input.ready = true;
  report.controller.ready = false;
  REQUIRE(report.ready());
}
