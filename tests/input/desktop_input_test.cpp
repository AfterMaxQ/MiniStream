#include "core/input/desktop_input.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("desktop input round trips every supported input contract") {
  const DesktopInput events[]{
      {DesktopInputKind::Key, 0, 0, 0, static_cast<std::uint16_t>(DesktopKey::W)},
      {DesktopInputKind::Key, kDesktopKeyRelease, 0, 0, static_cast<std::uint16_t>(DesktopKey::W)},
      {DesktopInputKind::MouseMove, 0, -120, 450, 0},
      {DesktopInputKind::MouseMove, kDesktopMouseGame, -120, 450, 0},
      {DesktopInputKind::MouseMove, kDesktopMouseAbsolute, 65535, 0, 0},
      {DesktopInputKind::MouseButton, 1, 0, 0, 0},
      {DesktopInputKind::MouseWheel, 0, 0, 120, 0},
      {DesktopInputKind::ReleaseAll, 0, 0, 0, 0},
      {DesktopInputKind::InputMode, 0, 0, 0, 0},
      {DesktopInputKind::InputMode, kInputModeGame, 0, 0, 0},
      {DesktopInputKind::InputMode, kInputModeGame | kInputModeEnglish, 0, 0, 0}};
  for (const auto& input : events) {
    CAPTURE(input.kind, input.flags);
    const auto bytes = encode_desktop_input(input);
    REQUIRE(bytes.size() == kDesktopInputBytes);
    REQUIRE(decode_desktop_input(bytes) == input);
  }
}

TEST_CASE("desktop input rejects unknown, contradictory and malformed events") {
  REQUIRE_FALSE(decode_desktop_input({}));
  const DesktopInput invalid[]{
      {static_cast<DesktopInputKind>(99), 0, 0, 0, 0},
      {DesktopInputKind::Key, 0, 0, 0, 87},
      {DesktopInputKind::ReleaseAll, 0, 0, 0, 1},
      {DesktopInputKind::MouseMove, kDesktopMouseAbsolute | kDesktopMouseGame, 0, 0, 0},
      {DesktopInputKind::MouseMove, kDesktopMouseAbsolute, -1, 0, 0},
      {DesktopInputKind::InputMode, kInputModeEnglish, 0, 0, 0},
      {DesktopInputKind::InputMode, 4, 0, 0, 0},
      {DesktopInputKind::InputMode, kInputModeGame, 1, 0, 0}};
  for (const auto& input : invalid) REQUIRE(encode_desktop_input(input).empty());
  auto bytes = encode_desktop_input({DesktopInputKind::InputMode, 0, 0, 0, 0});
  bytes[2] = std::byte{kInputModeEnglish};
  REQUIRE_FALSE(decode_desktop_input(bytes));
  bytes[0] = std::byte{99};
  REQUIRE_FALSE(decode_desktop_input(bytes));
}
