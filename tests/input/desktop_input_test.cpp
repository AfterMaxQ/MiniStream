#include "core/input/desktop_input.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ministream;

TEST_CASE("desktop input has a bounded reversible wire format") {
  const DesktopInput input{DesktopInputKind::MouseMove, 3, -120, 450, 0};
  const auto bytes = encode_desktop_input(input);
  REQUIRE(bytes.size() == kDesktopInputBytes);
  const auto decoded = decode_desktop_input(bytes);
  REQUIRE(decoded.has_value());
  REQUIRE(*decoded == input);
}

TEST_CASE("desktop input rejects malformed or unknown events") {
  REQUIRE_FALSE(decode_desktop_input({}));
  std::vector<std::byte> bytes(kDesktopInputBytes);
  bytes[0] = static_cast<std::byte>(99);
  REQUIRE_FALSE(decode_desktop_input(bytes));
}
