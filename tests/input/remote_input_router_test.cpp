#include "core/input/remote_input_router.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("remote input router forwards escape while remote mode is active") {
  InputCapture capture;
  std::size_t sent = 0;
  RemoteInputRouter router{capture, [&](const DesktopInput&) { ++sent; }};
  REQUIRE(router.begin());
  REQUIRE(router.active());
  REQUIRE(router.route({DesktopInputKind::MouseMove, 0, 1, 2, 0}));
  REQUIRE(sent == 1);
  REQUIRE(router.route({DesktopInputKind::Key, 0, 0, 0,
                        static_cast<std::uint16_t>(DesktopKey::Escape)}));
  REQUIRE(sent == 2);
  REQUIRE(router.active());
  router.end();
  REQUIRE_FALSE(router.active());
  REQUIRE_FALSE(capture.captured(InputDevice::Keyboard));
  REQUIRE_FALSE(capture.captured(InputDevice::Mouse));
  REQUIRE_FALSE(capture.captured(InputDevice::Gamepad));
}
