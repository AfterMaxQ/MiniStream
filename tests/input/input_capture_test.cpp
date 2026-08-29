#include "core/input/input_capture.hpp"

#include <catch2/catch_test_macros.hpp>

using ministream::InputCapture;
using ministream::InputDevice;

TEST_CASE("input capture starts local and cannot capture devices") {
  InputCapture capture;

  REQUIRE_FALSE(capture.remote());
  REQUIRE_FALSE(capture.capture(InputDevice::Keyboard).has_value());
  REQUIRE_FALSE(capture.routes_to_remote(InputDevice::Mouse));
}

TEST_CASE("input capture leases route selected devices and release on scope exit") {
  InputCapture capture;
  REQUIRE(capture.enter_remote());

  {
    auto keyboard = capture.capture(InputDevice::Keyboard);
    auto mouse = capture.capture(InputDevice::Mouse);
    REQUIRE(keyboard.has_value());
    REQUIRE(mouse.has_value());
    REQUIRE(capture.routes_to_remote(InputDevice::Keyboard));
    REQUIRE(capture.routes_to_remote(InputDevice::Mouse));
    REQUIRE_FALSE(capture.routes_to_remote(InputDevice::Gamepad));
    REQUIRE_FALSE(capture.capture(InputDevice::Keyboard).has_value());
  }

  REQUIRE_FALSE(capture.captured(InputDevice::Keyboard));
  REQUIRE_FALSE(capture.captured(InputDevice::Mouse));
  REQUIRE(capture.remote());
}

TEST_CASE("leaving remote mode releases every device and invalidates leases") {
  InputCapture capture;
  REQUIRE(capture.enter_remote());
  auto gamepad = capture.capture(InputDevice::Gamepad);
  REQUIRE(gamepad.has_value());

  capture.leave_remote();

  REQUIRE_FALSE(capture.remote());
  REQUIRE_FALSE(capture.captured(InputDevice::Gamepad));
  REQUIRE_FALSE(capture.routes_to_remote(InputDevice::Gamepad));
  REQUIRE_FALSE(gamepad->active());
  REQUIRE_FALSE(capture.capture(InputDevice::Gamepad).has_value());
}

TEST_CASE("moving a lease keeps release ownership explicit") {
  InputCapture capture;
  REQUIRE(capture.enter_remote());
  auto first = capture.capture(InputDevice::Mouse);
  REQUIRE(first.has_value());
  auto second = std::move(*first);

  REQUIRE_FALSE(first->active());
  REQUIRE(second.active());
  second.release();
  REQUIRE_FALSE(capture.captured(InputDevice::Mouse));
}
