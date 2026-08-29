#include "core/input/gamepad_packet.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>

using namespace ministream;

TEST_CASE("gamepad packet preserves every normalized control at its extremes") {
  const GamepadPacket packet{
      0x01020304U,
      0x0102030405060708ULL,
      {0xA5A55A5AU, 0, std::numeric_limits<std::uint16_t>::max(),
       std::numeric_limits<std::int16_t>::min(),
       std::numeric_limits<std::int16_t>::max(), -1234, 5678}};

  const auto decoded = decode_gamepad_packet(encode_gamepad_packet(packet));
  REQUIRE(decoded.has_value());
  REQUIRE(*decoded == packet);
}

TEST_CASE("gamepad latest-state filter rejects old packets across sequence wrap") {
  GamepadSequenceFilter filter;
  REQUIRE(filter.accept(0xFFFFFFFEU));
  REQUIRE(filter.accept(1U));
  REQUIRE_FALSE(filter.accept(0xFFFFFFFFU));
  REQUIRE_FALSE(filter.accept(1U));
}

TEST_CASE("gamepad packet rejects malformed wire size") {
  auto bytes = encode_gamepad_packet(GamepadPacket{});
  REQUIRE_FALSE(decode_gamepad_packet(
      std::span<const std::byte>{bytes}.first(bytes.size() - 1)).has_value());
}
