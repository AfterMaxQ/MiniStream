#include "windows/input/desktop_key_windows.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("neutral HID usages map to Windows set-one scan codes") {
  REQUIRE((windows_key_translation(DesktopKey::W) == WindowsKeyTranslation{0x11, false}));
  REQUIRE((windows_key_translation(DesktopKey::Space) ==
           WindowsKeyTranslation{0x39, false}));
  REQUIRE((windows_key_translation(DesktopKey::Up) == WindowsKeyTranslation{0x48, true}));
  REQUIRE((windows_key_translation(DesktopKey::LeftMeta) ==
           WindowsKeyTranslation{0x5B, true}));
}
