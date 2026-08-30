#include "core/input/desktop_key.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("Qt keyboard values map to platform-neutral HID usages") {
  REQUIRE(desktop_key_from_qt('A') == DesktopKey::A);
  REQUIRE(desktop_key_from_qt('W') == DesktopKey::W);
  REQUIRE(desktop_key_from_qt('0') == DesktopKey::Digit0);
  REQUIRE(desktop_key_from_qt('9') == DesktopKey::Digit9);
  REQUIRE(desktop_key_from_qt(' ') == DesktopKey::Space);
  REQUIRE(desktop_key_from_qt(';') == DesktopKey::Semicolon);
  REQUIRE(desktop_key_from_qt('/') == DesktopKey::Slash);
  REQUIRE(desktop_key_from_qt(0x01000000U) == DesktopKey::Escape);
  REQUIRE(desktop_key_from_qt(0x01000020U) == DesktopKey::LeftShift);
  REQUIRE(desktop_key_from_qt(0x01000021U) == DesktopKey::LeftControl);
  REQUIRE(desktop_key_from_qt(0x0100003AU) == DesktopKey::F11);
  REQUIRE_FALSE(desktop_key_from_qt(0x0100FFFFU).has_value());
}

TEST_CASE("desktop key validation accepts only supported HID usages") {
  REQUIRE(desktop_key_from_wire(static_cast<std::uint16_t>(DesktopKey::W)) == DesktopKey::W);
  REQUIRE(desktop_key_from_wire(static_cast<std::uint16_t>(DesktopKey::Space)) ==
          DesktopKey::Space);
  REQUIRE_FALSE(desktop_key_from_wire(0).has_value());
  REQUIRE_FALSE(desktop_key_from_wire(0xFFFFU).has_value());
}
