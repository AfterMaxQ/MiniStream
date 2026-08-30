#include "macos/platform/controlled_backend.hpp"
#include "macos/input/accessibility_input.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("macOS controlled backend can be stopped before capture starts") {
  MacControlledBackend backend;
  backend.stop();
  REQUIRE_FALSE(backend.next_video().has_value());
  REQUIRE_FALSE(backend.next_audio().has_value());
}

TEST_CASE("neutral HID usages map to macOS key codes") {
  REQUIRE(AccessibilityInput::native_key_code(DesktopKey::W) == 13);
  REQUIRE(AccessibilityInput::native_key_code(DesktopKey::Space) == 49);
  REQUIRE(AccessibilityInput::native_key_code(DesktopKey::LeftShift) == 56);
  REQUIRE(AccessibilityInput::native_key_code(DesktopKey::Escape) == 53);

  const auto space = AccessibilityInput::key_from_qt(' ', true);
  REQUIRE(space.has_value());
  REQUIRE(space->data == static_cast<std::uint16_t>(DesktopKey::Space));
}
