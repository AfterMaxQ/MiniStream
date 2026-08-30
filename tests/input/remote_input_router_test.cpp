#include "core/input/remote_input_router.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("remote input router releases all leases on escape") {
  InputCapture capture;
  std::size_t sent = 0;
  RemoteInputRouter router{capture, [&](const DesktopInput&) { ++sent; }};
  REQUIRE(router.begin());
  REQUIRE(router.active());
  REQUIRE(router.route({DesktopInputKind::MouseMove, 0, 1, 2, 0}));
  REQUIRE(sent == 1);
  REQUIRE_FALSE(router.route({DesktopInputKind::Key, 0, 0, 0, 0x1B}));
  REQUIRE_FALSE(router.active());
  REQUIRE_FALSE(capture.captured(InputDevice::Keyboard));
  REQUIRE_FALSE(capture.captured(InputDevice::Mouse));
  REQUIRE_FALSE(capture.captured(InputDevice::Gamepad));
}
