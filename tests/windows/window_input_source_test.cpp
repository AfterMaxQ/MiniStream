#include "windows/input/window_input_source.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("window input source maps Qt keys to Windows virtual keys") {
  const auto escape = WindowInputSource::key(0x01000000U, true);
  REQUIRE(escape);
  REQUIRE(escape->data == VK_ESCAPE);
  REQUIRE(escape->flags == 0);

  const auto f11 = WindowInputSource::key(0x0100003AU, false);
  REQUIRE(f11);
  REQUIRE(f11->data == VK_F11);
  REQUIRE(f11->flags == KEYEVENTF_KEYUP);

  const auto punctuation = WindowInputSource::key(static_cast<std::uint32_t>(';'), true);
  REQUIRE(punctuation);
  REQUIRE(punctuation->data == VK_OEM_1);
}

TEST_CASE("window input source uses portable mouse button events") {
  const auto left_down = WindowInputSource::mouse_button(1, true);
  REQUIRE(left_down);
  REQUIRE(left_down->flags == static_cast<std::uint16_t>(DesktopMouseButton::Left));

  const auto right_up = WindowInputSource::mouse_button(2, false);
  REQUIRE(right_up);
  REQUIRE(right_up->flags == (static_cast<std::uint16_t>(DesktopMouseButton::Right) |
                              kDesktopMouseRelease));

  REQUIRE_FALSE(WindowInputSource::mouse_button(8, true));
}
