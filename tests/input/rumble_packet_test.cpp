#include "core/input/rumble_packet.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>

using namespace ministream;

TEST_CASE("rumble feedback has a fixed six-byte wire format") {
  const RumblePacket packet{0x1234U, 0xABCDU, 250U};
  const auto bytes = encode_rumble_packet(packet);
  REQUIRE(bytes == std::array<std::byte, 6>{
                       std::byte{0x12}, std::byte{0x34}, std::byte{0xAB},
                       std::byte{0xCD}, std::byte{0x00}, std::byte{0xFA}});
  REQUIRE(decode_rumble_packet(bytes) == packet);
}
